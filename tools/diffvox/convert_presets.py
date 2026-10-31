#!/usr/bin/env python3
"""
MusicDAWM R5-light: DiffVox vocal presets -> Final Mix vocal presets.

DiffVox (https://github.com/SonyResearch/diffvox, MIT, (c) 2025 Chin-Yun Yu)
ships the effect settings it retrieved for 455 professionally mixed vocals
(presets/internal, presets/medleydb). This script, run once by a developer
(numpy only, not shipped):

  1. decodes the raw parameters into physical units, following DiffVox's
     parametrisations (modules/fx.py, torchcomp) and presets/fx_config.yaml;
  2. describes each preset (EQ curve, compression, reverb) and picks about 10
     representative ones (k-means, the preset nearest each centre);
  3. translates them to MusicDAWM's built-in effects: x42-fil4 (EQ),
     ACE Compressor, and the Final Mix Space send (reverb amount); the
     ping-pong delay is left out (D-047);
  4. computes, with DiffVox's own effects (EQ biquads, compressor-expander,
     FDN reverb), what each preset does to a synthetic test vocal at 48 kHz:
     the EQ response, the loudness and band balance, the reverb level. The
     C++ test (session_utils/final_mix_test.cc) checks MusicDAWM's
     translation against these numbers.

Usage: convert_presets.py <diffvox-repo> <out.xml> [<decoded.json> [<space-bus-gain-db>]]
"""

import json
import math
import sys
from pathlib import Path

import numpy as np
import yaml

SR_FIT = 44100  # DiffVox's sample rate (gamma and smoothing coefficients)
SR = 48000  # the test sessions' sample rate
N_PRESETS = 10
THIRD_OCT = [20 * 2 ** (k / 3) for k in range(31)]  # 20 Hz .. 20 kHz
EDGES = [80, 250, 1000, 4000, 8000]  # FinalMix::analyse bands


def sigmoid(x):
    return 1 / (1 + np.exp(-x))


def minmax(x, lo, hi):
    return sigmoid(x) * (hi - lo) + lo


def coef2ms(c, sr=SR_FIT):
    return -2200 / (sr * math.log(1 - c))


# ---------------------------------------------------------------- decoding


def decode(row, keys, shapes, cfg):
    """One raw parameter vector -> physical parameters (dict)."""
    vals = {}
    i = 0
    for k, sh in zip(keys, shapes):
        n = int(np.prod(sh)) if sh else 1
        vals[k] = np.array(row[i : i + n]).reshape(sh) if sh else float(row[i])
        i += n
    assert i == len(row), (i, len(row))

    m = cfg["model"]["_args_"]
    eq = []
    for j in range(6):
        c = m[j]
        t = c["_target_"].split(".")[-1]
        f = minmax(vals[f"{j}.params.parametrizations.freq.original"], c["min_freq"], c["max_freq"])
        d = {"type": t, "freq": float(f)}
        if f"{j}.params.gain" in vals:
            d["gain"] = vals[f"{j}.params.gain"]
        if f"{j}.params.parametrizations.Q.original" in vals:
            lo, hi = (0.5, 10.0) if t in ("LowPass", "HighPass") else (0.2, 20.0)
            d["Q"] = float(minmax(vals[f"{j}.params.parametrizations.Q.original"], c.get("min_Q", lo), c.get("max_Q", hi)))
        else:
            d["Q"] = 0.707
        eq.append(d)

    comp = {
        "cmp_th": vals["6.params.cmp_th"],
        "exp_th": vals["6.params.exp_th"],
        "make_up": vals["6.params.make_up"],
        "lookahead_ms": float(abs(vals["6.params.parametrizations.lookahead.original"][0]) % 15.0),
        "at_ms": coef2ms(float(sigmoid(vals["6.params.parametrizations.at.original"]))),
        "rt_ms": coef2ms(float(sigmoid(vals["6.params.parametrizations.rt.original"]))),
        "avg_coef": float(sigmoid(vals["6.params.parametrizations.avg_coef.original"])),
        "cmp_ratio": float(minmax(vals["6.params.parametrizations.cmp_ratio.original"], 1, 20)),
        "exp_ratio": float(sigmoid(vals["6.params.parametrizations.exp_ratio.original"])),
    }

    fdn_cfg = m[7]["_args_"][1]
    delays = np.array(fdn_cfg["delays"], dtype=float)
    t60 = fdn_cfg["ir_duration"] * 0.75
    gamma_max = 10 ** ((-60 / SR_FIT / t60 * delays.min()) / 20)
    gamma = minmax(vals["7.effects.1.params.parametrizations.gamma.original"], 0, gamma_max)  # (49, 1)
    U = vals["7.effects.1.params.parametrizations.U.original"]
    tri = np.triu(U, 1)
    reverb_eq = []
    for j, c in enumerate(fdn_cfg["eq"]["_args_"]):
        t = c["_target_"].split(".")[-1]
        f = minmax(vals[f"7.effects.1.eq.{j}.params.parametrizations.freq.original"], c["min_freq"], c["max_freq"])
        d = {"type": t, "freq": float(f), "gain": vals[f"7.effects.1.eq.{j}.params.gain"]}
        qk = f"7.effects.1.eq.{j}.params.parametrizations.Q.original"
        d["Q"] = float(minmax(vals[qk], c.get("min_Q", 0.2), c.get("max_Q", 20))) if qk in vals else 0.707
        reverb_eq.append(d)
    reverb = {
        "b": vals["7.effects.1.params.b"].tolist(),
        "c": vals["7.effects.1.params.c"].tolist(),
        "gamma": gamma.reshape(-1).tolist(),
        "A_generator": (tri - tri.T).tolist(),  # U = expm(A_generator)
        "delays": delays.tolist(),
        "eq": reverb_eq,
    }
    pan = float(sigmoid(vals["7.pan.params.parametrizations.pan.original"]))
    return {"eq": eq, "comp": comp, "reverb": reverb, "pan": pan}


# ---------------------------------------------------------------- filters (DiffVox's RBJ biquads)


def biquad(t, f, gain, Q, sr):
    w0 = 2 * math.pi * f / sr
    alpha = math.sin(w0) / 2 / Q
    c = math.cos(w0)
    A = 10 ** (gain / 40)
    if t == "HighPass":
        return [(1 + c) / 2, -1 - c, (1 + c) / 2], [1 + alpha, -2 * c, 1 - alpha]
    if t == "LowPass":
        return [(1 - c) / 2, 1 - c, (1 - c) / 2], [1 + alpha, -2 * c, 1 - alpha]
    if t == "Peak":
        return [1 + alpha * A, -2 * c, 1 - alpha * A], [1 + alpha / A, -2 * c, 1 - alpha / A]
    s = math.sqrt(A)
    if t == "LowShelf":
        return (
            [A * (A + 1 - (A - 1) * c + 2 * alpha * s), 2 * A * (A - 1 - (A + 1) * c), A * (A + 1 - (A - 1) * c - 2 * alpha * s)],
            [A + 1 + (A - 1) * c + 2 * alpha * s, -2 * (A - 1 + (A + 1) * c), A + 1 + (A - 1) * c - 2 * alpha * s],
        )
    if t == "HighShelf":
        return (
            [A * (A + 1 + (A - 1) * c + 2 * alpha * s), -2 * A * (A - 1 + (A + 1) * c), A * (A + 1 + (A - 1) * c - 2 * alpha * s)],
            [A + 1 - (A - 1) * c + 2 * alpha * s, 2 * (A - 1 - (A + 1) * c), A + 1 - (A - 1) * c - 2 * alpha * s],
        )
    raise ValueError(t)


def response(filters, freqs, sr):
    """Complex response of a cascade of biquads."""
    z = np.exp(-1j * 2 * np.pi * np.asarray(freqs) / sr)
    h = np.ones(len(z), dtype=complex)
    for d in filters:
        b, a = biquad(d["type"], d["freq"], d.get("gain", 0.0), d["Q"], sr)
        h *= (b[0] + b[1] * z + b[2] * z * z) / (a[0] + a[1] * z + a[2] * z * z)
    return h


def lfilter(b, a, x):
    """Direct form II transposed biquad (no scipy)."""
    b = np.asarray(b, float) / a[0]
    a = np.asarray(a, float) / a[0]
    y = np.empty_like(x)
    z1 = z2 = 0.0
    for n, xn in enumerate(x):
        yn = b[0] * xn + z1
        z1 = b[1] * xn - a[1] * yn + z2
        z2 = b[2] * xn - a[2] * yn
        y[n] = yn
    return y


def apply_eq(filters, x, sr):
    for d in filters:
        b, a = biquad(d["type"], d["freq"], d.get("gain", 0.0), d["Q"], sr)
        x = lfilter(b, a, x)
    return x


# ---------------------------------------------------------------- compressor-expander (torchcomp)


def compexp(x, p, sr):
    at = 1 - math.exp(-2200 / p["at_ms"] / sr)
    rt = 1 - math.exp(-2200 / p["rt_ms"] / sr)
    a = p["avg_coef"]
    sq = np.maximum(x * x, 1e-8)
    ms = np.empty_like(sq)
    s = 0.0
    for n, v in enumerate(sq):
        s = s + a * (v - s)
        ms[n] = s
    lvl = 10 * np.log10(ms)
    cs = 1 - 1 / p["cmp_ratio"]
    es = 1 - 1 / p["exp_ratio"]
    g = -np.maximum(-np.minimum(cs * (p["cmp_th"] - lvl), es * (p["exp_th"] - lvl)), 0)
    f = 10 ** (g / 20)
    y = np.empty_like(f)
    gg = 1.0
    for n, fn in enumerate(f):
        c = at if fn < gg else rt
        gg = gg * (1 - c) + c * fn
        y[n] = gg
    la = int(round(p["lookahead_ms"] * 0.001 * sr))
    if la:
        y = np.concatenate([y[la:], np.full(la, y[-1])])
    return x * y * 10 ** (p["make_up"] / 20)


# ---------------------------------------------------------------- FDN reverb


def expm(m):
    """Matrix exponential (scaling and squaring, Taylor)."""
    n = max(0, int(math.ceil(math.log2(max(1e-12, np.abs(m).sum(1).max())))) + 1)
    a = m / 2 ** n
    e = np.eye(len(m))
    t = np.eye(len(m))
    for k in range(1, 20):
        t = t @ a / k
        e = e + t
    for _ in range(n):
        e = e @ e
    return e


def fdn_power(rev, freqs):
    """|H|^2 summed over both outputs, for a mono input sent to both
    inputs, at freqs (Hz); DiffVox's FDN at 44.1 kHz, then its EQ."""
    delays = np.array(rev["delays"])
    U = expm(np.array(rev["A_generator"]))
    gamma = np.array(rev["gamma"])
    gf = np.interp(freqs, np.linspace(0, SR_FIT / 2, len(gamma)), gamma)
    w = 2 * np.pi * np.asarray(freqs) / SR_FIT
    b = np.array(rev["b"]) @ np.array([1.0, 1.0])  # (6,)
    c = np.array(rev["c"])  # (2, 6)
    out = np.empty(len(w))
    for i, (wi, gi) in enumerate(zip(w, gf)):
        g = gi ** (delays / delays.min())
        M = np.diag(np.exp(1j * wi * delays)) - U * g[None, :]
        v = np.linalg.solve(M, b + 0j)
        h = c @ v
        out[i] = np.sum(np.abs(h) ** 2)
    eqr = np.abs(response(rev["eq"], np.minimum(freqs, SR_FIT / 2 - 1), SR_FIT)) ** 2
    return out * eqr


def t60(rev):
    """Reverb time (s) at 500 Hz - 2 kHz, from the decay per sample."""
    delays = np.array(rev["delays"])
    gamma = np.array(rev["gamma"])
    f = np.linspace(0, SR_FIT / 2, len(gamma))
    sel = (f >= 500) & (f <= 2000)
    g = np.clip(gamma[sel], 1e-6, 0.999999)
    return float(np.median(-60 * delays.min() / (SR_FIT * 20 * np.log10(g))))


# ---------------------------------------------------------------- measures (as MusicDAWM measures them)


def k_weighting(sr):
    # BS.1770 pre-filter and RLB filter, as in pyloudnorm
    f0, G, Q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    K = math.tan(math.pi * f0 / sr)
    Vh = 10 ** (G / 20)
    Vb = Vh ** 0.4996667741545416
    a0 = 1 + K / Q + K * K
    b1 = [(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0]
    a1 = [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0]
    f0, Q = 38.13547087602444, 0.5003270373238773
    K = math.tan(math.pi * f0 / sr)
    a2 = [1, 2 * (K * K - 1) / (1 + K / Q + K * K), (1 - K / Q + K * K) / (1 + K / Q + K * K)]
    b2 = [1, -2, 1]
    return (b1, a1), (b2, a2)


def lufs(chans, sr):
    """Integrated loudness (gated) of a list of channels."""
    (b1, a1), (b2, a2) = k_weighting(sr)
    z = [lfilter(b2, a2, lfilter(b1, a1, x)) ** 2 for x in chans]
    blk = int(0.4 * sr)
    hop = int(0.1 * sr)
    pw = []
    for s in range(0, len(z[0]) - blk + 1, hop):
        pw.append(sum(float(np.mean(c[s : s + blk])) for c in z))
    pw = np.array(pw)
    l = -0.691 + 10 * np.log10(pw + 1e-30)
    pw = pw[l > -70]
    if not len(pw):
        return -200.0
    rel = -0.691 + 10 * math.log10(np.mean(pw)) - 10
    pw = pw[(-0.691 + 10 * np.log10(pw)) > rel]
    return -0.691 + 10 * math.log10(np.mean(pw))


def bands(x, sr):
    """FinalMix::analyse's six bands (mono = mid), dB relative to the whole."""
    def rbj(high, f):
        w = 2 * math.pi * f / sr
        al = math.sin(w) / (2 * math.sqrt(0.5))
        c = math.cos(w)
        a0 = 1 + al
        b0 = ((1 + c) / 2 if high else (1 - c) / 2) / a0
        b1 = (-(1 + c) if high else (1 - c)) / a0
        return [b0, b1, b0], [1, -2 * c / a0, (1 - al) / a0]

    e = []
    for b in range(6):
        y = x
        for _ in range(2):
            if b > 0:
                y = lfilter(*rbj(True, EDGES[b - 1]), y)
            if b < 5:
                y = lfilter(*rbj(False, EDGES[b]), y)
        e.append(float(np.sum(y * y)))
    s = sum(e)
    return [10 * math.log10(max(1e-12, v / s)) for v in e]


def test_vocal(sr, seconds=8.0):
    """A deterministic sung phrase: harmonic tone, gliding pitch, syllables
    of varying loudness, breath pauses. session_utils/final_mix_test.cc
    makes the same signal."""
    n = int(seconds * sr)
    t = np.arange(n) / sr
    f0 = 196 * (1 + 0.12 * np.sin(2 * np.pi * 0.31 * t) + 0.03 * np.sin(2 * np.pi * 5.5 * t))
    ph = 2 * np.pi * np.cumsum(f0) / sr
    x = np.zeros(n)
    for k in range(1, 25):
        # a vowel-like spectral envelope: formants near 700 Hz and 2.6 kHz
        fk = 196 * k
        amp = (1 / k) * (1 + 2.5 * math.exp(-((fk - 700) / 300) ** 2) + 1.5 * math.exp(-((fk - 2600) / 500) ** 2))
        x += amp * np.sin(k * ph)
    # syllables: 4 per second, loudness varying over 12 dB, gaps
    syl = 0.5 * (1 - np.cos(2 * np.pi * 4 * t))
    loud = 10 ** ((-6 + 6 * np.sin(2 * np.pi * 0.23 * t + 1)) / 20)
    gate = (np.sin(2 * np.pi * 0.5 * t) > -0.85).astype(float)
    return x * syl * loud * gate


# ---------------------------------------------------------------- translation to MusicDAWM


# ---------------------------------------------------------------- x42-fil4's own filters (src/ of fil4.lv2 v0.8.13)


def fil4_response(p, freqs, sr):
    """Response (dB) of x42-fil4 with ports p, settled: RBJ shelves with
    fil4's Q mapping (lv2.c, iir.h), Fons Adriaensen's peak sections
    (filters.h: a 2nd-order allpass lattice, out = (1 + a) x - a A(x)), the
    resonant high-pass (hip.h); the low-pass is not used."""
    z1 = np.exp(-1j * 2 * np.pi * np.asarray(freqs) / sr)
    h = np.ones(len(z1), dtype=complex) * 10 ** (p.get("gain", 0.0) / 20)
    for sh, lo, hi in (("LS", 25, 400), ("HS", 1000, 16000)):
        if p.get(sh + "sec", 0):
            q = min(2.0, max(0.25, 0.2129 + p[sh + "q"] / 2.25))
            d = {"type": "LowShelf" if sh == "LS" else "HighShelf", "freq": min(hi, max(lo, p[sh + "freq"])), "gain": p[sh + "gain"], "Q": q}
            b, a = biquad(d["type"], d["freq"], d["gain"], d["Q"], sr)
            h *= (b[0] + b[1] * z1 + b[2] * z1 * z1) / (a[0] + a[1] * z1 + a[2] * z1 * z1)
    for k in range(1, 5):
        if not p.get(f"sec{k}", 0):
            continue
        f = min(0.4998, max(0.0002, p[f"freq{k}"] / sr))
        g = 10 ** (p[f"gain{k}"] / 20)
        bw = p[f"q{k}"] * 7 * f / math.sqrt(g)
        s2 = (1 - bw) / (1 + bw)
        s1 = -math.cos(2 * math.pi * f)
        A = (s2 + s1 * (1 + s2) * z1 + z1 * z1) / (1 + s1 * (1 + s2) * z1 + s2 * z1 * z1)
        a = 0.5 * (g - 1)
        h *= (1 + a) - a * A
    if p.get("HighPass", 0):
        f = min(sr / 12, max(5.0, p["HPfreq"]))
        w = f / sr
        al = math.exp(-2 * math.pi * w)
        q = min(1.6, max(0.0, 0.7 + 0.78 * math.tanh(1.82 * (p["HPQ"] - 0.8))))
        g = 1 + w + 2 * q * w
        H1 = al * (1 - z1) / (1 - al * z1)
        h *= H1 * H1 * (g / al) / (1 + g * q * z1 * H1 * (H1 - 1))
    return 20 * np.log10(np.abs(h) + 1e-12)


FIT_F = np.geomspace(30, 16000, 120)
BOUNDS = {"HPfreq": (10, 1000), "HPQ": (0, 1.4), "LSfreq": (25, 400), "LSgain": (-18, 18), "LSq": (0.0625, 4),
          "HSfreq": (1000, 16000), "HSgain": (-18, 18), "HSq": (0.0625, 4), "gain": (-20, 20)}
for _k in range(1, 5):
    BOUNDS.update({f"freq{_k}": (20, 20000), f"q{_k}": (0.0625, 4), f"gain{_k}": (-18, 18)})
LOG = {"HPfreq", "LSfreq", "HSfreq", "freq1", "freq2", "freq3", "freq4", "q1", "q2", "q3", "q4", "LSq", "HSq"}


def fil4_settings(eq):
    """DiffVox EQ -> x42-fil4 ports, fitted on fil4's own response (30 Hz -
    16 kHz). Returns (ports, the largest error in dB)."""
    target = 20 * np.log10(np.abs(response(eq, FIT_F, SR)))
    peaks = [d for d in eq if d["type"] == "Peak"]
    ls = next(d for d in eq if d["type"] == "LowShelf")
    hs = next(d for d in eq if d["type"] == "HighShelf")
    hp = next(d for d in eq if d["type"] == "HighPass")
    p = {"HighPass": 1 if hp["freq"] >= 25 else 0, "HPfreq": min(1000.0, max(10.0, hp["freq"])), "HPQ": 0.7,
         "LowPass": 0, "LSsec": 1, "LSfreq": min(400.0, max(25.0, ls["freq"])), "LSq": 1.112, "LSgain": ls["gain"],
         "HSsec": 1, "HSfreq": min(16000.0, max(1000.0, hs["freq"])), "HSq": 1.112, "HSgain": hs["gain"], "gain": 0.0}
    for k in range(1, 5):
        d = peaks[k - 1] if k <= len(peaks) else {"freq": [700, 2500][k - 3], "gain": 0.0, "Q": 1.0}
        g = 10 ** (d["gain"] / 20)
        p.update({f"sec{k}": 1, f"freq{k}": d["freq"], f"q{k}": min(4.0, max(0.0625, math.sqrt(g) / (2.23 * d["Q"]))), f"gain{k}": d["gain"]})

    def loss(q):
        e = fil4_response(q, FIT_F, SR) - target
        return float(np.mean(e * e) + 0.5 * np.max(np.abs(e)) ** 2)

    keys = [k for k in BOUNDS if (k[:2] != "HP" or p["HighPass"])]
    cur = loss(p)
    step = {k: (0.25 if k in LOG else 2.0) for k in keys}
    for _ in range(60):
        improved = False
        for k in keys:
            for sgn in (1, -1):
                q = dict(p)
                lo, hi = BOUNDS[k]
                q[k] = min(hi, max(lo, p[k] * math.exp(sgn * step[k]) if k in LOG else p[k] + sgn * step[k]))
                l = loss(q)
                if l < cur:
                    p, cur, improved = q, l, True
                    break
        if not improved:
            step = {k: v / 2 for k, v in step.items()}
            if max(step.values()) < 1e-3:
                break
    err = float(np.max(np.abs(fil4_response(p, FIT_F, SR) - target)))
    return p, err


def describe(d):
    """Plain-language features of a preset."""
    r = np.abs(response(d["eq"], [100, 300, 3000, 10000], SR_FIT))
    lo, mid_lo, pres, air = (20 * np.log10(r)).tolist()
    c = d["comp"]
    rev = d["reverb"]
    return {"low": lo, "lowmid": mid_lo, "presence": pres, "air": air,
            "ratio": c["cmp_ratio"], "thresh": c["cmp_th"], "t60": t60(rev)}


def name_for(f, wet):
    """A plain name from what the preset does."""
    tone = ""
    if f["air"] - f["lowmid"] > 6:
        tone = "Bright"
    elif f["lowmid"] - f["air"] > 3:
        tone = "Warm"
    if f["presence"] - f["lowmid"] > 4:
        tone = (tone + ", present") if tone else "Present"
    comp = (f["ratio"] - 1) * max(0.0, -18 - f["thresh"] + 6)
    dyn = "very punchy" if comp > 60 else "punchy" if comp > 25 else "smooth" if comp > 8 else "natural"
    room = "room" if f["t60"] < 0.8 else "reverb"
    space = f"lots of {room}" if wet > -10 else f"some {room}" if wet > -17 else "dry"
    words = [w for w in (tone, dyn, space) if w]
    s = ", ".join(words)
    return s[0].upper() + s[1:]


def kmeans(X, k, seed=7, iters=100):
    rng = np.random.default_rng(seed)
    C = X[rng.choice(len(X), k, replace=False)]
    for _ in range(iters):
        lab = np.argmin(((X[:, None, :] - C[None]) ** 2).sum(-1), 1)
        C2 = np.array([X[lab == j].mean(0) if np.any(lab == j) else C[j] for j in range(k)])
        if np.allclose(C, C2):
            break
        C = C2
    return C, lab


def main():
    repo = Path(sys.argv[1])
    out = Path(sys.argv[2])
    cfg = yaml.safe_load(open(repo / "presets" / "fx_config.yaml"))

    presets = []
    for ds in ("internal", "medleydb"):
        info = json.load(open(repo / "presets" / ds / "info.json"))
        raw = np.load(repo / "presets" / ds / "raw_params.npy")
        for i, row in enumerate(raw):
            d = decode(row, info["params_keys"], info["params_original_shapes"], cfg)
            d["source"] = f"{ds}/{i}"
            presets.append(d)
    print(f"{len(presets)} presets decoded")
    if len(sys.argv) > 3:
        json.dump(presets, open(sys.argv[3], "w"), default=float)

    # features: the EQ curve at 8 points, compression, reverb time
    pts = [60, 150, 400, 1000, 2500, 5000, 9000, 14000]
    feats = []
    for d in presets:
        eqdb = 20 * np.log10(np.abs(response(d["eq"], pts, SR_FIT)))
        c = d["comp"]
        feats.append(np.concatenate([eqdb - eqdb.mean(), [3 * math.log(c["cmp_ratio"]), (c["cmp_th"] + 18) / 4, 4 * math.log(max(0.1, t60(d["reverb"])))]]))
    X = np.array(feats)
    X = (X - X.mean(0)) / (X.std(0) + 1e-9)
    C, lab = kmeans(X, N_PRESETS)
    # in each cluster, the preset nearest the centre that MusicDAWM can
    # reproduce: its EQ fitted on fil4 within 1.0 dB (the real plug-in may differ by
    # another 0.5 dB at the low end), its reverb within
    # the Space send's reach
    space_gain = float(sys.argv[4]) if len(sys.argv) > 4 else -4.1
    raw_vox = test_vocal(SR)
    test_gain = 10 ** ((-18 - lufs([raw_vox], SR)) / 20)
    psd_f = np.fft.rfftfreq(16384, 1 / SR)
    sel = (psd_f > 30) & (psd_f < 18000)
    seg0 = raw_vox[: (len(raw_vox) // 16384) * 16384].reshape(-1, 16384) * np.hanning(16384)
    P0 = np.mean(np.abs(np.fft.rfft(seg0, axis=1)) ** 2, 0)[sel]

    def wet_estimate(d):
        Pq = P0 * np.abs(response(d["eq"], psd_f[sel], SR)) ** 2
        return 10 * math.log10(np.sum(Pq * fdn_power(d["reverb"], psd_f[sel])) / (2 * np.sum(Pq)))

    chosen = []
    for j in range(N_PRESETS):
        idx = np.where(lab == j)[0]
        idx = idx[np.argsort(((X[idx] - C[j]) ** 2).sum(1))]
        for cand in idx:
            d = presets[int(cand)]
            if not (-24 + space_gain + 1 <= wet_estimate(d) <= space_gain):
                continue
            fp, err = fil4_settings(d["eq"])
            if err <= 1.0:
                d["fil4"] = fp
                d["fit_err"] = err
                chosen.append((int(cand), len(idx)))
                break
    chosen.sort(key=lambda x: -x[1])

    # the test vocal at -18 LUFS (DiffVox's input level), 48 kHz
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             "<!-- Generated by tools/diffvox/convert_presets.py from DiffVox (github.com/SonyResearch/diffvox, MIT, (c) 2025 Chin-Yun Yu). -->",
             f'<VocalPresets source="DiffVox" test-lufs="-18" test-gain="{test_gain:.6g}" sample-rate="48000">']
    used = set()
    for idx, size in chosen:
        d = presets[idx]
        f = describe(d)
        # DiffVox's chain on the test vocal: EQ, compressor (dry path)
        y = compexp(apply_eq(d["eq"], raw_vox * test_gain, SR), d["comp"], SR)
        ref_lufs = lufs([y, y], SR) - 3.0103  # centred mono in stereo, as FinalMix counts it
        ref_bands = bands(y, SR)
        # the reverb's energy relative to the dry path, weighted by the
        # vocal's spectrum
        seg = y[: (len(y) // 16384) * 16384].reshape(-1, 16384) * np.hanning(16384)
        P = np.mean(np.abs(np.fft.rfft(seg, axis=1)) ** 2, 0)
        sel = (psd_f > 30) & (psd_f < 18000)
        G = fdn_power(d["reverb"], psd_f[sel])
        wet_db = 10 * math.log10(np.sum(P[sel] * G) / (2 * np.sum(P[sel])))
        if not (-24 + space_gain + 0.5 <= wet_db <= space_gain + 1):
            print(f"  skipped {d['source']}: reverb {wet_db:.1f} dB out of the Space send's reach")
            continue
        name = name_for(f, wet_db)
        k = 2
        base = name
        while name in used:
            name = f"{base} {k}"
            k += 1
        used.add(name)
        eq_ref = 20 * np.log10(np.abs(response(d["eq"], THIRD_OCT, SR)))
        fp = d["fil4"]
        c = d["comp"]
        lines.append(f'  <Preset name="{name}" source="{d["source"]}" cluster-size="{size}">')
        lines.append("    <EQ " + " ".join(f'{k}="{v:.4g}"' for k, v in fp.items()) + "/>")
        lines.append(f'    <Compressor threshold-at-18="{c["cmp_th"]:.2f}" ratio="{min(20.0, c["cmp_ratio"]):.3f}" attack="{min(100.0, max(0.1, c["at_ms"])):.2f}" '
                     f'release="{min(2000.0, max(1.0, c["rt_ms"])):.1f}" makeup="{c["make_up"]:.2f}"/>')
        lines.append(f'    <Reverb wet-db="{wet_db:.2f}" t60="{f["t60"]:.2f}"/>')
        lines.append(f'    <Pan azimuth="{d["pan"]:.3f}"/>')
        lines.append(f'    <Reference lufs="{ref_lufs:.2f}" bands="{" ".join(f"{v:.2f}" for v in ref_bands)}" '
                     f'eq-db="{" ".join(f"{v:.2f}" for v in eq_ref)}"/>')
        lines.append("  </Preset>")
        print(f"{name:40s} {d['source']:14s} n={size:3d} eq-fit {d['fit_err']:.2f} dB wet {wet_db:6.1f} dB t60 {f['t60']:4.1f} s  ratio {c['cmp_ratio']:4.1f} th {c['cmp_th']:6.1f}  ref {ref_lufs:6.1f} LUFS")
    lines.append("</VocalPresets>")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()

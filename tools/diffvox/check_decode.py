#!/usr/bin/env python3
"""
MusicDAWM R5-light, CI only: check convert_presets.py's numpy decoding of
the DiffVox presets against DiffVox's own PyTorch model (its
parametrisations), for all 455 presets.

Usage: check_decode.py <diffvox-repo>
Needs torch, torchaudio, torchcomp, torch-fftconv, hydra-core, pyyaml.
"""

import json
import sys
from pathlib import Path

import numpy as np
import torch
import yaml
from hydra.utils import instantiate

repo = Path(sys.argv[1])
sys.path.insert(0, str(repo))
sys.path.insert(0, str(Path(__file__).parent))

import convert_presets as cp  # noqa: E402

cfg = yaml.safe_load(open(repo / "presets" / "fx_config.yaml"))
worst = 0.0
n = 0
for ds in ("internal", "medleydb"):
    info = json.load(open(repo / "presets" / ds / "info.json"))
    raw = np.load(repo / "presets" / ds / "raw_params.npy")
    model = instantiate(cfg["model"])
    keys, shapes = info["params_keys"], info["params_original_shapes"]
    for row in raw:
        state = model.state_dict()
        i = 0
        for k, sh in zip(keys, shapes):
            c = int(np.prod(sh)) if sh else 1
            state[k] = torch.tensor(row[i : i + c]).reshape(sh)
            i += c
        model.load_state_dict(state)
        mine = cp.decode(row, keys, shapes, cfg)
        with torch.no_grad():
            got = []
            exp = []
            for j in range(6):
                p = model[j].params
                got.append(float(p.freq))
                exp.append(mine["eq"][j]["freq"])
                if "gain" in p:
                    got.append(float(p.gain))
                    exp.append(mine["eq"][j]["gain"])
                if hasattr(p, "Q") and j not in (2, 3):
                    got.append(float(p.Q))
                    exp.append(mine["eq"][j]["Q"])
            c = model[6].params
            from torchcomp import coef2ms

            for name, v in (("cmp_th", float(c.cmp_th)), ("exp_th", float(c.exp_th)), ("make_up", float(c.make_up)),
                            ("cmp_ratio", float(c.cmp_ratio)), ("exp_ratio", float(c.exp_ratio)), ("avg_coef", float(c.avg_coef)),
                            ("at_ms", float(coef2ms(c.at, 44100))), ("rt_ms", float(coef2ms(c.rt, 44100))),
                            ("lookahead_ms", float(c.lookahead))):
                got.append(v)
                exp.append(mine["comp"][name])
            fdn = model[7].effects[1]
            got += fdn.params.gamma.reshape(-1).tolist()
            exp += mine["reverb"]["gamma"]
            got += fdn.params.U.reshape(-1).tolist()
            exp += cp.expm(np.array(mine["reverb"]["A_generator"])).reshape(-1).tolist()
            got.append(float(model[7].pan.params.pan))
            exp.append(mine["pan"])
        got, exp = np.array(got), np.array(exp)
        worst = max(worst, float(np.max(np.abs(got - exp) / (np.abs(exp) + 1e-3))))
        n += 1
print(f"{n} presets: largest relative difference {worst:.2e}")
sys.exit(0 if worst < 1e-3 else 1)

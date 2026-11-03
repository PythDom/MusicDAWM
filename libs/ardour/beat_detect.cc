/*
 * MusicDAWM: beat and bar detection (see ardour/beat_detect.h).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <algorithm>
#include <cmath>
#include <complex>

#include "ardour/audio_track.h"
#include "ardour/audioplaylist.h"
#include "ardour/audioregion.h"
#include "ardour/beat_detect.h"
#include "ardour/session.h"

using namespace ARDOUR;
using std::vector;

namespace {

int const    FRAME = 1024;
int const    HOP   = 128;
int const    NBANDS = 12;
double const TIGHTNESS = 100; /* RunBPM: regularity against onsets */
/* RunBPM: the onset detected at the centre of the analysis window comes
 * about 25 ms after the attack's start (measured on loops with known beats) */
double const ONSET_OFFSET = 0.025;

/* radix-2 FFT in place (RunBPM bpm.ts) */
void
fft (vector<double>& re, vector<double>& im)
{
	size_t const n = re.size ();
	for (size_t i = 1, j = 0; i < n; ++i) {
		size_t bit = n >> 1;
		for (; j & bit; bit >>= 1) {
			j ^= bit;
		}
		j ^= bit;
		if (i < j) {
			std::swap (re[i], re[j]);
			std::swap (im[i], im[j]);
		}
	}
	for (size_t len = 2; len <= n; len <<= 1) {
		double const ang = -2 * M_PI / len;
		double const wr  = cos (ang);
		double const wi  = sin (ang);
		for (size_t i = 0; i < n; i += len) {
			double cr = 1, ci = 0;
			for (size_t k = 0; k < len / 2; ++k) {
				size_t const a  = i + k;
				size_t const b  = a + len / 2;
				double const tr = re[b] * cr - im[b] * ci;
				double const ti = re[b] * ci + im[b] * cr;
				re[b]           = re[a] - tr;
				im[b]           = im[a] - ti;
				re[a] += tr;
				im[a] += ti;
				double const ncr = cr * wr - ci * wi;
				ci               = cr * wi + ci * wr;
				cr               = ncr;
			}
		}
	}
}

int
ds_factor (samplecnt_t sr)
{
	return std::max<int> (1, (int)(sr / 11025));
}

/* remove the slow trend (mean over ~0.5 s) and rectify */
vector<double>
detrend (vector<double> const& env, double fps)
{
	size_t const   n    = env.size ();
	int const      half = (int)std::lround (fps * 0.25);
	vector<double> prefix (n + 1, 0);
	for (size_t i = 0; i < n; ++i) {
		prefix[i + 1] = prefix[i] + env[i];
	}
	vector<double> out (n);
	for (size_t i = 0; i < n; ++i) {
		size_t const lo = i >= (size_t)half ? i - half : 0;
		size_t const hi = std::min (n, i + half + 1);
		double const v  = env[i] - (prefix[hi] - prefix[lo]) / (hi - lo);
		out[i]          = v > 0 ? v : 0;
	}
	return out;
}

/* the envelope of an already downsampled signal */
BeatDetect::Envelope
envelope_ds (float const* x, size_t n, double rate)
{
	BeatDetect::Envelope e;
	e.fps    = rate / HOP;
	e.frame0 = (FRAME / 2) / rate + ONSET_OFFSET;
	if (n < (size_t)FRAME) {
		return e;
	}
	size_t const frames = (n - FRAME) / HOP + 1;
	vector<double> win (FRAME);
	for (int i = 0; i < FRAME; ++i) {
		win[i] = 0.5 - 0.5 * cos (2 * M_PI * i / FRAME);
	}
	int const maxbin  = std::min (FRAME / 2, (int)std::lround (5000.0 / rate * FRAME));
	int const bassbin = std::max (2, (int)std::lround (200.0 / rate * FRAME));
	/* 12 log-spaced bands, 60 Hz .. 5 kHz */
	vector<int> edge (NBANDS + 1);
	for (int b = 0; b <= NBANDS; ++b) {
		double const f = 60.0 * pow (5000.0 / 60.0, b / double (NBANDS));
		edge[b]        = std::max (1, std::min (maxbin, (int)std::lround (f / rate * FRAME)));
	}
	vector<double> re (FRAME), im (FRAME), prev (maxbin, 0), cur (maxbin, 0);
	vector<double> flux (frames), bass (frames);
	e.bands.assign (frames, vector<float> (NBANDS, 0.f));
	for (size_t f = 0; f < frames; ++f) {
		size_t const off = f * HOP;
		for (int i = 0; i < FRAME; ++i) {
			re[i] = x[off + i] * win[i];
			im[i] = 0;
		}
		fft (re, im);
		double fl = 0, bs = 0;
		for (int k = 1; k < maxbin; ++k) {
			double const mag = std::hypot (re[k], im[k]);
			cur[k]           = std::log1p (100 * mag);
			if (f > 0) {
				double const d = cur[k] - prev[k];
				if (d > 0) {
					fl += d;
					if (k < bassbin) {
						bs += d;
					}
				}
			}
		}
		for (int b = 0; b < NBANDS; ++b) {
			double s = 0;
			for (int k = edge[b]; k < std::max (edge[b] + 1, edge[b + 1]); ++k) {
				s += cur[k];
			}
			e.bands[f][b] = (float)(s / std::max (1, edge[b + 1] - edge[b]));
		}
		flux[f] = fl;
		bass[f] = bs;
		std::swap (prev, cur);
	}
	e.onset = detrend (flux, e.fps);
	e.bass  = detrend (bass, e.fps);
	return e;
}

vector<double>
autocorrelation (vector<double> const& x, size_t maxlag)
{
	size_t const   n = x.size ();
	vector<double> ac (maxlag + 1, 0);
	for (size_t lag = 0; lag <= maxlag && lag < n; ++lag) {
		double s = 0;
		for (size_t i = 0; i + lag < n; ++i) {
			s += x[i] * x[i + lag];
		}
		ac[lag] = s / (n - lag);
	}
	return ac;
}

double
interp (vector<double> const& ac, double pos)
{
	size_t const i = (size_t)floor (pos);
	if (i + 1 >= ac.size ()) {
		return 0;
	}
	double const t = pos - i;
	return ac[i] * (1 - t) + ac[i + 1] * t;
}

/* strongest value of v within +-r frames of f */
double
peak_near (vector<double> const& v, int f, int r)
{
	double m = 0;
	for (int k = std::max (0, f - r); k <= std::min ((int)v.size () - 1, f + r); ++k) {
		m = std::max (m, v[k]);
	}
	return m;
}

void
zscore (vector<double>& v)
{
	if (v.empty ()) {
		return;
	}
	double m = 0, s = 0;
	for (double x : v) {
		m += x;
	}
	m /= v.size ();
	for (double x : v) {
		s += (x - m) * (x - m);
	}
	s = sqrt (s / v.size ());
	for (double& x : v) {
		x = s > 1e-12 ? (x - m) / s : 0;
	}
}

} // namespace

BeatDetect::Envelope
BeatDetect::envelope (float const* mono, samplecnt_t n, samplecnt_t sr)
{
	int const     factor = ds_factor (sr);
	vector<float> ds (n / factor);
	for (size_t i = 0; i < ds.size (); ++i) {
		double s = 0;
		for (int k = 0; k < factor; ++k) {
			s += mono[i * factor + k];
		}
		ds[i] = (float)(s / factor);
	}
	return envelope_ds (ds.data (), ds.size (), sr / double (factor));
}

double
BeatDetect::tempo (Envelope const& e, Options const& opt, double& other_bpm, double& confidence)
{
	other_bpm  = 0;
	confidence = 0;
	vector<double> const& env = e.onset;
	double const          fps = e.fps;
	if (env.size () < fps * 8) {
		return 0; /* at least 8 seconds */
	}
	double energy = 0;
	for (double v : env) {
		energy += v;
	}
	if (energy <= 1e-9) {
		return 0;
	}

	int const            COMB   = 4;
	size_t const         maxlag = std::min (env.size () - 1, (size_t)ceil (60 * fps / opt.min_bpm * COMB) + 2);
	vector<double> const ac     = autocorrelation (env, maxlag);

	double const   step = 0.05;
	vector<double> score;
	for (double bpm = opt.min_bpm; bpm <= opt.max_bpm + 1e-9; bpm += step) {
		double const lag = 60 * fps / bpm;
		double       s   = 0;
		for (int k = 1; k <= COMB; ++k) {
			s += interp (ac, k * lag);
		}
		score.push_back (s / COMB);
	}
	size_t best = 0;
	for (size_t i = 0; i < score.size (); ++i) {
		if (score[i] > score[best]) {
			best = i;
		}
	}
	if (score[best] <= 0) {
		return 0;
	}
	double const best_bpm = opt.min_bpm + best * step;
	auto         at       = [&] (double bpm) {
		long const idx = std::lround ((bpm - opt.min_bpm) / step);
		double     m   = 0;
		for (long d = -20; d <= 20; ++d) {
			if (idx + d >= 0 && idx + d < (long)score.size ()) {
				m = std::max (m, score[idx + d]);
			}
		}
		return m;
	};

	/* the octave: of the tempo, its double and its half that show a real
	 * pulse, the one closest to the user's taps, or to 110 per minute
	 * (RunBPM prefers 140, for running) */
	double const target = opt.prefer_bpm > 0 ? opt.prefer_bpm : 110;
	auto         dist   = [target] (double b) { return std::fabs (std::log2 (b / target)); };
	vector<double> cands { best_bpm };
	for (double c : { best_bpm * 2, best_bpm / 2 }) {
		if (c >= opt.min_bpm && c <= opt.max_bpm && at (c) >= 0.35 * score[best]) {
			cands.push_back (c);
		}
	}
	std::sort (cands.begin (), cands.end (), [&] (double a, double b) { return dist (a) < dist (b); });
	double bpm = cands[0];
	if (cands.size () > 1) {
		other_bpm = cands[1];
	}

	/* refine with a long comb (up to 16 beats): sub-frame precision */
	double const         beatlag  = 60 * fps / bpm;
	int const            longcomb = std::max (COMB, std::min (16, (int)floor (env.size () / 2.0 / beatlag)));
	vector<double> const acl      = autocorrelation (env, std::min (env.size () - 1, (size_t)ceil (beatlag * 1.02 * longcomb) + 2));
	double               refined = bpm, rs = -1e300;
	for (double b = bpm - 1; b <= bpm + 1; b += 0.01) {
		double const lag = 60 * fps / b;
		double       s   = 0;
		for (int k = 1; k <= longcomb; ++k) {
			s += interp (acl, k * lag);
		}
		if (s > rs) {
			rs      = s;
			refined = b;
		}
	}
	if (other_bpm > 0) {
		other_bpm *= refined / bpm;
	}

	/* confidence: normalised autocorrelation of the centred curve at the
	 * best lag between 0.3 and 1.5 s (about 0.05 for noise, > 0.8 for a
	 * clear pulse) */
	double mean = energy / env.size ();
	vector<double> c (env.size ());
	for (size_t i = 0; i < env.size (); ++i) {
		c[i] = env[i] - mean;
	}
	vector<double> const acc = autocorrelation (c, std::min (c.size () - 1, (size_t)ceil (1.5 * fps)));
	double               per = 0;
	for (size_t lag = (size_t)std::lround (0.3 * fps); lag < acc.size (); ++lag) {
		per = std::max (per, acc[lag] / std::max (acc[0], 1e-12));
	}
	confidence = std::max (0.0, std::min (1.0, (per - 0.1) / 0.6));
	return refined;
}

vector<int>
BeatDetect::track (Envelope const& e, double bpm)
{
	vector<int>  frames;
	size_t const n      = e.onset.size ();
	double const period = 60 * e.fps / bpm;
	if (!(bpm > 0) || n < period * 4) {
		return frames;
	}
	/* normalise so that the weight of regularity does not depend on the level */
	double mean = 0, var = 0;
	for (double v : e.onset) {
		mean += v;
	}
	mean /= n;
	for (double v : e.onset) {
		var += (v - mean) * (v - mean);
	}
	double const std_ = sqrt (var / n) > 0 ? sqrt (var / n) : 1;

	vector<double> score (n, 0);
	vector<int>    back (n, -1);
	int const      minlag = std::max (1, (int)std::lround (period / 2));
	int const      maxlag = (int)std::lround (period * 2);
	vector<double> cost (maxlag + 1, 0);
	for (int lag = minlag; lag <= maxlag; ++lag) {
		double const l = log (lag / period);
		cost[lag]      = -TIGHTNESS * l * l;
	}
	for (size_t t = 0; t < n; ++t) {
		double best = -1e300;
		int    arg  = -1;
		for (int lag = minlag; lag <= maxlag && (long)t - lag >= 0; ++lag) {
			double const s = score[t - lag] + cost[lag];
			if (s > best) {
				best = s;
				arg  = (int)t - lag;
			}
		}
		score[t] = e.onset[t] / std_ + (arg >= 0 ? best : 0);
		back[t]  = arg;
	}
	/* the last beat: the best score within the last period */
	size_t end = n - 1;
	for (size_t t = n > (size_t)period ? n - (size_t)std::lround (period) : 0; t < n; ++t) {
		if (score[t] > score[end]) {
			end = t;
		}
	}
	for (long t = (long)end; t >= 0; t = back[t]) {
		frames.push_back ((int)t);
	}
	std::reverse (frames.begin (), frames.end ());
	return frames;
}

BeatDetect::Result
BeatDetect::analyse (float const* mono, samplecnt_t n, samplecnt_t sr, Options const& opt, samplepos_t origin)
{
	Result r;
	if (!mono || n <= 0 || sr <= 0) {
		return r;
	}
	int const     factor = ds_factor (sr);
	vector<float> ds (n / factor);
	for (size_t i = 0; i < ds.size (); ++i) {
		double s = 0;
		for (int k = 0; k < factor; ++k) {
			s += mono[i * factor + k];
		}
		ds[i] = (float)(s / factor);
	}
	Envelope const e = envelope_ds (ds.data (), ds.size (), sr / double (factor));

	double const bpm = tempo (e, opt, r.other_bpm, r.confidence);
	if (!(bpm > 0)) {
		return r;
	}
	vector<int> const frames = track (e, bpm);
	if (frames.size () < 4) {
		return r;
	}
	/* the first beats may fall before the music (an intro): keep only
	 * beats from the first clear onset on */
	vector<double> strength;
	for (int f : frames) {
		strength.push_back (peak_near (e.onset, f, 2));
	}
	vector<double> sorted = strength;
	std::sort (sorted.begin (), sorted.end ());
	double const median = sorted[sorted.size () / 2];
	size_t       first  = 0;
	while (first < frames.size () && strength[first] < 0.25 * median) {
		++first;
	}
	size_t last = frames.size ();
	while (last > first + 1 && strength[last - 1] < 0.25 * median) {
		--last;
	}
	vector<int> const fr (frames.begin () + first, frames.begin () + last);
	vector<double> const st (strength.begin () + first, strength.begin () + last);
	if (fr.size () < 4) {
		return r;
	}

	for (int f : fr) {
		double const t = e.frame0 + f / e.fps;
		r.beats.push_back (origin + (samplepos_t)std::llround (t * sr));
	}

	/* average tempo: regression of the beat times on their rank */
	{
		double       sx = 0, sy = 0, sxx = 0, sxy = 0;
		size_t const k  = r.beats.size ();
		for (size_t i = 0; i < k; ++i) {
			double const y = r.beats[i] / double (sr);
			sx += i;
			sy += y;
			sxx += double (i) * i;
			sxy += i * y;
		}
		double const slope = (k * sxy - sx * sy) / (k * sxx - sx * sx);
		r.bpm              = slope > 0 ? 60 / slope : bpm;
	}

	/* the first beat of the bar: per position in the bar, the bass accent
	 * and the change of sound between the beat and the one before it */
	int const bpb = std::max (1, opt.beats_per_bar);
	if (bpb > 1 && fr.size () >= (size_t)(2 * bpb)) {
		vector<double> bass, change;
		for (size_t i = 0; i < fr.size (); ++i) {
			bass.push_back (peak_near (e.bass, fr[i], 2));
			/* mean band energies over this beat and the previous one */
			auto mean_bands = [&] (int a, int b) {
				vector<double> m (NBANDS, 0);
				int            cnt = 0;
				for (int f = std::max (0, a); f < std::min ((int)e.bands.size (), b); ++f, ++cnt) {
					for (int k = 0; k < NBANDS; ++k) {
						m[k] += e.bands[f][k];
					}
				}
				for (double& v : m) {
					v /= std::max (1, cnt);
				}
				return m;
			};
			if (i == 0 || i + 1 >= fr.size ()) {
				change.push_back (0);
				continue;
			}
			vector<double> const a = mean_bands (fr[i - 1], fr[i]);
			vector<double> const b = mean_bands (fr[i], fr[i + 1]);
			double               d = 0;
			for (int k = 0; k < NBANDS; ++k) {
				d += std::fabs (b[k] - a[k]);
			}
			change.push_back (d);
		}
		zscore (bass);
		zscore (change);
		double best = -1e300, second = -1e300;
		for (int p = 0; p < bpb; ++p) {
			double s   = 0;
			int    cnt = 0;
			for (size_t i = p; i < fr.size (); i += bpb, ++cnt) {
				s += bass[i] + change[i];
			}
			s /= std::max (1, cnt);
			if (s > best) {
				second     = best;
				best       = s;
				r.downbeat = p;
			} else if (s > second) {
				second = s;
			}
		}
		r.downbeat_sure = best - second > 0.3;
	}

	/* unsure: two or more beats in a row with no clear onset */
	for (size_t i = 0; i < st.size ();) {
		if (st[i] >= 0.25 * median) {
			++i;
			continue;
		}
		size_t j = i;
		while (j < st.size () && st[j] < 0.25 * median) {
			++j;
		}
		if (j - i >= 2) {
			r.unsure.push_back (std::make_pair (r.beats[i], r.beats[j - 1]));
		}
		i = j;
	}

	r.ok = true;
	return r;
}

BeatDetect::Result
BeatDetect::analyse_tracks (Session& s, vector<std::shared_ptr<Track>> const& tracks, samplepos_t start, samplepos_t end, Options const& opt)
{
	if (end <= start) {
		return Result ();
	}
	samplecnt_t const sr     = s.sample_rate ();
	int const         factor = ds_factor (sr);
	samplecnt_t const len    = end - start;
	vector<float>     ds (len / factor, 0.f);

	samplecnt_t const chunk = 65536 * factor;
	vector<Sample>    buf (chunk), mixdown (chunk);
	vector<float>     gain (chunk);
	vector<float>     sum (chunk);

	for (auto const& t : tracks) {
		std::shared_ptr<AudioPlaylist> pl = std::dynamic_pointer_cast<AudioPlaylist> (t ? t->playlist () : std::shared_ptr<Playlist> ());
		if (!pl) {
			continue;
		}
		uint32_t nch = 1;
		std::shared_ptr<RegionList> const rl = pl->region_list (); /* a copy: keep it alive */
		for (auto const& r : *rl) {
			if (std::shared_ptr<AudioRegion> ar = std::dynamic_pointer_cast<AudioRegion> (r)) {
				nch = std::max (nch, ar->n_channels ());
			}
		}
		for (samplecnt_t off = 0; off + factor <= len; off += chunk) {
			samplecnt_t const cnt = std::min (chunk, len - off);
			std::fill (sum.begin (), sum.begin () + cnt, 0.f);
			for (uint32_t c = 0; c < nch; ++c) {
				std::fill (buf.begin (), buf.begin () + cnt, 0.f);
				pl->read (buf.data (), mixdown.data (), gain.data (), timepos_t (start + off), timecnt_t (cnt), c);
				for (samplecnt_t k = 0; k < cnt; ++k) {
					sum[k] += buf[k] / nch;
				}
			}
			for (samplecnt_t k = 0; k + factor <= cnt; k += factor) {
				size_t const i = (off + k) / factor;
				if (i >= ds.size ()) {
					break;
				}
				double a = 0;
				for (int q = 0; q < factor; ++q) {
					a += sum[k + q];
				}
				ds[i] += (float)(a / factor);
			}
		}
	}

	/* the mix is already at the low rate (analyse () then downsamples by 1);
	 * positions come back at that rate */
	samplecnt_t const low = sr / factor;
	Result            r   = analyse (ds.data (), ds.size (), low, opt, 0);
	auto              to_session = [&] (samplepos_t p) { return start + (samplepos_t)std::llround (p * double (sr) / low); };
	for (auto& b : r.beats) {
		b = to_session (b);
	}
	for (auto& u : r.unsure) {
		u.first  = to_session (u.first);
		u.second = to_session (u.second);
	}
	return r;
}

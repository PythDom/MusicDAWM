/*
 * MusicDAWM WP4: Tap Tempo Map (see ardour/tap_tempo.h).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <algorithm>
#include <cmath>

#include "pbd/error.h"

#include "ardour/audioregion.h"
#include "ardour/playlist.h"
#include "ardour/session.h"
#include "ardour/tap_tempo.h"
#include "ardour/track.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using namespace Temporal;
using std::vector;

namespace {

/* median of the intervals around interval i (window of up to 9) */
double
local_interval (vector<samplepos_t> const& t, size_t i)
{
	vector<double> d;
	size_t const   lo = i >= 4 ? i - 4 : 0;
	size_t const   hi = std::min (t.size () - 1, i + 5);
	for (size_t k = lo; k < hi; ++k) {
		d.push_back (double (t[k + 1] - t[k]));
	}
	std::nth_element (d.begin (), d.begin () + d.size () / 2, d.end ());
	return d[d.size () / 2];
}

} // namespace

vector<samplepos_t>
TapTempo::merge (vector<samplepos_t> const& old_taps, vector<samplepos_t> const& take)
{
	if (take.empty ()) {
		return old_taps;
	}
	samplepos_t const   from = take.front ();
	samplepos_t const   to   = take.back ();
	vector<samplepos_t> out;
	for (auto t : old_taps) {
		if (t < from || t > to) {
			out.push_back (t);
		}
	}
	out.insert (out.end (), take.begin (), take.end ());
	std::sort (out.begin (), out.end ());
	return out;
}

vector<samplepos_t>
TapTempo::clean (vector<samplepos_t> t, Options const& opt, Report& rep)
{
	std::sort (t.begin (), t.end ());
	t.erase (std::unique (t.begin (), t.end ()), t.end ());

	if (t.size () < 3) {
		rep.taps = t.size ();
		return t;
	}

	/* double taps: an interval much shorter than its neighbours */
	for (size_t i = 0; i + 1 < t.size () && t.size () > 3;) {
		double const m = local_interval (t, std::min (i, t.size () - 2));
		if (double (t[i + 1] - t[i]) < 0.5 * m) {
			/* keep the tap that fits the beat grid best */
			bool drop_second = true;
			if (i + 2 < t.size ()) {
				double const e1 = std::fabs (double (t[i + 2] - t[i]) - m);
				double const e2 = std::fabs (double (t[i + 2] - t[i + 1]) - m);
				drop_second     = e1 <= e2;
			}
			t.erase (t.begin () + (drop_second ? i + 1 : i));
			++rep.doubles_removed;
		} else {
			++i;
		}
	}

	/* missed taps: an interval of about n beats gets n-1 taps added */
	vector<samplepos_t> filled;
	filled.push_back (t[0]);
	for (size_t i = 0; i + 1 < t.size (); ++i) {
		double const m = local_interval (t, i);
		double const d = double (t[i + 1] - t[i]);
		int const    n = (int)std::lround (d / m);
		if (d > 1.5 * m && n >= 2) {
			for (int k = 1; k < n; ++k) {
				filled.push_back (t[i] + (samplepos_t)std::llround (d * k / n));
				++rep.missed_filled;
			}
		}
		filled.push_back (t[i + 1]);
	}
	t = filled;

	/* jitter: each tap moves onto a curve (2nd degree) fitted through its
	 * neighbours (7 taps); the curve follows tempo drift, which a straight
	 * line would lag behind */
	if (opt.smooth && t.size () >= 7) {
		vector<samplepos_t> s (t);
		for (size_t i = 0; i < t.size (); ++i) {
			size_t lo = i >= 3 ? i - 3 : 0;
			size_t hi = std::min (t.size () - 1, lo + 6);
			lo        = hi >= 6 ? hi - 6 : 0;
			/* least squares y = a + b x + c x^2, x relative to tap i */
			double m[3][4] = { { 0 } };
			for (size_t k = lo; k <= hi; ++k) {
				double const x    = double (k) - double (i);
				double const y    = double (t[k] - t[i]);
				double const p[3] = { 1, x, x * x };
				for (int r = 0; r < 3; ++r) {
					for (int c = 0; c < 3; ++c) {
						m[r][c] += p[r] * p[c];
					}
					m[r][3] += p[r] * y;
				}
			}
			/* Gauss-Jordan elimination */
			for (int c = 0; c < 3; ++c) {
				int piv = c;
				for (int r = c + 1; r < 3; ++r) {
					if (std::fabs (m[r][c]) > std::fabs (m[piv][c])) {
						piv = r;
					}
				}
				for (int k = 0; k < 4; ++k) {
					std::swap (m[c][k], m[piv][k]);
				}
				for (int r = 0; r < 3; ++r) {
					if (r != c) {
						double const f = m[r][c] / m[c][c];
						for (int k = 0; k < 4; ++k) {
							m[r][k] -= f * m[c][k];
						}
					}
				}
			}
			double const a = m[0][3] / m[0][0]; /* the curve at tap i */
			s[i]           = t[i] + (samplepos_t)std::llround (a);
		}
		t = s;
	}

	rep.taps = t.size ();
	return t;
}

vector<samplepos_t>
TapTempo::snap (vector<samplepos_t> const& taps, vector<samplepos_t> const& onsets, samplecnt_t max_distance, Report& rep)
{
	vector<samplepos_t> out;
	for (auto t : taps) {
		auto        it   = std::lower_bound (onsets.begin (), onsets.end (), t);
		samplepos_t best = t;
		samplecnt_t dist = max_distance + 1;
		if (it != onsets.end () && *it - t < dist) {
			best = *it;
			dist = *it - t;
		}
		if (it != onsets.begin () && t - *(it - 1) < dist) {
			best = *(it - 1);
			dist = t - *(it - 1);
		}
		if (dist <= max_distance && best != t) {
			++rep.snapped;
			out.push_back (best);
		} else {
			out.push_back (t);
		}
	}
	/* two taps snapped to the same onset: keep the original second one */
	for (size_t i = 1; i < out.size (); ++i) {
		if (out[i] <= out[i - 1]) {
			out[i] = taps[i];
			--rep.snapped;
		}
	}
	return out;
}

/* Energy-based onsets: a frame (128 samples) whose level jumps by more than
 * 9 dB above the average of the previous 20 ms, above -50 dBFS, at least
 * 50 ms after the previous onset; the onset is the first sample of the jump
 * that reaches a tenth of the following peak. Good for drum hits and plucked
 * notes, which is what the taps are snapped to. */
vector<samplepos_t>
TapTempo::detect_onsets (Sample const* x, samplecnt_t n, samplecnt_t sr)
{
	vector<samplepos_t> onsets;
	samplecnt_t const   frame = 128;
	samplecnt_t const   gap   = sr / 20;
	int const           hist  = std::max<int> (2, (int)(0.02 * sr / frame));
	vector<double>      lev;
	for (samplecnt_t f = 0; f + frame <= n; f += frame) {
		double e = 0;
		for (samplecnt_t k = 0; k < frame; ++k) {
			e += x[f + k] * x[f + k];
		}
		lev.push_back (10 * log10 (e / frame + 1e-12));
	}
	samplepos_t last = -gap;
	for (size_t i = hist; i < lev.size (); ++i) {
		double avg = 0;
		for (int k = 1; k <= hist; ++k) {
			avg += pow (10, lev[i - k] / 10);
		}
		avg = 10 * log10 (avg / hist + 1e-12);
		if (lev[i] < -50 || lev[i] - avg < 9) {
			continue;
		}
		samplepos_t const f0 = (samplepos_t)(i - 1) * frame;
		if (f0 - last < gap) {
			continue;
		}
		/* refine: first sample reaching a tenth of the peak of the next 2 frames */
		float       peak = 0;
		samplepos_t end  = std::min<samplepos_t> (n, f0 + 3 * frame);
		for (samplepos_t k = f0; k < end; ++k) {
			peak = std::max (peak, std::fabs (x[k]));
		}
		samplepos_t at = f0;
		for (samplepos_t k = f0; k < end; ++k) {
			if (std::fabs (x[k]) >= 0.1f * peak) {
				at = k;
				break;
			}
		}
		onsets.push_back (at);
		last = at;
	}
	return onsets;
}

vector<samplepos_t>
TapTempo::track_onsets (Session& s, std::shared_ptr<Track> track)
{
	vector<samplepos_t> onsets;
	if (!track || !track->playlist ()) {
		return onsets;
	}
	std::shared_ptr<RegionList> rl = track->playlist ()->region_list ();
	for (auto const& r : *rl) {
		std::shared_ptr<AudioRegion> ar = std::dynamic_pointer_cast<AudioRegion> (r);
		if (!ar || ar->muted ()) {
			continue;
		}
		samplecnt_t const n = ar->length_samples ();
		vector<Sample>    mix (n, 0.f);
		vector<Sample>    ch (n);
		for (uint32_t c = 0; c < ar->n_channels (); ++c) {
			if (ar->read (ch.data (), 0, n, c) != n) {
				continue;
			}
			for (samplecnt_t k = 0; k < n; ++k) {
				mix[k] += ch[k];
			}
		}
		for (auto p : detect_onsets (mix.data (), n, s.sample_rate ())) {
			onsets.push_back (ar->position_sample () + p);
		}
	}
	std::sort (onsets.begin (), onsets.end ());
	return onsets;
}

double
TapTempo::tempo_for (samplecnt_t interval, samplecnt_t sample_rate, Options const& opt)
{
	double const beats = opt.unit == Bars ? opt.divisions_per_bar : 1;
	return beats * 60.0 * sample_rate / double (interval);
}

bool
TapTempo::apply (TempoMap& map, vector<samplepos_t> const& taps, samplecnt_t sr, Options const& opt)
{
	if (taps.size () < 2) {
		return false;
	}

	/* start from a clean map: one tempo, one meter */
	{
		vector<MusicTimePoint const*> bt;
		for (auto const& p : map.bartimes ()) {
			bt.push_back (&p);
		}
		for (auto p : bt) {
			map.remove_bartime (*p, false);
		}
		vector<TempoPoint const*> tp;
		for (auto const& p : map.tempos ()) {
			if (!map.is_initial (p)) {
				tp.push_back (&p);
			}
		}
		for (auto p : tp) {
			map.remove_tempo (*p, false);
		}
		vector<Temporal::MeterPoint const*> mp;
		for (auto const& p : map.meters ()) {
			if (!map.is_initial (p)) {
				mp.push_back (&p);
			}
		}
		for (auto p : mp) {
			map.remove_meter (*p, false);
		}
	}

	Temporal::Meter const meter (opt.divisions_per_bar, opt.note_value);
	map.set_meter (meter, timepos_t (AudioTime));

	/* Before the first tap (count-in, silence): close to the first
	 * interval's tempo, but such that the first tap falls exactly on a
	 * whole beat, where a BBT marker can make it bar 1 (a BBT marker must
	 * be on a whole beat). */
	double const first = tempo_for (taps[1] - taps[0], sr, opt);
	if (taps[0] > 0) {
		double const beat_len = 60.0 * sr / first; /* samples */
		double const beats    = std::max (1.0, std::round (taps[0] / beat_len));
		double const pre      = beats * 60.0 * sr / double (taps[0]);
		map.set_tempo (Tempo (pre, opt.note_value), timepos_t (AudioTime));
		map.set_bartime (BBT_Time (1, 1, 0), timepos_t (taps[0]));
	} else {
		map.set_tempo (Tempo (first, opt.note_value), timepos_t (AudioTime));
	}

	for (size_t i = 0; i + 1 < taps.size (); ++i) {
		double const npm = tempo_for (taps[i + 1] - taps[i], sr, opt);
		map.set_tempo (Tempo (npm, opt.note_value), timepos_t (taps[i]));
	}
	return true;
}

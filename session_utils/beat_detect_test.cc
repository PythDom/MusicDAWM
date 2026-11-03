/*
 * MusicDAWM: beat and bar detection test, headless (see ardour/beat_detect.h).
 *
 * Synthetic songs in 4/4 with a two-beat pickup: kick on 1 and 3, snare on
 * 2 and 4, hi-hat on the eighths, a chord that changes on every bar. One at
 * a steady 120 per minute, one played "without a click" (96 -> 108) with a
 * two-bar break. Each must give the tempo, every beat to within a few
 * milliseconds, the first beat of the bar and the break as unsure; on
 * tracks (drums and chords on two tracks, mixed); and as a tempo map whose
 * bars fall on the real bars.
 *
 * Usage: <prefix>-beat_detect_test <session-dir>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <unistd.h>

#include <glibmm/miscutils.h>

#include "pbd/compose.h"

#include "temporal/tempo.h"

#include "ardour/audio_track.h"
#include "ardour/audioregion.h"
#include "ardour/audiosource.h"
#include "ardour/beat_detect.h"
#include "ardour/playlist.h"
#include "ardour/region_factory.h"
#include "ardour/session.h"
#include "ardour/source_factory.h"
#include "ardour/tap_tempo.h"

#include "common.h"

using namespace std;
using namespace ARDOUR;
using namespace Temporal;

static int failures = 0;

static void
check (bool ok, string const& what)
{
	printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str ());
	if (!ok) {
		++failures;
	}
}

static int const SR      = 48000;
static int const PICKUP  = 2; /* the music starts on beat 3 of a bar */

/* n beats from t0, the tempo drifting linearly from bpm0 to bpm1 */
static vector<double>
true_beats (double t0, int n, double bpm0, double bpm1)
{
	vector<double> b;
	double         t = t0;
	for (int i = 0; i < n; ++i) {
		b.push_back (t);
		t += 60.0 / (bpm0 + (bpm1 - bpm0) * i / (n - 1));
	}
	return b;
}

static bool
is_downbeat (int i)
{
	return (i + PICKUP) % 4 == 0;
}

/* drums and chords of the song; beats in [break0, break1) are silent but
 * for a quiet pad */
struct Song {
	vector<float> drums, chords;
};

static Song
make_song (vector<double> const& beats, double seconds, int break0 = -1, int break1 = -1)
{
	samplecnt_t const n = (samplecnt_t)(seconds * SR);
	Song              s;
	s.drums.assign (n, 0.f);
	s.chords.assign (n, 0.f);
	std::mt19937                      rng (7);
	std::uniform_real_distribution<float> noise (-1.f, 1.f);
	auto in_break = [&] (int i) { return i >= break0 && i < break1; };

	for (size_t i = 0; i < beats.size (); ++i) {
		int const         pos = (i + PICKUP) % 4;
		samplepos_t const p   = (samplepos_t)std::llround (beats[i] * SR);
		double const      len = (i + 1 < beats.size () ? beats[i + 1] - beats[i] : 0.5);
		if (!in_break (i)) {
			if (pos == 0 || pos == 2) { /* kick */
				for (int k = 0; k < SR * 0.15 && p + k < n; ++k) {
					double const f = 50 + 70 * exp (-k / (SR * 0.02));
					s.drums[p + k] += 0.8f * expf (-k / (SR * 0.05f)) * sinf (2 * M_PI * f * k / SR);
				}
			} else { /* snare */
				for (int k = 0; k < SR * 0.12 && p + k < n; ++k) {
					s.drums[p + k] += 0.45f * expf (-k / (SR * 0.03f)) * (noise (rng) + 0.5f * sinf (2 * M_PI * 190 * k / SR));
				}
			}
			for (int h = 0; h < 2; ++h) { /* hi-hat on the eighths */
				samplepos_t const q = p + (samplepos_t)(h * len / 2 * SR);
				for (int k = 0; k < SR * 0.03 && q + k < n; ++k) {
					s.drums[q + k] += 0.12f * expf (-k / (SR * 0.008f)) * noise (rng);
				}
			}
		}
		/* the chord of the bar, from its downbeat to the next one */
		if (is_downbeat (i) || i == 0) {
			static double const roots[] = { 110.0, 146.83, 130.81, 98.0 };
			int const           bar     = (i + PICKUP) / 4;
			double const        f       = roots[bar % 4];
			size_t              j       = i + 1;
			while (j < beats.size () && !is_downbeat (j)) {
				++j;
			}
			samplepos_t const e   = j < beats.size () ? (samplepos_t)std::llround (beats[j] * SR) : n;
			float const       amp = in_break (i) ? 0.02f : 0.12f;
			for (samplepos_t k = p; k < e && k < n; ++k) {
				double const t   = (k - p) / double (SR);
				float const  env = (float)std::min (1.0, t / 0.01) * expf (-(float)t / 1.5f);
				s.chords[k] += amp * env * (sinf (2 * M_PI * f * t) + 0.6f * sinf (2 * M_PI * f * 1.5 * t) + 0.4f * sinf (2 * M_PI * f * 2 * t));
			}
		}
	}
	return s;
}

/* each detected beat against the nearest true beat */
struct Match {
	double rms = 0, max = 0;
	int    matched = 0;
	bool   downbeat_right = false;
};

static Match
match (BeatDetect::Result const& r, vector<double> const& truth, int skip0 = -1, int skip1 = -1)
{
	Match m;
	double ss = 0;
	int    n  = 0;
	for (size_t i = 0; i < r.beats.size (); ++i) {
		double const t  = r.beats[i] / double (SR);
		auto         it = std::lower_bound (truth.begin (), truth.end (), t);
		size_t       k  = it - truth.begin ();
		if (k > 0 && (k == truth.size () || t - truth[k - 1] < truth[k] - t)) {
			--k;
		}
		if ((int)k >= skip0 && (int)k < skip1) {
			continue;
		}
		double const e = std::fabs (t - truth[k]);
		ss += e * e;
		m.max = std::max (m.max, e);
		++n;
		if ((int)i == r.downbeat) {
			m.downbeat_right = is_downbeat (k);
		}
	}
	m.matched = n;
	m.rms     = n ? sqrt (ss / n) : 1;
	return m;
}

static void
write_wav (string const& path, vector<float> const& x)
{
	FILE* f   = fopen (path.c_str (), "wb");
	auto  u32 = [f] (uint32_t v) { fwrite (&v, 4, 1, f); };
	auto  u16 = [f] (uint16_t v) { fwrite (&v, 2, 1, f); };
	fwrite ("RIFF", 1, 4, f);
	u32 (36 + x.size () * 4);
	fwrite ("WAVEfmt ", 1, 8, f);
	u32 (16);
	u16 (3);
	u16 (1);
	u32 (SR);
	u32 (SR * 4);
	u16 (4);
	u16 (32);
	fwrite ("data", 1, 4, f);
	u32 (x.size () * 4);
	fwrite (x.data (), 4, x.size (), f);
	fclose (f);
}

static std::shared_ptr<AudioTrack>
track_from (Session* s, string const& path, string const& name)
{
	std::shared_ptr<AudioSource> src = std::dynamic_pointer_cast<AudioSource> (
	    SourceFactory::createExternal (DataType::AUDIO, *s, path, 0, Source::Flag (0), true, false));
	PBD::PropertyList plist;
	plist.add (Properties::start, timepos_t (AudioTime));
	plist.add (Properties::length, src->length ());
	plist.add (Properties::name, name);
	plist.add (Properties::whole_file, true);
	plist.add (Properties::external, true);
	std::shared_ptr<Region>     whole = RegionFactory::create (src, plist);
	std::shared_ptr<Region>     r     = RegionFactory::create (whole, false);
	std::shared_ptr<AudioTrack> t     = s->new_audio_track (1, 2, 0, 1, name, PresentationInfo::max_order, Normal, true).front ();
	t->playlist ()->add_region (r, timepos_t (AudioTime));
	return t;
}

int
main (int argc, char* argv[])
{
	setvbuf (stdout, NULL, _IOLBF, 0);
	if (argc != 2) {
		fprintf (stderr, "usage: %s <session-dir>\n", argv[0]);
		return 2;
	}
	string const dir = argv[1];
	SessionUtils::init (false);
	BusProfile bp;
	bp.master_out_channels = 2;
	Session* s             = SessionUtils::create_session (dir, "beats", SR, &bp);
	if (!s) {
		return 1;
	}

	/* ---- 1. steady 120 per minute ---- */
	printf ("-- steady 120\n");
	{
		vector<double> const truth = true_beats (1.3, 82, 120, 120);
		Song const           song  = make_song (truth, truth.back () + 2);
		vector<float>        mix (song.drums.size ());
		for (size_t k = 0; k < mix.size (); ++k) {
			mix[k] = song.drums[k] + song.chords[k];
		}
		BeatDetect::Options opt;
		auto                t0 = std::chrono::steady_clock::now ();
		BeatDetect::Result  r  = BeatDetect::analyse (mix.data (), mix.size (), SR, opt);
		double const        ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now () - t0).count ();
		Match const         m  = match (r, truth);
		printf ("   %.2f per minute (or %.1f), confidence %.2f, %zu beats, error rms %.1f ms max %.1f ms, bar starts on beat %d, %.0f ms\n",
		        r.bpm, r.other_bpm, r.confidence, r.beats.size (), m.rms * 1000, m.max * 1000, r.downbeat + 1, ms);
		check (r.ok && std::fabs (r.bpm - 120) < 0.2, "tempo 120");
		check (r.confidence > 0.5, "a clear pulse");
		check (r.beats.size () >= 80 && r.beats.size () <= 83, "every beat found");
		check (m.rms < 0.008 && m.max < 0.02, "beats within a few ms");
		check (m.downbeat_right && r.downbeat_sure, "the first beat of the bar found (after a two-beat pickup), and sure");
		check (r.unsure.empty (), "nothing unsure");

		opt.prefer_bpm             = 62; /* the user tapped half time */
		BeatDetect::Result const h = BeatDetect::analyse (mix.data (), mix.size (), SR, opt);
		printf ("   tapped about 62: %.2f (or %.1f)\n", h.bpm, h.other_bpm);
		check (std::fabs (h.bpm - 60) < 0.2 && std::fabs (h.other_bpm - 120) < 1, "the user's taps choose the octave (60), 120 offered");
	}

	/* ---- 2. played without a click: 96 -> 108, a two-bar break ---- */
	printf ("-- drifting 96 -> 108, break in bars 13-14\n");
	vector<double> const truth = true_beats (2.3, 130, 96, 108);
	int const            br0   = 4 * 12 - PICKUP, br1 = br0 + 8; /* beats of bars 13 and 14 */
	Song const           song  = make_song (truth, truth.back () + 2, br0, br1);
	{
		vector<float> mix (song.drums.size ());
		for (size_t k = 0; k < mix.size (); ++k) {
			mix[k] = song.drums[k] + song.chords[k];
		}
		BeatDetect::Result const r = BeatDetect::analyse (mix.data (), mix.size (), SR, BeatDetect::Options ());
		Match const              m = match (r, truth, br0, br1);
		printf ("   %.2f per minute, confidence %.2f, %zu beats, error rms %.1f ms max %.1f ms (outside the break), bar starts on beat %d\n",
		        r.bpm, r.confidence, r.beats.size (), m.rms * 1000, m.max * 1000, r.downbeat + 1);
		for (auto const& u : r.unsure) {
			printf ("   unsure %.2f - %.2f s (break %.2f - %.2f s)\n", u.first / double (SR), u.second / double (SR), truth[br0], truth[br1 - 1]);
		}
		check (r.ok && r.bpm > 100 && r.bpm < 104, "average tempo (about 102)");
		check (m.rms < 0.010 && m.max < 0.03, "beats follow the drift");
		check (m.downbeat_right, "the first beat of the bar found");
		bool break_found = false;
		for (auto const& u : r.unsure) {
			break_found |= u.first / double (SR) >= truth[br0] - 0.1 && u.second / double (SR) <= truth[br1 - 1] + 0.1 && u.second > u.first;
		}
		check (break_found, "the break is reported as unsure");
	}

	/* ---- 3. on tracks, and as the tempo map ---- */
	printf ("-- on two tracks, then the tempo map\n");
	{
		write_wav (Glib::build_filename (dir, "drums.wav"), song.drums);
		write_wav (Glib::build_filename (dir, "chords.wav"), song.chords);
		vector<std::shared_ptr<Track>> tracks { track_from (s, Glib::build_filename (dir, "drums.wav"), "Drums"),
			                                track_from (s, Glib::build_filename (dir, "chords.wav"), "Chords") };
		BeatDetect::Result const r = BeatDetect::analyse_tracks (*s, tracks, 0, (samplepos_t)(song.drums.size ()), BeatDetect::Options ());
		Match const              m = match (r, truth, br0, br1);
		printf ("   %.2f per minute, %zu beats, error rms %.1f ms max %.1f ms, bar starts on beat %d\n", r.bpm, r.beats.size (), m.rms * 1000, m.max * 1000, r.downbeat + 1);
		check (r.ok && m.rms < 0.010 && m.max < 0.03 && m.downbeat_right, "the mix of the two tracks gives the same beats and bars");

		BeatDetect::Result const d = BeatDetect::analyse_tracks (*s, { tracks[0] }, 0, (samplepos_t)(song.drums.size ()), BeatDetect::Options ());
		Match const              md = match (d, truth, br0, br1);
		printf ("   drums alone: %.2f per minute, error rms %.1f ms\n", d.bpm, md.rms * 1000);
		check (d.ok && md.rms < 0.010, "the drum track alone gives the beats");

		/* the map: taps on every beat from the first downbeat */
		vector<samplepos_t> taps (r.beats.begin () + r.downbeat, r.beats.end ());
		TapTempo::Options   to;
		TempoMap::WritableSharedPtr map = TempoMap::write_copy ();
		check (TapTempo::apply (*map, taps, SR, to), "tempo map made from the beats");
		TempoMap::update (map);
		TempoMap::SharedPtr tm (TempoMap::use ());
		/* every bar of the grid against the real bar starts (outside the break) */
		double ss = 0, mx = 0;
		int    nb = 0;
		int    first_db = 0;
		while (!is_downbeat (first_db)) {
			++first_db;
		}
		for (int bar = 1; bar <= 30; ++bar) {
			int const k = first_db + 4 * (bar - 1);
			if (k >= (int)truth.size () || (k >= br0 && k < br1)) {
				continue;
			}
			/* bar 1 is on the first tap (a BBT marker); count quarters from there */
			Beats const  q0 = tm->quarters_at (timepos_t (taps.front ()));
			double const t  = timepos_t (q0 + Beats (4 * (bar - 1), 0)).samples () / double (SR);
			double const e = std::fabs (t - truth[k]);
			ss += e * e;
			mx = std::max (mx, e);
			++nb;
		}
		printf ("   bars 1-30: error rms %.1f ms, max %.1f ms\n", sqrt (ss / nb) * 1000, mx * 1000);
		check (nb > 20 && sqrt (ss / nb) < 0.010 && mx < 0.03, "the bars of the grid fall on the real bars");
	}

	s->save_state ("");
	printf ("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
	fflush (stdout);
	SessionUtils::unload_session (s);
	_exit (failures ? 1 : 0);
}

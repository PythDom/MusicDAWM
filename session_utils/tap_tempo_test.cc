/*
 * MusicDAWM WP4: Tap Tempo Map test, headless.
 *
 * A performance with a drifting tempo (96 -> 108 bpm) starting at 2.3 s;
 * simulated taps with timing jitter, two missed taps and a double tap. The
 * cleaned taps become the session's tempo map, which must put every beat
 * (and every bar, in 6/8) where it really is. A click track at the true
 * beats checks "snap to onsets".
 *
 * Usage: <prefix>-tap_tempo_test <session-dir>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <cmath>
#include <cstdio>
#include <iostream>
#include <random>
#include <unistd.h>

#include <glibmm/miscutils.h>

#include "pbd/compose.h"

#include "temporal/tempo.h"

#include "ardour/audio_track.h"
#include "ardour/audioregion.h"
#include "ardour/audiosource.h"
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

static int const SR = 48000;

/* the performance: n beats from t0, tempo drifting linearly from bpm0 to bpm1 */
static vector<double>
true_beats (double t0, int n, double bpm0, double bpm1)
{
	vector<double> b;
	double         t = t0;
	for (int i = 0; i < n; ++i) {
		b.push_back (t);
		double const bpm = bpm0 + (bpm1 - bpm0) * i / (n - 1);
		t += 60.0 / bpm;
	}
	return b;
}

static void
write_clicks (string const& path, vector<double> const& at, double seconds)
{
	samplecnt_t const n = (samplecnt_t)(seconds * SR);
	vector<float>     x (n, 0.f);
	for (double t : at) {
		samplepos_t const p = (samplepos_t)std::llround (t * SR);
		for (int k = 0; k < 480 && p + k < n; ++k) {
			x[p + k] = 0.8f * expf (-k / 60.f) * sinf (2.f * M_PI * 1000.f * k / SR);
		}
	}
	FILE* f   = fopen (path.c_str (), "wb");
	auto  u32 = [f] (uint32_t v) { fwrite (&v, 4, 1, f); };
	auto  u16 = [f] (uint16_t v) { fwrite (&v, 2, 1, f); };
	fwrite ("RIFF", 1, 4, f);
	u32 (36 + n * 4);
	fwrite ("WAVEfmt ", 1, 8, f);
	u32 (16);
	u16 (3);
	u16 (1);
	u32 (SR);
	u32 (SR * 4);
	u16 (4);
	u16 (32);
	fwrite ("data", 1, 4, f);
	u32 (n * 4);
	fwrite (x.data (), 4, n, f);
	fclose (f);
}

/* time (s) of quarter note q after the first tap, in the session's map */
static double
map_time (double first_tap, double quarters)
{
	TempoMap::SharedPtr tm (TempoMap::use ());
	Beats const         q0 = tm->quarters_at (timepos_t ((samplepos_t)std::llround (first_tap * SR)));
	Beats const         q  = q0 + Beats::from_double (quarters);
	return timepos_t (q).samples () / double (SR);
}

struct Err {
	double rms, max;
};

/* beat (or bar) k of the grid, counted from its bar 1, against the truth */
static Err
errors (vector<double> const& truth, double quarters_per_tap)
{
	TempoMap::SharedPtr tm (TempoMap::use ());
	double              s = 0, m = 0;
	/* bar 1 is the BBT marker, or 0 */
	samplepos_t b1 = 0;
	for (auto const& p : tm->bartimes ()) {
		b1 = superclock_to_samples (p.sclock (), SR);
	}
	for (size_t k = 0; k < truth.size (); ++k) {
		double const e = fabs (map_time (b1 / double (SR), k * quarters_per_tap) - truth[k]);
		s += e * e;
		m = max (m, e);
	}
	return { sqrt (s / truth.size ()) * 1000, m * 1000 };
}

static void
apply_map (vector<samplepos_t> const& taps, TapTempo::Options const& opt)
{
	TempoMap::WritableSharedPtr wm = TempoMap::write_copy ();
	TapTempo::apply (*wm, taps, SR, opt);
	TempoMap::update (wm);
}

int
main (int argc, char* argv[])
{
	setvbuf (stdout, NULL, _IOLBF, 0);
	if (argc != 2) {
		cerr << "usage: " << argv[0] << " <session-dir>\n";
		return 2;
	}
	string const dir = argv[1];

	SessionUtils::init (false);
	BusProfile bus_profile;
	bus_profile.master_out_channels = 2;
	Session* s = SessionUtils::create_session (dir, "taps", SR, &bus_profile);
	if (!s) {
		return 1;
	}

	std::mt19937                     rng (1);
	std::normal_distribution<double> jitter (0.0, 0.008); /* 8 ms */

	/* ---- taps on beats, 4/4 ---- */
	vector<double> const truth = true_beats (2.3, 64, 96, 108);
	vector<samplepos_t>  taps;
	for (size_t k = 0; k < truth.size (); ++k) {
		if (k == 20 || k == 41) {
			continue; /* missed */
		}
		taps.push_back ((samplepos_t)std::llround ((truth[k] + (k ? jitter (rng) : 0)) * SR));
		if (k == 30) {
			taps.push_back ((samplepos_t)std::llround ((truth[k] + 0.07) * SR)); /* double tap */
		}
	}
	TapTempo::Options opt;
	TapTempo::Report  rep;
	vector<samplepos_t> clean = TapTempo::clean (taps, opt, rep);
	printf ("   %zu taps -> %d (doubles removed %d, missed filled %d)\n", taps.size (), rep.taps, rep.doubles_removed, rep.missed_filled);
	check (rep.doubles_removed == 1 && rep.missed_filled == 2 && rep.taps == 64, "cleaning: the double tap is dropped, the 2 missed taps are filled");

	apply_map (clean, opt);
	{
		TempoMap::SharedPtr tm (TempoMap::use ());
		BBT_Time const      b = tm->bbt_at (timepos_t (clean[0]));
		printf ("   first tap at %.4f s: %s\n", clean[0] / double (SR), string_compose ("%1|%2|%3", b.bars, b.beats, b.ticks).c_str ());
		check (b.bars == 1 && b.beats == 1 && b.ticks == 0 && fabs (clean[0] / double (SR) - 2.3) < 0.005, "bar 1 starts on the first tap (2.3 s)");
		check (tm->n_tempos () >= 60, string_compose ("one tempo per tap (%1 tempos)", tm->n_tempos ()));
	}
	Err e = errors (truth, 1);
	printf ("   beat error: rms %.1f ms, max %.1f ms\n", e.rms, e.max);
	check (e.rms < 6 && e.max < 15, "the grid follows the performance: every beat within 15 ms (8 ms tap jitter)");

	/* re-tapping a passage replaces it */
	vector<samplepos_t> take;
	for (size_t k = 10; k <= 20; ++k) {
		take.push_back ((samplepos_t)std::llround (truth[k] * SR));
	}
	vector<samplepos_t> merged = TapTempo::merge (taps, take);
	bool                same   = true;
	for (auto t : merged) {
		same &= !(t > take.front () && t < take.back ()) || std::find (take.begin (), take.end (), t) != take.end ();
	}
	check (same, "re-tapping a passage replaces the taps there");

	/* ---- snap to onsets: a click track at the true beats, sloppy taps ---- */
	string const wav = Glib::build_filename (dir, "clicks.wav");
	write_clicks (wav, truth, truth.back () + 2);
	std::shared_ptr<AudioSource> src = std::dynamic_pointer_cast<AudioSource> (
	    SourceFactory::createExternal (DataType::AUDIO, *s, wav, 0, Source::Flag (0), true, false));
	PBD::PropertyList plist;
	plist.add (Properties::start, timepos_t (AudioTime));
	plist.add (Properties::length, src->length ());
	plist.add (Properties::name, string ("clicks"));
	plist.add (Properties::whole_file, true);
	plist.add (Properties::external, true);
	std::shared_ptr<Region>     whole = RegionFactory::create (src, plist);
	std::shared_ptr<Region>     r     = RegionFactory::create (whole, false);
	std::shared_ptr<AudioTrack> t     = s->new_audio_track (1, 2, 0, 1, "Drums", PresentationInfo::max_order, Normal, true).front ();
	t->playlist ()->add_region (r, timepos_t (AudioTime));

	vector<samplepos_t> onsets = TapTempo::track_onsets (*s, t);
	printf ("   %zu onsets found\n", onsets.size ());
	check (onsets.size () >= 60 && onsets.size () <= 68, "onsets of the drum track found");

	std::normal_distribution<double> sloppy (0.0, 0.02); /* 20 ms */
	vector<samplepos_t>              loose;
	for (size_t k = 0; k < truth.size (); ++k) {
		loose.push_back ((samplepos_t)std::llround ((truth[k] + (k ? sloppy (rng) : 0)) * SR));
	}
	TapTempo::Report  rep2;
	TapTempo::Options nosmooth;
	nosmooth.smooth           = false;
	vector<samplepos_t> sn    = TapTempo::snap (TapTempo::clean (loose, nosmooth, rep2), onsets, SR / 15, rep2);
	double              off   = 0;
	for (size_t k = 0; k < sn.size () && k < truth.size (); ++k) {
		off = max (off, fabs (sn[k] / double (SR) - truth[k]));
	}
	printf ("   %d taps snapped, largest distance to the true beat %.1f ms\n", rep2.snapped, off * 1000);
	apply_map (sn, nosmooth);
	Err e2 = errors (truth, 1);
	printf ("   beat error after snapping: rms %.1f ms, max %.1f ms\n", e2.rms, e2.max);
	check (rep2.snapped >= 60 && e2.max < 12, "snap to onsets: sloppy taps (20 ms) give a grid on the drum hits");

	/* ---- taps on bar starts, 6/8 ---- */
	vector<double> const bars = true_beats (1.0, 24, 40, 44); /* bar starts; 40-44 "bars per minute" */
	vector<samplepos_t>  btaps;
	for (size_t k = 0; k < bars.size (); ++k) {
		btaps.push_back ((samplepos_t)std::llround ((bars[k] + (k ? jitter (rng) : 0)) * SR));
	}
	TapTempo::Options o68;
	o68.unit              = TapTempo::Bars;
	o68.divisions_per_bar = 6;
	o68.note_value        = 8;
	TapTempo::Report r68;
	apply_map (TapTempo::clean (btaps, o68, r68), o68);
	Err e3 = errors (bars, 3); /* a 6/8 bar is 3 quarter notes */
	printf ("   bar error (6/8): rms %.1f ms, max %.1f ms\n", e3.rms, e3.max);
	{
		TempoMap::SharedPtr tm (TempoMap::use ());
		TempoMetric const   m = tm->metric_at (timepos_t ((samplepos_t)std::llround (5.0 * SR)));
		check (m.meter ().divisions_per_bar () == 6 && m.meter ().note_value () == 8, "taps on bar starts: the meter is 6/8");
		/* just after the tap, so that a few ms of smoothing do not matter */
		BBT_Time const b = tm->bbt_at (timepos_t ((samplepos_t)std::llround ((bars[5] + 0.02) * SR)));
		printf ("   6th bar tap at %d|%d|%d\n", b.bars, b.beats, b.ticks);
		check (b.bars == 6 && b.beats == 1, "the 6th tap is bar 6");
	}
	check (e3.rms < 6 && e3.max < 15, "taps on bar starts: every bar within 15 ms");

	s->save_state ("");
	printf ("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
	fflush (stdout);
	SessionUtils::unload_session (s);
	_exit (failures ? 1 : 0);
}

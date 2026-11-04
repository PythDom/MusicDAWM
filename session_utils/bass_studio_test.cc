/*
 * MusicDAWM Bass Studio test, headless (see ardour/bass_studio.h).
 *
 * The chord chart parser, the bass lines of the styles (pitches, rhythm,
 * approach notes, feel, a 3/4 bar, the kick drum), and in a session: the
 * bass track with its General MIDI bass, the line as an editable region,
 * a remake that replaces it, undo, the complexity following the Drum Studio
 * parts and the kicks of its grooves, and the preview region.
 *
 * Usage: <prefix>-bass_studio_test <session-dir> <groove-library | ->
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <unistd.h>

#include <glibmm/miscutils.h>
#include <glibmm/timer.h>

#include "pbd/compose.h"

#include "temporal/tempo.h"

#include "ardour/bass_studio.h"
#include "ardour/drum_studio.h"
#include "ardour/gm_sound.h"
#include "ardour/location.h"
#include "ardour/midi_model.h"
#include "ardour/midi_region.h"
#include "ardour/midi_track.h"
#include "ardour/playlist.h"
#include "ardour/plugin_insert.h"
#include "ardour/session.h"

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

static string
describe (vector<BassStudio::Note> const& n, size_t max = 12)
{
	string s;
	for (size_t i = 0; i < n.size () && i < max; ++i) {
		s += string_compose ("%1@%2 ", n[i].pitch, n[i].start);
	}
	return s;
}

static vector<BassStudio::Note>
line (string const& chart, string const& style, int cx, DrumStudio::Feel feel = DrumStudio::FeelNormal, double bpb = 4,
      vector<vector<double>> const& kicks = vector<vector<double>> ())
{
	vector<BassStudio::Bar> bars;
	string                  err;
	BassStudio::parse_chart (chart, bars, err);
	BassStudio::Options o;
	o.style      = style;
	o.complexity = cx;
	o.feel       = feel;
	return BassStudio::generate (bars, o, bpb, vector<int> (), kicks);
}

int
main (int argc, char* argv[])
{
	setvbuf (stdout, NULL, _IOLBF, 0);
	if (argc != 3) {
		fprintf (stderr, "usage: %s <session-dir> <groove-library>\n", argv[0]);
		return 2;
	}
	if (string (argv[2]) != "-") { /* "-": the bundle's groove library */
		Glib::setenv ("MUSICDAWM_DRUMSTUDIO_DIR", argv[2], true);
	}

	/* ---- 1. the chord chart ---- */
	printf ("-- chord chart\n");
	{
		vector<BassStudio::Bar> bars;
		string                  err;
		bool const ok = BassStudio::parse_chart ("[Verse] C G | Am F\nC/E Dm7,G7 % N.C. [Chorus] Bbmaj7 F#m7b5 Ebsus4 Cdim7 Gaug", bars, err);
		check (ok && bars.size () == 13, string_compose ("13 bars (%1) %2", bars.size (), err));
		if (bars.size () == 13) {
			check (bars[0].chords[0].root == 0 && bars[1].chords[0].root == 7 && bars[2].chords[0].root == 9 && bars[2].chords[0].third == 3,
			       "C, G, Am (minor third)");
			check (bars[4].chords[0].root == 0 && bars[4].chords[0].bass == 4, "C/E: the bass plays E");
			check (bars[5].chords.size () == 2 && bars[5].chords[0].root == 2 && bars[5].chords[1].root == 7, "Dm7,G7: two chords in a bar");
			check (bars[6].chords.size () == 2 && bars[6].chords[1].root == 7, "% repeats the bar before");
			check (bars[7].chords.empty (), "N.C.: a bar without bass");
			check (bars[8].chords[0].root == 10 && bars[8].chords[0].seventh == 11, "Bbmaj7");
			check (bars[9].chords[0].root == 6 && bars[9].chords[0].fifth == 6, "F#m7b5 (flat five)");
			check (bars[10].chords[0].root == 3 && bars[10].chords[0].third == 5, "Ebsus4");
			check (bars[11].chords[0].seventh == 9 && bars[12].chords[0].fifth == 8, "Cdim7, Gaug");
		}
		check (!BassStudio::parse_chart ("C H7 F", bars, err) && err.find ("H7") != string::npos && err.find ("bar 2") != string::npos,
		       string_compose ("a wrong chord is named with its bar (%1)", err));
		check (!BassStudio::parse_chart ("  ", bars, err), "an empty chart is refused");
	}

	/* ---- 2. the lines ---- */
	printf ("-- bass lines\n");
	{
		vector<BassStudio::Note> const r = line ("C G Am F", "rock8", 2);
		printf ("   rock 8ths: %s\n", describe (r).c_str ());
		bool eighths = r.size () == 32;
		for (size_t i = 0; eighths && i < r.size (); ++i) {
			eighths = std::fabs (r[i].start - i * 0.5) < 1e-9;
		}
		check (eighths, "rock eighths: 32 eighth notes over 4 bars");
		check (r.size () == 32 && r[0].pitch == 36 && r[8].pitch == 31 && r[16].pitch == 33 && r[24].pitch == 29,
		       "the roots in the bass register (C2, G1, A1, F1)");
		check (r.size () == 32 && r[0].velocity > r[1].velocity, "the downbeat is accented");
		check (r.size () == 32 && r[0].length < 0.5 && r[0].length > 0.4, "notes are separated (a little shorter than an eighth)");

		vector<BassStudio::Note> const w = line ("C F", "walking", 3);
		printf ("   walking: %s\n", describe (w).c_str ());
		check (w.size () == 8 && w[0].pitch == 36 && w[1].pitch == 40 && w[2].pitch == 43 && w[3].pitch == 28,
		       "walking: C E G, then E1 a semitone below the next root F1");

		vector<BassStudio::Note> const sl = line ("C/E", "root", 1);
		check (sl.size () == 1 && sl[0].pitch == 28 && std::fabs (sl[0].length - 3.68) < 0.01, "C/E: a whole note E1");

		vector<BassStudio::Note> const h = line ("C G Am F", "rock8", 1, DrumStudio::FeelHalf);
		printf ("   half-time: %s\n", describe (h).c_str ());
		check (h.size () == 8 && std::fabs (h[1].start - 2) < 1e-9 && h[2].pitch == 31, "half-time: a note every 2 beats, following the chords");
		vector<BassStudio::Note> const d = line ("C G", "rock8", 1, DrumStudio::FeelDouble);
		check (d.size () == 16 && std::fabs (d[1].start - 0.5) < 1e-9, "double-time: a note every half beat");

		vector<BassStudio::Note> const t = line ("C G", "rock8", 1, DrumStudio::FeelNormal, 3);
		check (t.size () == 6 && std::fabs (t[3].start - 3) < 1e-9 && t[3].pitch == 31, "3/4: three beats a bar");

		vector<BassStudio::Note> const k = line ("C G", "kick", 1, DrumStudio::FeelNormal, 4, { { 0, 1.5, 2.5 }, { 0, 2 } });
		check (k.size () == 5 && std::fabs (k[1].start - 1.5) < 1e-9 && k[3].pitch == 31, "follow the kick: a root on every kick");

		vector<BassStudio::Note> const nc = line ("C N.C. G", "rock8", 1);
		check (nc.size () == 8 && nc[3].start + nc[3].length <= 4.0 + 1e-9 && std::fabs (nc[4].start - 8) < 1e-9, "no bass in the N.C. bar");
	}

	/* ---- 3. in a session ---- */
	printf ("-- bass track\n");
	SessionUtils::init (false);
	BusProfile bp;
	bp.master_out_channels = 2;
	Session* s             = SessionUtils::create_session (argv[1], "Bass", 48000, &bp);
	if (!s) {
		return 1;
	}
	if (!GMSound::available ()) {
		bool const wanted = !Glib::getenv ("MUSICDAWM_SOUNDFONT").empty () || !Glib::getenv ("MUSICDAWM_REQUIRE_SOUNDFONT").empty ();
		printf ("   soundfont or Fluid Synth not found%s\n", wanted ? "" : " (set MUSICDAWM_SOUNDFONT to test)");
		check (!wanted, "the soundfont and Fluid Synth are found");
	} else {
		BassStudio::Options o;
		o.style        = "rock8";
		o.complexity   = 1;
		o.follow_drums = true;
		string     err;
		int const  n   = BassStudio::make_bass_line (*s, "C G Am F", 1, o, err);
		std::shared_ptr<MidiTrack> bt = BassStudio::find_bass_track (*s);
		check (n == 16 && bt, string_compose ("a bass track with the line (%1 notes) %2", n, err));
		std::shared_ptr<PluginInsert> pi = bt ? std::dynamic_pointer_cast<PluginInsert> (bt->the_instrument ()) : std::shared_ptr<PluginInsert> ();
		check (pi && pi->plugin ()->unique_id () == "urn:ardour:a-fluidsynth", "played by Fluid Synth");
		std::shared_ptr<RegionList> rl = bt ? bt->playlist ()->region_list () : std::shared_ptr<RegionList> ();
		std::shared_ptr<MidiRegion> mr = (rl && rl->size () == 1) ? std::dynamic_pointer_cast<MidiRegion> (rl->front ()) : std::shared_ptr<MidiRegion> ();
		check (mr && mr->name () == "Bass line" && mr->length ().beats () == Beats (16, 0), "one region \"Bass line\", 4 bars long");
		bool pc = false;
		if (mr) {
			for (auto const& c : mr->model ()->patch_changes ()) {
				pc |= c->program () == 33 && c->channel () == 0;
			}
			check (mr->model ()->notes ().size () == 16, string_compose ("its notes can be edited (%1 in the model)", mr->model ()->notes ().size ()));
		}
		check (pc, "the bass sound (Finger Bass) is chosen by a program change");

		o.complexity = 2;
		int const n2 = BassStudio::make_bass_line (*s, "C G Am F C", 1, o, err);
		rl           = bt->playlist ()->region_list ();
		check (n2 == 40 && rl->size () == 1 && rl->front ()->length ().beats () == Beats (20, 0), "a remake replaces the line (5 bars, eighths)");
		s->undo (1);
		rl = bt->playlist ()->region_list ();
		check (rl->size () == 1 && rl->front ()->length ().beats () == Beats (16, 0), "undo brings the first line back");

		string chart;
		int    bar;
		BassStudio::Options so;
		BassStudio::saved (*s, chart, bar, so);
		check (chart == "C G Am F C" && bar == 1 && so.complexity == 2, "the chart and options are saved with the session");

		/* the drums: verse 1 from bar 1, chorus 4 from bar 3 */
		std::shared_ptr<MidiTrack> drums = DrumStudio::create_drum_track (*s, "rock");
		check ((bool)drums, "a drum track");
		for (int w = 0; drums && w < 100 && !drums->triggerbox ()->trigger (DrumStudio::slot (DrumStudio::part_index ("chorus"), 4))->the_region (); ++w) {
			Glib::usleep (50000);
		}
		TempoMap::SharedPtr tmap (TempoMap::use ());
		auto at_bar = [&] (int b) { return timepos_t (tmap->quarters_at (BBT_Argument (BBT_Time (b, 1, 0)))); };
		s->locations ()->add (new Location (*s, at_bar (1), at_bar (1), "verse", Location::Flags (Location::IsMark | Location::IsCueMarker),
		                                    DrumStudio::slot (DrumStudio::part_index ("verse"), 1)));
		s->locations ()->add (new Location (*s, at_bar (3), at_bar (3), "chorus", Location::Flags (Location::IsMark | Location::IsCueMarker),
		                                    DrumStudio::slot (DrumStudio::part_index ("chorus"), 4)));
		vector<int>            cx;
		vector<vector<double>> kicks;
		BassStudio::drum_parts (*s, 1, 4, cx, kicks);
		printf ("   drum complexity by bar: %d %d %d %d; kicks in bar 1: %zu\n", cx[0], cx[1], cx[2], cx[3], kicks[0].size ());
		check (cx == vector<int> ({ 1, 1, 4, 4 }), "the complexity follows the drum parts (verse 1, chorus 4)");
		check (!kicks[0].empty () && kicks[0][0] < 1e-6, "the kicks of the drum grooves, bar by bar");

		std::shared_ptr<Region> pr = BassStudio::preview_region (*s, "C G Am F", 1, o, 2, err);
		std::shared_ptr<MidiRegion> pm = std::dynamic_pointer_cast<MidiRegion> (pr);
		check (pm && pm->length ().beats () == Beats (8, 0) && !pm->model ()->notes ().empty (), "a preview region of the first 2 bars");
		pm.reset ();
		pr.reset ();
		mr.reset ();
		rl.reset ();
		pi.reset ();
		bt.reset ();
		drums.reset ();
	}

	printf ("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
	fflush (stdout);
	SessionUtils::unload_session (s);
	_exit (failures ? 1 : 0);
}

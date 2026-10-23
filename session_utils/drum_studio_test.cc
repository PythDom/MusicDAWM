/*
 * MusicDAWM Drum Studio test (WP3), headless, in real time with the dummy backend.
 *
 * 1. the groove library: 12+ styles, every cell present
 * 2. "Create drum track" with the rock style: 28 slots, right lengths, hidden
 *    from the Cue page
 * 3. perform intro -> verse -> fill -> chorus -> ending live (switches quantized
 *    to the bar, fill returns to the verse by itself) with Rec Cues armed
 * 4. the recorded cue markers sit on the expected bars
 * 5. replay: the drum track plays the same cells at the same bars
 * 6. drag one marker one bar later, flatten to MIDI regions, delete one kick
 *    in one region: the other regions keep theirs
 * 7. save, reopen (in a second run of this program): regions, markers and
 *    slots are still there
 *
 * Built with the other session utilities as <prefix>-drum_studio_test.
 * Usage: <prefix>-drum_studio_test <session-dir> <groove-library-dir | ->
 * ("-" uses the installed library; MUSICDAWM_REQUIRE_KIT=1 also requires the
 * bundled AVL kits)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <cinttypes>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <iostream>
#include <map>

#include <glibmm/miscutils.h>
#include <glibmm/timer.h>

#include "pbd/stateful_diff_command.h"

#include "temporal/tempo.h"

#include "ardour/drum_studio.h"
#include "ardour/location.h"
#include "ardour/midi_model.h"
#include "ardour/midi_region.h"
#include "ardour/midi_track.h"
#include "ardour/playlist.h"
#include "ardour/session.h"
#include "ardour/triggerbox.h"

#include "common.h"

using namespace std;
using namespace ARDOUR;
using namespace Temporal;

static int failures = 0;
static int reopen (char const*, char const*, size_t, size_t);

static void
check (bool ok, string const& what)
{
	printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str ());
	if (!ok) {
		++failures;
	}
}

/* start of bar n (1-based), in samples */
static samplepos_t
bar (int n)
{
	return TempoMap::use ()->sample_at (BBT_Argument (BBT_Time (n, 1, 0)));
}

static void
wait_rolling (Session* s, bool yn)
{
	for (int i = 0; i < 300 && s->transport_rolling () != yn; ++i) {
		Glib::usleep (10000);
	}
}

static int
cell (string const& part, int complexity)
{
	return DrumStudio::slot (DrumStudio::part_index (part), complexity);
}

/* cue markers, by position */
static map<samplepos_t, int>
cue_markers (Session* s)
{
	map<samplepos_t, int> m;
	for (auto const& l : s->locations ()->list ()) {
		if (l->is_cue_marker ()) {
			m[l->start_sample ()] = l->cue_id ();
		}
	}
	return m;
}

static string
describe (map<samplepos_t, int> const& m)
{
	string out;
	for (auto const& [pos, cue] : m) {
		int p, c;
		DrumStudio::cell_of_slot (cue, p, c);
		char buf[64];
		snprintf (buf, sizeof (buf), "%s%.2fs:%s%d", out.empty () ? "" : " ", pos / 48000.,
		          (p >= 0 && p < (int)DrumStudio::parts ().size ()) ? DrumStudio::parts ()[p].key.c_str () : "?", c);
		out += buf;
	}
	return out;
}

/* roll from 0 to `until`, ticking the performer; `actions` maps a time (s) to
 * a cell; a negative time means: request it while stopped (it starts the transport) */
static void
perform (Session* s, DrumPerformer& perf, map<double, pair<string, int>> const& actions, double until)
{
	s->request_locate (0, false, MustStop);
	Glib::usleep (300000);
	auto next = actions.begin ();
	for (; next != actions.end () && next->first < 0; ++next) {
		perf.request (DrumStudio::part_index (next->second.first), next->second.second);
	}
	s->request_roll ();
	wait_rolling (s, true);
	while (s->transport_sample () < until * s->sample_rate ()) {
		double const t = s->transport_sample () / (double)s->sample_rate ();
		if (next != actions.end () && t >= next->first) {
			perf.request (DrumStudio::part_index (next->second.first), next->second.second);
			++next;
		}
		perf.tick ();
		Glib::usleep (20000);
	}
	s->request_stop ();
	wait_rolling (s, false);
	Glib::usleep (500000); /* cue recording is flushed after the stop */
}

static size_t
count_notes (std::shared_ptr<MidiRegion> r, uint8_t note)
{
	size_t n = 0;
	std::shared_ptr<MidiModel> m = r->model ();
	for (auto const& nt : m->notes ()) {
		if (nt->note () == note && nt->time () >= r->start ().beats () && nt->time () < r->start ().beats () + r->length ().beats ()) {
			++n;
		}
	}
	return n;
}

int
main (int argc, char* argv[])
{
	setvbuf (stdout, NULL, _IOLBF, 0);
	if (argc == 6 && string (argv[1]) == "--reopen") {
		return reopen (argv[2], argv[3], atoi (argv[4]), atoi (argv[5]));
	}
	if (argc < 3) {
		cerr << "usage: " << argv[0] << " <session-dir> <groove-library-dir | ->\n"
		     << "  -: use the installed library (data search path)\n";
		return 2;
	}
	if (string (argv[2]) != "-") {
		Glib::setenv ("MUSICDAWM_DRUMSTUDIO_DIR", argv[2], true);
	}

	SessionUtils::init (false);
	BusProfile bus_profile;
	bus_profile.master_out_channels = 2;
	Session* s = SessionUtils::create_session (argv[1], "Drums", 48000, &bus_profile);
	if (!s) {
		return 1;
	}

	/* 1. library */
	printf ("-- library\n");
	check (DrumStudio::parts ().size () == 7, "7 song parts");
	check (DrumStudio::styles ().size () >= 12, "at least 12 styles");
	bool complete = true;
	for (auto const& st : DrumStudio::styles ()) {
		for (auto const& p : DrumStudio::parts ()) {
			for (int c = 1; c <= DrumStudio::complexities; ++c) {
				complete = complete && st.groove (p.key, c);
			}
		}
	}
	check (complete, "every style has every part in 4 complexities");

	/* 2. create the drum track */
	printf ("-- create drum track\n");
	std::shared_ptr<MidiTrack> track = DrumStudio::create_drum_track (*s, "rock");
	check ((bool)track, "drum track created");
	if (!track) {
		return 1;
	}
	check (DrumStudio::is_drum_track (track) && DrumStudio::style_of (track) == "rock", "marked as a rock drum track");
	check (!track->presentation_info ().trigger_track (), "hidden from the Cue page");
	std::shared_ptr<TriggerBox> tb = track->triggerbox ();
	Glib::usleep (500000); /* regions are set in the trigger thread */
	int filled = 0;
	for (int n = 0; n < DrumStudio::slot_count (); ++n) {
		filled += (tb->trigger (n) && tb->trigger (n)->the_region ()) ? 1 : 0;
	}
	check (filled == 28, string_compose ("28 slots loaded (%1)", filled));
	std::shared_ptr<Region> verse = tb->trigger (cell ("verse", 2))->the_region ();
	std::shared_ptr<Region> fill  = tb->trigger (cell ("fill", 3))->the_region ();
	check (verse && verse->length ().beats () == Beats (8, 0), "verse groove is 2 bars");
	check (fill && fill->length ().beats () == Beats (4, 0), "fill is 1 bar");
	if (getenv ("MUSICDAWM_REQUIRE_KIT")) {
		/* set by the Windows workflow, which bundles the kit */
		check (DrumStudio::bundled_kits ().size () >= 4, string_compose ("the AVL kits are installed (%1)", DrumStudio::bundled_kits ().size ()));
	}
	check (DrumStudio::bundled_kits ().empty () || track->the_instrument (), "plays through the bundled kit when it is installed");

	/* 3. perform live, Rec Cues armed */
	printf ("-- perform (real time, about 21 s)\n");
	DrumPerformer perf (*s);
	perf.set_quantize (DrumStudio::QuantizeBar);
	perf.set_recording (true);
	/* at 120 bpm 4/4 a bar is 2 s; each request comes mid-bar */
	map<double, pair<string, int>> const show = {
		{ -1, { "intro", 2 } },    /* while stopped -> bar 1 */
		{ 2.5, { "verse", 2 } },   /* -> bar 3 */
		{ 6.5, { "fill", 3 } },    /* -> bar 5, back to the verse at bar 6 */
		{ 12.5, { "chorus", 2 } }, /* -> bar 8 */
		{ 16.5, { "ending", 1 } }, /* -> bar 10 */
	};
	perform (s, perf, show, 21.0);
	perf.set_recording (false);

	/* 4. markers */
	map<samplepos_t, int> const got = cue_markers (s);
	map<samplepos_t, int> const want = {
		{ bar (1), cell ("intro", 2) }, { bar (3), cell ("verse", 2) }, { bar (5), cell ("fill", 3) },
		{ bar (6), cell ("verse", 2) }, { bar (8), cell ("chorus", 2) }, { bar (10), cell ("ending", 1) },
	};
	printf ("   recorded: %s\n   expected: %s\n", describe (got).c_str (), describe (want).c_str ());
	check (got == want, "switches recorded as cue markers on the right bars, fill returned to the verse");

	/* 5. replay: watch which slot plays */
	printf ("-- replay\n");
	perf.reset ();
	s->request_locate (0, false, MustStop);
	Glib::usleep (300000);
	s->request_roll ();
	wait_rolling (s, true);
	map<samplepos_t, int> heard;
	int last = -1;
	while (s->transport_sample () < 21 * 48000) {
		TriggerPtr t = tb->currently_playing ();
		int const now = t ? (int)t->index () : -1;
		if (now != last && now >= 0) {
			heard[s->transport_sample ()] = now;
		}
		last = now;
		Glib::usleep (5000);
	}
	s->request_stop ();
	wait_rolling (s, false);
	/* each change must be seen within 100 ms after its marker */
	bool same = heard.size () == want.size ();
	auto h = heard.begin ();
	for (auto const& [pos, cue] : want) {
		if (h == heard.end ()) {
			same = false;
			break;
		}
		same = same && h->second == cue && h->first >= pos - 2400 && h->first < pos + 4800;
		++h;
	}
	printf ("   heard:    %s\n", describe (heard).c_str ());
	check (same, "replay plays the same cells at the same bars");

	/* 6. drag, flatten, edit */
	printf ("-- drag, flatten, edit\n");
	for (auto const& l : s->locations ()->list ()) {
		if (l->is_cue_marker () && l->start_sample () == bar (8)) {
			l->set_start (timepos_t (TempoMap::use ()->quarters_at (BBT_Argument (BBT_Time (9, 1, 0)))), false);
		}
	}
	check (cue_markers (s).count (bar (9)) == 1, "chorus marker dragged from bar 8 to bar 9");

	int const made = DrumStudio::flatten (*s, track, timepos_t (), timepos_t (s->sample_rate () * 30));
	std::shared_ptr<RegionList> rl = track->playlist ()->region_list ();
	printf ("   %d regions:", made);
	for (auto const& r : *rl) {
		printf (" %s@%.1fs", r->name ().c_str (), r->position ().samples () / 48000.);
	}
	printf ("\n");
	/* intro 2-3 (1 region), verse 3-5 (1 region of 2 bars), fill 5-6, verse 6-9
	 * (2 bars + 1 bar), chorus 9-10, ending from 10 (once) */
	check (made == 7 && (int)rl->size () == 7, "flatten made 7 regions");
	check (tb->trigger (cell ("verse", 2))->cue_isolated (), "slots no longer follow cues after flatten");
	/* a track with clips would otherwise not play its regions (triggerbox-overrides-disk-monitoring) */
	check (track->monitoring_state () & MonitoringDisk, "the drum track plays its regions");

	check (rl->front ()->name () == "Rock Intro 2", string_compose ("regions are named by style, part and complexity (%1)", rl->front ()->name ()));
	std::shared_ptr<MidiRegion> first, second;
	for (auto const& r : *rl) {
		if (r->name () == "Rock Verse 2") {
			(first ? second : first) = std::dynamic_pointer_cast<MidiRegion> (r);
		}
	}
	check (first && second, "two verse regions");
	if (first && second) {
		size_t const kicks_before = count_notes (second, 36);
		std::shared_ptr<MidiModel> m = first->model ();
		MidiModel::NoteDiffCommand* cmd = m->new_note_diff_command ("delete one kick");
		for (auto const& nt : m->notes ()) {
			if (nt->note () == 36) {
				cmd->remove (nt);
				break;
			}
		}
		m->apply_diff_command_as_commit (*s, cmd);
		check (count_notes (second, 36) == kicks_before && kicks_before > 0, "deleting a kick in one region leaves the other regions alone");
	}

	/* 7. save; a second run of this program reopens the session. (Loading a
	 * second session in the same process can hang the dummy backend's
	 * threads at teardown; see reports/WP3.md.) */
	s->save_state ("");
	size_t const regions = rl->size ();
	/* drop our references before the session (and the trigger worker) go away */
	first.reset (); second.reset (); verse.reset (); fill.reset ();
	rl.reset (); tb.reset (); track.reset ();
	SessionUtils::unload_session (s);
	SessionUtils::cleanup ();

	string cmd = string_compose ("\"%1\" --reopen \"%2\" \"%3\" %4 %5", argv[0], argv[1], argv[2], regions, want.size ());
#ifdef PLATFORM_WINDOWS
	/* cmd.exe drops the first and last quote of the whole line */
	cmd = "\"" + cmd + "\"";
#endif
	int const reopened = system (cmd.c_str ());
	check (reopened == 0, "after reopening: drum track, regions and cue markers are there");

	printf ("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
	return failures ? 1 : 0;
}

static int
reopen (char const* dir, char const* lib, size_t regions, size_t markers)
{
	if (string (lib) != "-") {
		Glib::setenv ("MUSICDAWM_DRUMSTUDIO_DIR", lib, true);
	}
	SessionUtils::init (false);
	Session* s = SessionUtils::load_session (dir, "Drums");
	std::shared_ptr<MidiTrack> again = DrumStudio::find_drum_track (*s);
	check (again && DrumStudio::style_of (again) == "rock", "reopened: the drum track is still a rock drum track");
	check (again && again->playlist ()->region_list ()->size () == regions, "reopened: its regions are there");
	check (cue_markers (s).size () == markers, "reopened: the cue markers are there");
	check (again && again->triggerbox ()->trigger (27) && again->triggerbox ()->trigger (27)->the_region (), "reopened: all 28 slots are there");
	again.reset ();
	SessionUtils::unload_session (s);
	/* SessionUtils::cleanup () cancels the remaining threads; after loading a
	 * session with triggers one of them does not take cancellation cleanly
	 * ("exception not rethrown"). The session is closed, so just exit. */
	fflush (stdout);
	_exit (failures ? 1 : 0);
}

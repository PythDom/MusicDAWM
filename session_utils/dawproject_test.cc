/*
 * MusicDAWM DAWproject import test (WP5), headless.
 *
 * Imports the file written by tools/dawproject/make_test_project.py into a
 * new session and checks what came across; see that script for the content.
 *
 * Built with the other session utilities as <prefix>-dawproject_test.
 * Usage: <prefix>-dawproject_test <session-dir> <file.dawproject>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <cmath>
#include <cstdio>
#include <iostream>
#include <unistd.h>

#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include "pbd/compose.h"

#include "temporal/tempo.h"

#include "ardour/audio_port.h"
#include "ardour/audio_track.h"
#include "ardour/audioregion.h"
#include "ardour/automation_list.h"
#include "ardour/dB.h"
#include "ardour/dawproject_import.h"
#include "ardour/gain_control.h"
#include "ardour/io.h"
#include "ardour/location.h"
#include "ardour/midi_model.h"
#include "ardour/midi_region.h"
#include "ardour/midi_track.h"
#include "ardour/mute_control.h"
#include "ardour/pannable.h"
#include "ardour/playlist.h"
#include "ardour/send.h"
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

static bool
near (double a, double b, double tol)
{
	return fabs (a - b) <= tol;
}

/* session position of a beat (1 beat = a quarter note) */
static samplepos_t
beat (double b)
{
	return timepos_t (Beats::from_double (b)).samples ();
}

static vector<std::shared_ptr<Region>>
regions (std::shared_ptr<Route> r)
{
	vector<std::shared_ptr<Region>> v;
	std::shared_ptr<Track>          t = std::dynamic_pointer_cast<Track> (r);
	if (t) {
		std::shared_ptr<RegionList> rl = t->playlist ()->region_list ();
		v.assign (rl->begin (), rl->end ());
		sort (v.begin (), v.end (), [] (std::shared_ptr<Region> a, std::shared_ptr<Region> b) { return a->position () < b->position (); });
	}
	return v;
}

int
main (int argc, char* argv[])
{
	setvbuf (stdout, NULL, _IOLBF, 0);
	if (argc < 3) {
		cerr << "usage: " << argv[0] << " <session-dir> <file.dawproject>\n";
		return 2;
	}

	SessionUtils::init (false);
	BusProfile bus_profile;
	bus_profile.master_out_channels = 2;
	Session* s = SessionUtils::create_session (argv[1], "Imported", 48000, &bus_profile);
	if (!s) {
		return 1;
	}
	samplecnt_t const sr = s->sample_rate ();

	DAWProjectImport::Result const res = DAWProjectImport::import (*s, argv[2]);
	printf ("imported from %s: %d tracks, %d busses, %d regions, %d markers\n", res.application.c_str (), res.tracks, res.busses, res.regions, res.markers);
	for (auto const& n : res.notes) {
		printf ("   note: %s\n", n.c_str ());
	}
	check (res.ok, "import succeeded");
	check (res.application == "MusicDAWM test 1", "exporting application read");
	check (res.tracks == 4 && res.busses == 2, "4 tracks and 2 busses (folder flattened)");

	/* tempo map */
	TempoMap::SharedPtr tmap (TempoMap::use ());
	check (near (tmap->tempo_at (timepos_t (Beats ())).note_types_per_minute (), 120, 0.01), "tempo 120 at the start");
	check (near (tmap->tempo_at (timepos_t (Beats::from_double (40))).note_types_per_minute (), 90, 0.01), "tempo 90 from beat 32");
	Meter const& m9 (tmap->meter_at (timepos_t (Beats::from_double (33))));
	check (m9.divisions_per_bar () == 3 && m9.note_value () == 4, "3/4 from bar 9");

	/* Guitar */
	std::shared_ptr<Route> g = s->route_by_name ("Guitar");
	check ((bool) g, "track Guitar");
	if (g) {
		vector<std::shared_ptr<Region>> rs = regions (g);
		check (rs.size () == 1, "Guitar: one region");
		if (rs.size () == 1) {
			std::shared_ptr<AudioRegion> ar = std::dynamic_pointer_cast<AudioRegion> (rs[0]);
			check (llabs (rs[0]->position ().samples () - beat (4)) <= 1, "Guitar: clip at beat 4 (2 s)");
			check (llabs (rs[0]->length ().samples () - 4 * sr) <= 1, "Guitar: 8 beats long (4 s)");
			check (llabs (rs[0]->start ().samples () - sr) <= 1, "Guitar: starts 1 s into the file");
			check (ar && llabs (ar->fade_in_length ().samples () - sr / 2) <= 2 && ar->fade_in_active (), string_compose ("Guitar: 1-beat fade in (0.5 s; %1 samples)", ar ? ar->fade_in_length ().samples () : 0));
			check (rs[0]->name () == "Guitar take", "Guitar: clip name");
		}
		check (near (accurate_coefficient_to_dB (g->gain_control ()->get_value ()), -6, 0.05) || g->gain_control ()->automation_state () == Play, "Guitar: -6 dB");
		check (g->pan_azimuth_control () && near (g->pan_azimuth_control ()->get_value (), 0.25, 0.001), "Guitar: pan 0.25");
		std::shared_ptr<Route> gb = s->route_by_name ("Guitar Bus");
		check (gb && g->output ()->audio (0)->connected_to (gb->input ()->audio (0)->name ()), "Guitar: output to Guitar Bus");
		check (!g->output ()->audio (0)->connected_to (s->master_out ()->input ()->audio (0)->name ()), "Guitar: not also to the master");
		std::shared_ptr<AutomationList> al = g->gain_control ()->alist ();
		check (g->gain_control ()->automation_state () == Play && al->size () == 2, "Guitar: volume automation, 2 points");
		if (al->size () == 2) {
			check (near (accurate_coefficient_to_dB ((*al->begin ())->value), -6, 0.05) && near (accurate_coefficient_to_dB ((*al->rbegin ())->value), -12, 0.05), "Guitar: automation -6 dB -> -12 dB");
		}
	}

	/* Vocal */
	std::shared_ptr<Route> v = s->route_by_name ("Vocal");
	check (v && v->input ()->n_ports ().n_audio () == 2, "track Vocal, stereo");
	if (v) {
		vector<std::shared_ptr<Region>> rs = regions (v);
		check (rs.size () == 2, "Vocal: two regions");
		if (rs.size () == 2) {
			check (llabs (rs[0]->position ().samples () - beat (8)) <= 1 && llabs (rs[0]->length ().samples () - 8 * sr) <= 2, "Vocal: nested clip with warps at beat 8, 8 s long");
			check (rs[0]->sources ().size () == 2, "Vocal: both channels");
			check (!rs[0]->muted () && rs[1]->muted (), "Vocal: the disabled clip is a muted region");
			check (llabs (rs[1]->position ().samples () - beat (40)) <= 1, "Vocal: clip after the tempo change at beat 40");
		}
		check (near (v->gain_control ()->get_value (), 0.5, 0.001), "Vocal: gain 0.5");
		std::shared_ptr<Route> rev = s->route_by_name ("Reverb");
		std::shared_ptr<Send>  send = rev ? v->internal_send_for (rev) : std::shared_ptr<Send> ();
		check (send && near (send->gain_control ()->get_value (), 0.25, 0.001), "Vocal: send 0.25 to Reverb");
	}
	bool said_plugin = false;
	for (auto const& n : res.notes) {
		said_plugin |= n.find ("Pro-Q 3") != string::npos;
	}
	check (said_plugin, "the plug-in that was not imported is reported");

	/* Room */
	std::shared_ptr<Route> r = s->route_by_name ("Room");
	check (r && r->muted (), "Room: muted");
	if (r) {
		vector<std::shared_ptr<Region>> rs = regions (r);
		check (rs.size () == 2, "Room: loop played as two regions");
		if (rs.size () == 2) {
			check (rs[0]->position ().samples () == 0 && llabs (rs[0]->length ().samples () - 2 * sr) <= 1, "Room: first pass 0-2 s");
			check (llabs (rs[1]->position ().samples () - 2 * sr) <= 1 && llabs (rs[1]->length ().samples () - sr) <= 1 && rs[1]->start ().samples () == 0, "Room: second pass 2-3 s from the loop start");
		}
	}

	/* Bass, from the folder */
	std::shared_ptr<MidiTrack> b = std::dynamic_pointer_cast<MidiTrack> (s->route_by_name ("Bass"));
	check ((bool) b, "MIDI track Bass (from the folder)");
	if (b) {
		vector<std::shared_ptr<Region>> rs = regions (b);
		check (rs.size () == 1, "Bass: one region");
		if (rs.size () == 1) {
			std::shared_ptr<MidiRegion> mr = std::dynamic_pointer_cast<MidiRegion> (rs[0]);
			check (rs[0]->position ().samples () == 0 && rs[0]->length ().beats () == Beats (8, 0), "Bass: 8 beats at the start");
			size_t n = 0;
			bool   velocity = false;
			for (auto const& nt : mr->model ()->notes ()) {
				++n;
				if (nt->note () == 47) {
					velocity = nt->velocity () == 127 && nt->length () == Beats (2, 0) && nt->time () == Beats (6, 0);
				}
			}
			check (n == 4, string_compose ("Bass: 4 notes; the one outside the clip is left out (%1)", n));
			check (velocity, "Bass: note time, length and velocity");
		}
	}
	check (s->master_out () && near (s->master_out ()->gain_control ()->get_value (), 0.8, 0.001), "Master: gain 0.8");

	/* markers */
	int marks = 0;
	for (auto const& l : s->locations ()->list ()) {
		if (l->is_mark () && !l->is_cue_marker () && (l->name () == "Verse" || l->name () == "Chorus")) {
			++marks;
			if (l->name () == "Chorus") {
				check (llabs (l->start ().samples () - beat (16)) <= 1, "marker Chorus at beat 16");
			}
		}
	}
	check (marks == 2, "markers Verse and Chorus");

	check (!Glib::file_test (Glib::build_filename (Glib::path_get_dirname (Glib::path_get_dirname (argv[1])), "escape.txt"), Glib::FILE_TEST_EXISTS)
	       && !Glib::file_test (Glib::build_filename (Glib::get_tmp_dir (), "escape.txt"), Glib::FILE_TEST_EXISTS),
	       "an archive entry with \"..\" is not written");
	check (s->current_end ().samples () >= beat (44), "session range covers the import");

	s->save_state ("");
	printf ("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
	fflush (stdout);
	SessionUtils::unload_session (s);
	_exit (failures ? 1 : 0);
}

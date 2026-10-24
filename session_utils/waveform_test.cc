/*
 * MusicDAWM Waveform import test (WP6), headless.
 *
 * Imports a project written by tools/waveform/make_test_project.py into a new
 * session and checks it; see that script for the content. The input is
 * either an edit (.tracktionedit, XML or binary), or a .dawproject written by
 * the converter, so all three paths are checked the same way.
 *
 * Built with the other session utilities as <prefix>-waveform_test.
 * Usage: <prefix>-waveform_test <session-dir> <edit.tracktionedit | file.dawproject>
 *        <prefix>-waveform_test --convert <edit.tracktionedit> <out.dawproject>
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

#include "pbd/compose.h"

#include "temporal/tempo.h"

#include "ardour/audio_track.h"
#include "ardour/audioregion.h"
#include "ardour/dB.h"
#include "ardour/dawproject_import.h"
#include "ardour/gain_control.h"
#include "ardour/location.h"
#include "ardour/midi_model.h"
#include "ardour/midi_region.h"
#include "ardour/midi_track.h"
#include "ardour/pannable.h"
#include "ardour/playlist.h"
#include "ardour/session.h"
#include "ardour/waveform_import.h"

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
db_is (gain_t coeff, double dB)
{
	return fabs (accurate_coefficient_to_dB (coeff) - dB) < 0.02;
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
	if (argc == 4 && string (argv[1]) == "--convert") {
		vector<string> notes;
		bool const     ok = WaveformImport::write_dawproject (argv[2], argv[3], notes);
		for (auto const& n : notes) {
			printf ("   note: %s\n", n.c_str ());
		}
		printf ("%s %s\n", ok ? "wrote" : "FAILED to write", argv[3]);
		return ok ? 0 : 1;
	}
	if (argc < 3) {
		cerr << "usage: " << argv[0] << " <session-dir> <edit.tracktionedit | file.dawproject>\n"
		     << "       " << argv[0] << " --convert <edit.tracktionedit> <out.dawproject>\n";
		return 2;
	}

	SessionUtils::init (false);
	BusProfile bus_profile;
	bus_profile.master_out_channels = 2;
	Session* s = SessionUtils::create_session (argv[1], "Mother", 48000, &bus_profile);
	if (!s) {
		return 1;
	}
	samplecnt_t const sr = s->sample_rate ();

	string const in = argv[2];
	bool const   dp = in.size () > 11 && in.substr (in.size () - 11) == ".dawproject";
	DAWProjectImport::Result const res = dp ? DAWProjectImport::import (*s, in) : WaveformImport::import (*s, in);
	printf ("%s from %s: %d tracks, %d busses, %d regions, %d markers\n", dp ? "DAWproject" : "Waveform edit", res.application.c_str (), res.tracks, res.busses, res.regions, res.markers);
	for (auto const& n : res.notes) {
		printf ("   note: %s\n", n.c_str ());
	}
	check (res.ok, "import succeeded");
	check (res.application == "Waveform 11.0.24", "made by Waveform 11.0.24");
	check (res.tracks == 8, string_compose ("8 tracks (%1)", res.tracks));

	char const* names[] = { "01 Guitar L", "02 Guitar R", "03 Guitar DI", "04 Room", "05 Guitar 2", "06 Voice ref", "07 Vocal" };
	bool        all     = true;
	for (auto const* n : names) {
		all = all && std::dynamic_pointer_cast<AudioTrack> (s->route_by_name (n));
	}
	check (all, "audio tracks named after their files (Waveform does not store default names)");

	if (std::shared_ptr<Route> t1 = s->route_by_name ("01 Guitar L")) {
		vector<std::shared_ptr<Region>> rs = regions (t1);
		std::shared_ptr<AudioRegion>    ar = rs.size () == 1 ? std::dynamic_pointer_cast<AudioRegion> (rs[0]) : std::shared_ptr<AudioRegion> ();
		check (ar && db_is (ar->scale_amplitude (), 15.16), "track 1: clip gain +15.16 dB");
		check (ar && llabs (ar->fade_in_length ().samples () - sr / 2) <= 2, "track 1: 0.5 s fade in");
		check (ar && llabs (ar->length ().samples () - 20 * sr) <= 2, "track 1: 20 s (resampled from 44.1 kHz)");
	}
	if (std::shared_ptr<Route> t2 = s->route_by_name ("02 Guitar R")) {
		check (db_is (t2->gain_control ()->get_value (), -3), "track 2: fader -3 dB");
		check (t2->pan_azimuth_control () && fabs (t2->pan_azimuth_control ()->get_value () - 0.75) < 0.001, "track 2: pan right of centre");
	}
	if (std::shared_ptr<Route> t4 = s->route_by_name ("04 Room")) {
		check (t4->muted (), "track 4: muted");
	}
	if (std::shared_ptr<Route> t5 = s->route_by_name ("05 Guitar 2")) {
		vector<std::shared_ptr<Region>> rs = regions (t5);
		check (rs.size () == 2, "track 5: split into 2 clips");
		if (rs.size () == 2) {
			std::shared_ptr<AudioRegion> a = std::dynamic_pointer_cast<AudioRegion> (rs[0]);
			std::shared_ptr<AudioRegion> b = std::dynamic_pointer_cast<AudioRegion> (rs[1]);
			check (llabs (rs[1]->position ().samples () - 12 * sr) <= 1 && llabs (rs[1]->start ().samples () - 12 * sr) <= 2, "track 5: split at 12 s");
			check (a && b && db_is (a->scale_amplitude (), 3) && db_is (b->scale_amplitude (), -6), "track 5: gains +3 / -6 dB");
		}
	}
	if (std::shared_ptr<Route> t7 = s->route_by_name ("07 Vocal")) {
		vector<std::shared_ptr<Region>> rs = regions (t7);
		check (rs.size () == 1 && llabs (rs[0]->position ().samples () - 2 * sr) <= 1, "track 7: vocal at 2 s");
	}
	bool said_plugin = false;
	for (auto const& n : res.notes) {
		said_plugin |= n.find ("TDR Nova") != string::npos;
	}
	check (said_plugin, "the plug-in on the vocal is reported");

	std::shared_ptr<MidiTrack> mt;
	for (auto const& r : *s->get_routes ()) {
		if ((mt = std::dynamic_pointer_cast<MidiTrack> (r))) {
			break;
		}
	}
	check ((bool) mt, "MIDI track");
	if (mt) {
		vector<std::shared_ptr<Region>> rs = regions (mt);
		std::shared_ptr<MidiRegion>     mr = rs.size () == 1 ? std::dynamic_pointer_cast<MidiRegion> (rs[0]) : std::shared_ptr<MidiRegion> ();
		check (mr && llabs (rs[0]->position ().samples () - 4 * sr) <= 1, "MIDI clip at 4 s");
		size_t n = mr ? mr->model ()->notes ().size () : 0;
		check (n == 3, string_compose ("MIDI clip: 3 notes (%1)", n));
	}

	TempoMap::SharedPtr tmap (TempoMap::use ());
	check (fabs (tmap->tempo_at (timepos_t (Beats ())).note_types_per_minute () - 120) < 0.01, "tempo 120");
	bool verse = false;
	for (auto const& l : s->locations ()->list ()) {
		verse |= l->is_mark () && l->name () == "Verse" && llabs (l->start ().samples () - 10 * sr) <= 1;
	}
	check (verse, "marker Verse at 10 s");
	check (s->master_out () && db_is (s->master_out ()->gain_control ()->get_value (), 0), "master at 0 dB");

	s->save_state ("");
	printf ("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
	fflush (stdout);
	SessionUtils::unload_session (s);
	_exit (failures ? 1 : 0);
}

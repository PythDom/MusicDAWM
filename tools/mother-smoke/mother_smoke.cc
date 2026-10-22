/*
 * Phase 0 "Mother" smoke test for the Ardour base.
 *
 * Builds a session shaped like the user's reference project without the GUI:
 * 7 mono WAV tracks placed at their BWF positions, 120 bpm 4/4, track 5 split
 * at 292 s with different gains on each half, a clip gain of +15.16 dB on
 * track 1, and track 4 muted. Saves, closes, reopens, and checks every value.
 *
 * Build: copy into <ardour>/session_utils/, apply
 * patches/upstream/0002-session-utils-let-create_session-take-a-BusProfile.patch, and run ./waf
 * (it becomes <prefix>-mother_smoke). Usage: <prefix>-mother_smoke <session-dir> <wav>...
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <iostream>

#include <glibmm/timer.h>

#include "pbd/basename.h"
#include "pbd/controllable.h"

#include "ardour/audio_track.h"
#include "ardour/audioregion.h"
#include "ardour/audiosource.h"
#include "ardour/dB.h"
#include "ardour/mute_control.h"
#include "ardour/playlist.h"
#include "ardour/region_factory.h"
#include "ardour/session.h"
#include "pbd/stateful_diff_command.h"
#include "ardour/source_factory.h"

#include "temporal/tempo.h"

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
near_db (gain_t coeff, double dB)
{
	return fabs (accurate_coefficient_to_dB (fabsf (coeff)) - dB) < 0.01;
}

static std::shared_ptr<AudioTrack>
track_by_name (Session* s, string const& name)
{
	std::shared_ptr<AudioTrack> t = std::dynamic_pointer_cast<AudioTrack> (s->route_by_name (name));
	if (!t) {
		fprintf (stderr, "no track named '%s'\n", name.c_str ());
		exit (1);
	}
	return t;
}

static void
build (Session* s, vector<string> const& wavs)
{
	for (auto const& path : wavs) {
		string name = PBD::basename_nosuffix (path);

		std::shared_ptr<AudioSource> src = std::dynamic_pointer_cast<AudioSource> (
		    SourceFactory::createExternal (DataType::AUDIO, *s, path, 0, Source::Flag (0), true, false));

		PBD::PropertyList plist;
		plist.add (Properties::start, timepos_t (AudioTime));
		plist.add (Properties::length, src->length ());
		plist.add (Properties::name, name);
		plist.add (Properties::whole_file, true);
		plist.add (Properties::external, true);
		std::shared_ptr<Region> whole = RegionFactory::create (src, plist);

		/* the region on the timeline is a copy of the whole-file region, as in the GUI import */
		std::shared_ptr<Region> r = RegionFactory::create (whole, false);

		list<std::shared_ptr<AudioTrack>> tl = s->new_audio_track (1, 2, 0, 1, name, PresentationInfo::max_order, Normal, true);
		if (tl.empty ()) {
			fprintf (stderr, "cannot create track '%s'\n", name.c_str ());
			exit (1);
		}
		std::shared_ptr<AudioTrack> t = tl.front ();
		printf ("track '%s' <- %s (%" PRId64 " samples at %" PRId64 ")\n", t->name ().c_str (), path.c_str (),
		        src->length ().samples (), src->natural_position ().samples ());
		std::shared_ptr<Playlist> pl = t->playlist ();
		pl->add_region (r, src->natural_position ());
		/* the GUI import extends the session range the same way; export uses it */
		s->maybe_update_session_range (src->natural_position (), src->natural_position () + src->length ());
	}

	samplepos_t const sr = s->sample_rate ();

	/* clip gain above +15 dB (M6) */
	std::shared_ptr<AudioTrack> t1 = track_by_name (s, "01 Guitar L");
	std::dynamic_pointer_cast<AudioRegion> (t1->playlist ()->region_list ()->front ())->set_scale_amplitude (dB_to_coefficient (15.16));

	/* split track 5 at 292 s, different gains on each side (M4, M6) */
	std::shared_ptr<AudioTrack> t5 = track_by_name (s, "05 Guitar 2");
	std::shared_ptr<Playlist>   p5 = t5->playlist ();
	/* like the GUI, edits that add undo commands need an open reversible command */
	s->begin_reversible_command ("split at 292 s");
	p5->clear_changes ();
	p5->split_region (p5->region_list ()->front (), timepos_t (292 * sr));
	s->add_command (new PBD::StatefulDiffCommand (p5));
	s->commit_reversible_command ();
	std::shared_ptr<RegionList> rl5 = p5->region_list (); /* keep the list alive for the loop */
	for (auto const& r : *rl5) {
		double dB = r->position ().samples () < 292 * sr ? 3.0 : -6.0;
		std::dynamic_pointer_cast<AudioRegion> (r)->set_scale_amplitude (dB_to_coefficient (dB));
	}

	/* mute track 4 (M13) */
	track_by_name (s, "04 Room")->mute_control ()->set_value (1.0, PBD::Controllable::NoGroup);

	/* mute is a realtime control: the change is applied by the next process cycle */
	for (int i = 0; i < 100 && !track_by_name (s, "04 Room")->muted (); ++i) {
		Glib::usleep (10000);
	}
}

static void
verify (Session* s, char const* when)
{
	printf ("-- checks %s\n", when);
	samplepos_t const sr = s->sample_rate ();

	check (s->get_tracks ()->size () == 7, "7 audio tracks");
	check (s->master_out () != 0, "master bus");

	TempoMap::SharedPtr tm = TempoMap::use ();
	check (fabs (tm->tempo_at (timepos_t (0)).note_types_per_minute () - 120.0) < 1e-6, "tempo 120 bpm");
	check (tm->meter_at (timepos_t (0)).divisions_per_bar () == 4, "meter 4/4");

	std::shared_ptr<AudioTrack> t1 = track_by_name (s, "01 Guitar L");
	std::shared_ptr<AudioTrack> t7 = track_by_name (s, "07 Vocal");
	check (t1 && t7, "tracks found by name");
	if (!t1 || !t7) {
		return;
	}

	std::shared_ptr<Region> r1 = t1->playlist ()->region_list ()->front ();
	std::shared_ptr<Region> r7 = t7->playlist ()->region_list ()->front ();
	check (r1->position ().samples () == 0, "track 1 at 0 s (BWF TimeReference 0)");
	check (r7->position ().samples () == 2 * 44100 * sr / 44100, "vocal at 2 s (BWF TimeReference)");
	check (r1->length ().samples () == 341 * sr, "length 5:41");
	check (s->current_end ().samples () == 343 * sr, "session range ends with the vocal (2 s + 5:41)");
	check (near_db (std::dynamic_pointer_cast<AudioRegion> (r1)->scale_amplitude (), 15.16), "clip gain +15.16 dB");

	std::shared_ptr<RegionList> rl5 = track_by_name (s, "05 Guitar 2")->playlist ()->region_list ();
	check (rl5->size () == 2, "track 5 split into 2 clips");
	bool split_ok = true;
	for (auto const& r : *rl5) {
		bool   first = r->position ().samples () == 0;
		double want  = first ? 3.0 : -6.0;
		split_ok     = split_ok && near_db (std::dynamic_pointer_cast<AudioRegion> (r)->scale_amplitude (), want);
		if (!first) {
			split_ok = split_ok && r->position ().samples () == 292 * sr;
		}
	}
	check (split_ok, "split at 292 s, gains +3 / -6 dB");

	check (track_by_name (s, "04 Room")->muted (), "track 4 muted");
	check (!t1->muted (), "track 1 not muted");
}

int
main (int argc, char* argv[])
{
	if (argc < 3) {
		cerr << "usage: " << argv[0] << " <session-dir> <wav>...\n";
		return 2;
	}
	string const       dir  = argv[1];
	vector<string> wavs (argv + 2, argv + argc);

	setvbuf (stdout, NULL, _IOLBF, 0);
	SessionUtils::init (false);

	BusProfile bus_profile;
	bus_profile.master_out_channels = 2;
	Session* s = SessionUtils::create_session (dir, "Mother", 44100, &bus_profile);
	if (!s) {
		cerr << "cannot create session in " << dir << "\n";
		return 1;
	}
	build (s, wavs);
	verify (s, "before save");
	s->save_state ("");
	SessionUtils::unload_session (s);

	s = SessionUtils::load_session (dir, "Mother");
	verify (s, "after reopen");
	SessionUtils::unload_session (s);
	SessionUtils::cleanup ();

	printf ("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
	return failures ? 1 : 0;
}

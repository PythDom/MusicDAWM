/*
 * MusicDAWM Drum Studio demo: a short song in one style, without real-time
 * performing. It places the cue markers a performance would record, at the
 * style's meter and suggested tempo, flattens them to MIDI regions and saves
 * the session. Export it with <prefix>-export to listen to the style.
 *
 * Song: intro 2 (2 bars), verse 2 (4), pre-chorus 2 (2), chorus 3 (4),
 * fill 3 (1), chorus 4 (3), ending 2.
 *
 * Built with the other session utilities as <prefix>-drum_studio_demo.
 * Usage: <prefix>-drum_studio_demo <session-dir> <style> [<groove-library-dir>]
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <cstdio>
#include <iostream>
#include <unistd.h>

#include <glibmm/miscutils.h>
#include <glibmm/timer.h>

#include "temporal/tempo.h"

#include "ardour/drum_studio.h"
#include "ardour/location.h"
#include "ardour/midi_track.h"
#include "ardour/session.h"

#include "common.h"

using namespace std;
using namespace ARDOUR;
using namespace Temporal;

int
main (int argc, char* argv[])
{
	if (argc < 3) {
		cerr << "usage: " << argv[0] << " <session-dir> <style> [<groove-library-dir>]\n";
		return 2;
	}
	if (argc > 3) {
		Glib::setenv ("MUSICDAWM_DRUMSTUDIO_DIR", argv[3], true);
	}

	SessionUtils::init (false);
	DrumStudio::Style const* st = DrumStudio::style (argv[2]);
	if (!st) {
		cerr << "unknown style '" << argv[2] << "'; styles:";
		for (auto const& s : DrumStudio::styles ()) {
			cerr << " " << s.key;
		}
		cerr << "\n";
		SessionUtils::cleanup ();
		return 1;
	}

	BusProfile bus_profile;
	bus_profile.master_out_channels = 2;
	Session* s = SessionUtils::create_session (argv[1], "Demo", 48000, &bus_profile);
	if (!s) {
		SessionUtils::cleanup ();
		return 1;
	}

	{
		TempoMap::WritableSharedPtr tmap (TempoMap::write_copy ());
		tmap->set_meter (Meter (st->meter_num, st->meter_den), timepos_t (Beats ()));
		tmap->set_tempo (Tempo (st->tempo, 4), timepos_t (Beats ()));
		TempoMap::update (tmap);
	}

	std::shared_ptr<MidiTrack> track = DrumStudio::create_drum_track (*s, st->key);
	if (!track) {
		cerr << "could not create the drum track\n";
		SessionUtils::unload_session (s);
		SessionUtils::cleanup ();
		_exit (1);
	}
	Glib::usleep (500000); /* regions are set in the trigger thread */

	struct { char const* part; int complexity; int bar; } const song[] = {
		{ "intro", 2, 1 }, { "verse", 2, 3 }, { "prechorus", 2, 7 }, { "chorus", 3, 9 },
		{ "fill", 3, 13 }, { "chorus", 4, 14 }, { "ending", 2, 17 },
	};
	TempoMap::SharedPtr tmap (TempoMap::use ());
	for (auto const& c : song) {
		timepos_t const pos (tmap->quarters_at (BBT_Argument (BBT_Time (c.bar, 1, 0))));
		int const       cue = DrumStudio::slot (DrumStudio::part_index (c.part), c.complexity);
		s->locations ()->add (new Location (*s, pos, pos, string (), Location::Flags (Location::IsMark | Location::IsCueMarker), cue));
	}
	timepos_t const end (tmap->quarters_at (BBT_Argument (BBT_Time (19, 1, 0))));
	s->maybe_update_session_range (timepos_t (Beats ()), end);

	int const made = DrumStudio::flatten (*s, track, timepos_t (Beats ()), end);
	printf ("%s: %d/%d at %.0f BPM, %d regions\n", st->name.c_str (), st->meter_num, st->meter_den, st->tempo, made);
	s->save_state ("");
	fflush (stdout);

	track.reset ();
	SessionUtils::unload_session (s);
	/* like the test: the process may hold references past cleanup */
	_exit (made > 0 ? 0 : 1);
}

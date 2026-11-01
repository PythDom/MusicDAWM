/*
 * MusicDAWM WP2: "New song from WAV files" test, headless.
 *
 * Takes as a Zoom R8 writes them (BWF, a time stamp per take): mono and
 * stereo, one at 44.1 kHz in a 48 kHz song, one without a time stamp. Each
 * becomes a track named after the file, at its recorded position.
 *
 * Usage: <prefix>-song_import_test <session-dir>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <unistd.h>

#include <glib/gstdio.h>
#include <glibmm/miscutils.h>
#include <sndfile.h>

#include "pbd/compose.h"

#include "ardour/audio_track.h"
#include "ardour/playlist.h"
#include "ardour/region.h"
#include "ardour/session.h"
#include "ardour/song_import.h"

#include "common.h"

using namespace std;
using namespace ARDOUR;

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

/* a BWF file (or a plain WAV when stamp < 0) of a tone */
static string
write_take (string const& dir, string const& name, int rate, int channels, double seconds, double stamp_seconds)
{
	string const path = Glib::build_filename (dir, name);
	SF_INFO      info;
	memset (&info, 0, sizeof (info));
	info.samplerate = rate;
	info.channels   = channels;
	info.format     = SF_FORMAT_WAV | SF_FORMAT_PCM_24;
	SNDFILE* sf     = sf_open (path.c_str (), SFM_WRITE, &info);
	if (stamp_seconds >= 0) {
		SF_BROADCAST_INFO bi;
		memset (&bi, 0, sizeof (bi));
		strncpy (bi.description, "MusicDAWM test take", sizeof (bi.description) - 1);
		int64_t const tr       = (int64_t)llround (stamp_seconds * rate);
		bi.time_reference_low  = (uint32_t)(tr & 0xffffffff);
		bi.time_reference_high = (uint32_t)(tr >> 32);
		sf_command (sf, SFC_SET_BROADCAST_INFO, &bi, sizeof (bi));
	}
	sf_count_t const n = (sf_count_t)(seconds * rate);
	vector<float>    x (n * channels);
	for (sf_count_t i = 0; i < n; ++i) {
		for (int c = 0; c < channels; ++c) {
			x[i * channels + c] = 0.3f * sinf (2 * M_PI * (220 + 110 * c) * i / rate);
		}
	}
	sf_writef_float (sf, x.data (), n);
	sf_close (sf);
	return path;
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
	Session* s = SessionUtils::create_session (dir, "song", SR, &bus_profile);
	if (!s) {
		return 1;
	}

	/* the takes, as on the card: the song starts at the earliest (10 s) */
	string const card = Glib::build_filename (dir, "card");
	g_mkdir_with_parents (card.c_str (), 0755);
	write_take (card, "01 Guitar L.wav", SR, 1, 3, 10.0);
	write_take (card, "02_Guitar_R.WAV", SR, 1, 3, 12.0);
	write_take (card, "03 Vocal take.wav", SR, 2, 2, 11.5);
	write_take (card, "04-Bass.wav", 44100, 1, 4, 10.25);
	write_take (card, "05 Tambourine.wav", SR, 1, 1, -1); /* no time stamp */
	FILE* junk = fopen (Glib::build_filename (card, "notes.txt").c_str (), "w");
	fputs ("not audio", junk);
	fclose (junk);

	SongImport::File const pf = SongImport::probe (Glib::build_filename (card, "04-Bass.wav"));
	printf ("   04-Bass.wav: %u ch, %d Hz, time stamp %s %lld\n", pf.channels, pf.rate, pf.has_time_reference ? "yes" : "no", (long long)pf.time_reference);
	check (pf.ok && pf.channels == 1 && pf.rate == 44100 && pf.has_time_reference && pf.time_reference == 452025, "the BWF time stamp is read");
	check (SongImport::track_name ("/x/01 Guitar L.wav") == "Guitar L" && SongImport::track_name ("/x/02_Guitar_R.WAV") == "Guitar R"
	       && SongImport::track_name ("/x/04-Bass.wav") == "Bass" && SongImport::track_name ("/x/TRACK07.WAV") == "TRACK07",
	       "track names from the file names");

	vector<string> const wavs = SongImport::wav_files_in (card);
	check (wavs.size () == 5, string_compose ("the folder's WAV files (%1)", wavs.size ()));

	SongImport::Report const r = SongImport::add_files (*s, wavs);
	printf ("   %d tracks, %d converted, %d without time stamp, %zu failed\n", r.tracks, r.converted, r.no_stamp, r.failed.size ());
	check (r.tracks == 5 && r.converted == 1 && r.no_stamp == 1 && r.failed.empty (), "5 tracks, the 44.1 kHz take converted, 1 without a time stamp");

	struct Expect {
		char const* name;
		uint32_t    channels;
		double      at; /* seconds into the song */
		double      length;
	};
	Expect const expect[] = {
		{ "Guitar L", 1, 0.0, 3 }, { "Guitar R", 1, 2.0, 3 }, { "Vocal take", 2, 1.5, 2 }, { "Bass", 1, 0.25, 4 }, { "Tambourine", 1, 0.0, 1 },
	};
	bool all = true;
	for (auto const& e : expect) {
		std::shared_ptr<Track> t = std::dynamic_pointer_cast<Track> (s->route_by_name (e.name));
		if (!t || t->playlist ()->region_list ()->empty ()) {
			printf ("   %s: missing\n", e.name);
			all = false;
			continue;
		}
		std::shared_ptr<Region> reg = t->playlist ()->region_list ()->front ();
		samplepos_t const       pos = reg->position ().samples ();
		samplecnt_t const       len = reg->length ().samples ();
		printf ("   %-10s %u ch at %.4f s, %.4f s long\n", e.name, t->n_inputs ().n_audio (), pos / double (SR), len / double (SR));
		all &= t->n_inputs ().n_audio () == e.channels && std::llabs (pos - (samplepos_t)llround (e.at * SR)) <= 1
		       && std::llabs (len - (samplecnt_t)llround (e.length * SR)) <= 2;
	}
	check (all, "each take on its own track, at its recorded position (to the sample), at the song's rate");
	check (s->current_start_sample () == 0 && std::llabs (s->current_end_sample () - (samplepos_t)(5 * SR)) <= 2,
	       "the song range covers the takes (0 - 5 s)");

	s->save_state ("");
	printf ("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
	fflush (stdout);
	SessionUtils::unload_session (s);
	_exit (failures ? 1 : 0);
}

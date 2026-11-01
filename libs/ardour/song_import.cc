/*
 * MusicDAWM WP2: a new song from WAV files (see ardour/song_import.h).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <algorithm>
#include <cctype>
#include <cstring>

#include <glib.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>
#include <sndfile.h>

#include "pbd/basename.h"
#include "pbd/error.h"

#include "ardour/audio_track.h"
#include "ardour/import_status.h"
#include "ardour/playlist.h"
#include "ardour/region_factory.h"
#include "ardour/session.h"
#include "ardour/song_import.h"
#include "ardour/source.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using std::string;
using std::vector;

SongImport::File
SongImport::probe (string const& path)
{
	File    f;
	SF_INFO info;
	f.path = path;
	memset (&info, 0, sizeof (info));
	SNDFILE* sf = sf_open (path.c_str (), SFM_READ, &info);
	if (!sf) {
		return f;
	}
	f.ok       = true;
	f.channels = info.channels;
	f.rate     = info.samplerate;
	f.length   = info.frames;
	SF_BROADCAST_INFO bi;
	memset (&bi, 0, sizeof (bi));
	if (sf_command (sf, SFC_GET_BROADCAST_INFO, &bi, sizeof (bi)) == SF_TRUE) {
		f.has_time_reference = true;
		f.time_reference     = ((int64_t)bi.time_reference_high << 32) | (int64_t)bi.time_reference_low;
	}
	sf_close (sf);
	return f;
}

string
SongImport::track_name (string const& path)
{
	string n = PBD::basename_nosuffix (path);
	/* "01 Guitar L", "01_Guitar-L", "01-Guitar L" -> "Guitar L" */
	size_t i = 0;
	while (i < n.size () && isdigit ((unsigned char)n[i])) {
		++i;
	}
	if (i > 0 && i < n.size () && (n[i] == ' ' || n[i] == '_' || n[i] == '-' || n[i] == '.')) {
		n = n.substr (i + 1);
	}
	std::replace (n.begin (), n.end (), '_', ' ');
	while (!n.empty () && n[0] == ' ') {
		n.erase (0, 1);
	}
	return n.empty () ? string (PBD::basename_nosuffix (path)) : n;
}

vector<string>
SongImport::wav_files_in (string const& folder)
{
	vector<string> v;
	try {
		Glib::Dir d (folder);
		for (auto const& e : d) {
			gchar*       l     = g_ascii_strdown (e.c_str (), -1);
			string const lower (l);
			g_free (l);
			if (lower.size () > 4 && lower.compare (lower.size () - 4, 4, ".wav") == 0) {
				v.push_back (Glib::build_filename (folder, e));
			}
		}
	} catch (...) {
	}
	std::sort (v.begin (), v.end ());
	return v;
}

SongImport::Report
SongImport::add_files (Session& s, vector<string> paths)
{
	Report r;
	std::sort (paths.begin (), paths.end ());

	vector<File> files;
	for (auto const& p : paths) {
		File f = probe (p);
		if (!f.ok) {
			r.failed.push_back (p);
			continue;
		}
		files.push_back (f);
	}

	/* the song starts at the earliest take (in seconds, rates may differ) */
	double first = -1;
	for (auto const& f : files) {
		if (f.has_time_reference) {
			double const t = f.time_reference / double (f.rate);
			first          = first < 0 ? t : std::min (first, t);
		}
	}
	if (first < 0) {
		first = 0;
	}

	samplecnt_t const sr = s.nominal_sample_rate ();
	samplepos_t       end = 0;
	for (auto const& f : files) {
		ImportStatus status;
		status.total                   = 1;
		status.current                 = 1;
		status.quality                 = SrcBest;
		status.freeze                  = false;
		status.replace_existing_source = false;
		status.split_midi_channels     = false;
		status.import_markers          = false;
		status.midi_track_name_source  = ARDOUR::SMFFileAndTrackName;
		status.paths.push_back (f.path);
		s.import_files (status);
		if (status.cancel || status.sources.empty ()) {
			r.failed.push_back (f.path);
			continue;
		}
		SourceList sl;
		for (auto const& src : status.sources) {
			sl.push_back (src);
		}
		uint32_t const nch = std::min<uint32_t> (2, sl.size ());
		sl.resize (nch);

		std::list<std::shared_ptr<AudioTrack>> tl = s.new_audio_track (nch, 2, 0, 1, track_name (f.path), PresentationInfo::max_order, Normal, true);
		if (tl.empty ()) {
			r.failed.push_back (f.path);
			continue;
		}
		std::shared_ptr<AudioTrack> t = tl.front ();

		PBD::PropertyList whole;
		whole.add (Properties::start, timepos_t (Temporal::AudioTime));
		whole.add (Properties::length, sl.front ()->length ());
		whole.add (Properties::name, string (PBD::basename_nosuffix (f.path)));
		whole.add (Properties::whole_file, true);
		std::shared_ptr<Region> wr = RegionFactory::create (sl, whole);
		PBD::PropertyList       plist;
		plist.add (Properties::start, timepos_t (Temporal::AudioTime));
		plist.add (Properties::length, sl.front ()->length ());
		plist.add (Properties::name, string (PBD::basename_nosuffix (f.path)));
		std::shared_ptr<Region> reg = RegionFactory::create (wr, plist);

		samplepos_t pos = 0;
		if (f.has_time_reference) {
			pos = (samplepos_t)llround ((f.time_reference / double (f.rate) - first) * sr);
		} else {
			++r.no_stamp;
		}
		t->playlist ()->add_region (reg, timepos_t (pos));
		end = std::max (end, pos + reg->length ().samples ());
		if (f.rate != sr) {
			++r.converted;
		}
		++r.tracks;
	}
	if (end > 0) {
		s.maybe_update_session_range (timepos_t (Temporal::AudioTime), timepos_t (end));
	}
	return r;
}

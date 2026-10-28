/*
 * MusicDAWM ARA 2 hosting test (WP9), headless.
 *
 * Uses the ARA SDK's test plug-in (ARATestPlugIn.vst3, built from
 * ARA_SDK/ARA_Examples) as Region FX of an audio region and checks that it
 * works in ARA mode: it reads the audio through the host, analyses it (notes),
 * renders the region where it is on the timeline, follows moves and trims,
 * and gets its data back after the session is saved and loaded again.
 *
 * Built with --ara only, as <prefix>-ara_test.
 * Usage: <prefix>-ara_test <session-dir> <path/to/ARATestPlugIn.vst3>
 *        <prefix>-ara_test --reload <session-dir> <path/to/ARATestPlugIn.vst3>
 * (the second run loads the session saved by the first)
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

#include <glib.h>
#include <glibmm/miscutils.h>

#include "pbd/compose.h"

#include "ardour/ara_host.h"
#include "ardour/audio_track.h"
#include "ardour/audioregion.h"
#include "ardour/audiosource.h"
#include "ardour/playlist.h"
#include "ardour/region_factory.h"
#include "ardour/region_fx_plugin.h"
#include "ardour/session.h"
#include "ardour/source_factory.h"
#include "ardour/plugin_manager.h"
#include "ardour/plugin_scan_result.h"
#include "ardour/rc_configuration.h"

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

/* the source: 1 s tone, 0.5 s digital silence, 1.5 s tone. The tone never
 * hits 0.0 (the test plug-in's "analysis" splits notes at zero samples). */
static float
signal_at (samplepos_t i)
{
	if (i >= SR && i < SR * 3 / 2) {
		return 0;
	}
	return 0.4f + 0.3f * sinf (2.f * M_PI * 220.f * i / SR);
}

static void
write_wav (string const& path, samplecnt_t n)
{
	FILE* f = fopen (path.c_str (), "wb");
	auto  u32 = [f] (uint32_t v) { fwrite (&v, 4, 1, f); };
	auto  u16 = [f] (uint16_t v) { fwrite (&v, 2, 1, f); };
	fwrite ("RIFF", 1, 4, f);
	u32 (36 + n * 4);
	fwrite ("WAVEfmt ", 1, 8, f);
	u32 (16);
	u16 (3); /* float */
	u16 (1);
	u32 (SR);
	u32 (SR * 4);
	u16 (4);
	u16 (32);
	fwrite ("data", 1, 4, f);
	u32 (n * 4);
	for (samplepos_t i = 0; i < n; ++i) {
		float s = signal_at (i);
		fwrite (&s, 4, 1, f);
	}
	fclose (f);
}

/* the region as heard at [at, at + n), compared with the source */
static bool
renders (std::shared_ptr<AudioRegion> r, samplepos_t at, samplecnt_t n, float gain = 1.f)
{
	vector<Sample> buf (n), mix (n);
	vector<gain_t> g (n);
	samplecnt_t const got = r->read_at (buf.data (), mix.data (), g.data (), at, n, 0);
	if (got != n) {
		printf ("   read %ld of %ld samples\n", (long)got, (long)n);
		return false;
	}
	samplepos_t const src_at = at - r->position ().samples () + r->start ().samples ();
	double            err    = 0;
	for (samplecnt_t i = 0; i < n; ++i) {
		err = max<double> (err, fabs (buf[i] - gain * signal_at (src_at + i)));
	}
	if (err > 1e-4) {
		printf ("   largest difference %g (first sample %g, expected %g)\n", err, buf[0], gain * signal_at (src_at));
	}
	return err <= 1e-4;
}

static bool
wait_for_notes (std::shared_ptr<AudioRegion> r, vector<ARAHost::Note>& notes)
{
	for (int i = 0; i < 500; ++i) {
		ARAHost::idle ();
		notes.clear ();
		if (ARAHost::region_notes (r.get (), notes)) {
			return true;
		}
		g_usleep (20000);
	}
	return false;
}

/* the plug-in's folder is a VST3 folder, as set in the preferences */
static PluginManager&
find_plugins (string const& module)
{
	Config->set_plugin_path_vst3 (Glib::path_get_dirname (module));
	PluginManager& pm = PluginManager::instance ();
	pm.refresh (false);
	return pm;
}

/* through Ardour's plug-in scan, as the program finds Melodyne; the session
 * finds it again by its ID when it is loaded */
static PluginPtr
load_plugin (Session& s, string const& module)
{
	PluginManager& pm = find_plugins (module);
	for (auto const& i : pm.vst3_plugin_info ()) {
		if (i->name == "ARATestPlugIn") {
			PluginPtr p = i->load (s);
			if (!p) {
				printf ("   found %s, but it does not load\n", i->path.c_str ());
			}
			return p;
		}
	}
	/* not found: say what the scan saw */
	printf ("   VST3 path: %s\n", Config->get_plugin_path_vst3 ().c_str ());
	for (auto const& i : pm.vst3_plugin_info ()) {
		printf ("   VST3 plug-in: %s (%s)\n", i->name.c_str (), i->path.c_str ());
	}
	std::vector<std::shared_ptr<PluginScanLogEntry>> log;
	pm.scan_log (log);
	for (auto const& e : log) {
		if (e->type () == VST3) {
			printf ("   scan of %s: result 0x%x\n%s\n", e->path ().c_str (), (unsigned)e->result (), e->log ().c_str ());
		}
	}
	return PluginPtr ();
}

static std::shared_ptr<AudioRegion>
the_region (Session* s)
{
	std::shared_ptr<AudioTrack> t = std::dynamic_pointer_cast<AudioTrack> (s->route_by_name ("Voice"));
	if (!t) {
		return std::shared_ptr<AudioRegion> ();
	}
	std::shared_ptr<RegionList> rl = t->playlist ()->region_list ();
	return rl->size () == 1 ? std::dynamic_pointer_cast<AudioRegion> (rl->front ()) : std::shared_ptr<AudioRegion> ();
}

static int
first (string const& dir, string const& module)
{

	SessionUtils::init (false);
	BusProfile bus_profile;
	bus_profile.master_out_channels = 2;
	Session* s = SessionUtils::create_session (dir, "ara", SR, &bus_profile);
	if (!s) {
		return 1;
	}

	/* a mono track "Voice", the region at 2 s */
	string const wav = Glib::build_filename (dir, "voice.wav");
	write_wav (wav, 3 * SR);
	std::shared_ptr<AudioSource> src = std::dynamic_pointer_cast<AudioSource> (
	    SourceFactory::createExternal (DataType::AUDIO, *s, wav, 0, Source::Flag (0), true, false));
	PBD::PropertyList plist;
	plist.add (Properties::start, timepos_t (AudioTime));
	plist.add (Properties::length, src->length ());
	plist.add (Properties::name, string ("voice"));
	plist.add (Properties::whole_file, true);
	plist.add (Properties::external, true);
	std::shared_ptr<Region> whole = RegionFactory::create (src, plist);
	std::shared_ptr<AudioRegion> r = std::dynamic_pointer_cast<AudioRegion> (RegionFactory::create (whole, false));
	std::shared_ptr<AudioTrack>  t = s->new_audio_track (1, 2, 0, 1, "Voice", PresentationInfo::max_order, Normal, true).front ();
	t->playlist ()->add_region (r, timepos_t (2 * SR));

	PluginPtr plugin = load_plugin (*s, module);
	check ((bool)plugin, "ARATestPlugIn loads");
	if (!plugin) {
		fflush (stdout);
		_exit (1);
	}
	check (plugin->ara_factory () != 0, "it is an ARA 2 plug-in");
	check (plugin->ara_extension () == 0, "as an ordinary plug-in it is not in ARA mode");

	std::shared_ptr<RegionFxPlugin> rfx (new RegionFxPlugin (*s, AudioTime, plugin));
	check (r->add_plugin (rfx), "added as Region FX");
	check (plugin->ara_extension () != 0, "as Region FX it is in ARA mode");
	check (ARAHost::playback_regions (*s) == 1, "the region is a playback region of the ARA document");

	vector<ARAHost::Note> notes;
	bool const analysed = wait_for_notes (r, notes);
	check (analysed && ARAHost::samples_read () >= 3 * SR, string_compose ("the plug-in read the audio through the host (%1 samples)", ARAHost::samples_read ()));
	for (auto const& n : notes) {
		printf ("   note at %.3f s, %.3f s long\n", n.start, n.duration);
	}
	check (analysed && notes.size () == 2 && fabs (notes[1].start - 3.5) < 0.01, "analysis: 2 notes, the second at 3.5 s on the timeline");

	check (renders (r, 2 * SR + SR / 2, 4096), "renders the region at its position");

	r->set_position (timepos_t (10 * SR));
	check (renders (r, 10 * SR + SR / 4, 4096), "moved to 10 s: renders there");

	r->trim_front (timepos_t (10 * SR + SR / 2));
	check (r->start ().samples () == SR / 2 && renders (r, 10 * SR + SR / 2 + 100, 4096), "trimmed: renders from the new start");
	notes.clear ();
	check (ARAHost::region_notes (r.get (), notes) && !notes.empty () && fabs (notes.back ().start - 11.5) < 0.01, "trimmed: the notes follow (second note at 11.5 s)");

	/* an edit in the plug-in (e.g. a Melodyne pitch change) is reported as a
	 * content change: the region must be read again */
	{
		bool                  refreshed = false;
		PBD::ScopedConnection c;
		r->PropertyChanged.connect_same_thread (c, [&refreshed] (PBD::PropertyChange const& what) {
			refreshed |= what.contains (Properties::region_fx);
		});
		bool const sent = ARAHost::simulate_content_change (r.get ());
		check (sent && refreshed && renders (r, 10 * SR + SR, 4096), "a change of the plug-in's output makes the region read again");
	}

	r->set_scale_amplitude (0.5);
	check (renders (r, 10 * SR + SR, 4096, 0.5), "region gain applies after the ARA rendering");
	r->set_scale_amplitude (1.0);

	s->save_state ("");
	bool saved = false;
	if (GDir* d = g_dir_open (s->plugins_dir ().c_str (), 0, 0)) {
		while (char const* f = g_dir_read_name (d)) {
			string const n = f;
			if (n.find ("ara-") == 0 && n.size () > 8 && n.substr (n.size () - 8) == "-ara.dat") {
				printf ("   %s\n", f);
				saved = true;
			}
		}
		g_dir_close (d);
	}
	check (saved, "ARA data saved with the session");

	/* close: the plug-in goes with the session, then its ARA document */
	plugin.reset ();
	rfx.reset ();
	r.reset ();
	whole.reset ();
	src.reset ();
	t.reset ();
	SessionUtils::unload_session (s);
	check (ARAHost::documents_alive () == 0, "after closing: the ARA document is gone (and the plug-in uninitialized)");

	printf ("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
	fflush (stdout);
	_exit (failures ? 1 : 0);
}

/* the second run: load the session saved by the first (a new process; the
 * session utilities cannot load a second session after unloading one) */
static int
reload (string const& dir, string const& module)
{
	SessionUtils::init (false);
	/* the plug-in scan, as the program has it when it starts */
	find_plugins (module);

	Session* s = SessionUtils::load_session (dir, "ara");
	check (s != 0, "session loads again");
	if (!s) {
		return 1;
	}
	vector<ARAHost::Note>        notes;
	std::shared_ptr<AudioRegion> r = the_region (s);
	check (r && r->nth_plugin (0) && r->nth_plugin (0)->plugin () && r->nth_plugin (0)->plugin ()->ara_extension (), "after loading: the Region FX is in ARA mode again");
	check (ARAHost::playback_regions (*s) == 1, "after loading: one playback region");
	/* asked at once, before the plug-in could analyse anything */
	bool const restored = r && ARAHost::region_notes (r.get (), notes);
	for (auto const& n : notes) {
		printf ("   note at %.3f s, %.3f s long\n", n.start, n.duration);
	}
	check (restored && notes.size () == 2 && fabs (notes.back ().start - 11.5) < 0.01, "after loading: the plug-in's notes are restored from the saved data");
	check (r && renders (r, 10 * SR + SR / 2 + 100, 4096), "after loading: renders as before");

	r.reset ();
	SessionUtils::unload_session (s);
	check (ARAHost::documents_alive () == 0, "after closing: the ARA document is gone (and the plug-in uninitialized)");

	printf ("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
	fflush (stdout);
	_exit (failures ? 1 : 0);
}

int
main (int argc, char* argv[])
{
	setvbuf (stdout, NULL, _IOLBF, 0);
	bool const again = argc == 4 && string (argv[1]) == "--reload";
	if (argc != 3 && !again) {
		cerr << "usage: " << argv[0] << " <session-dir> <ARATestPlugIn.vst3 module>\n"
		     << "       " << argv[0] << " --reload <session-dir> <ARATestPlugIn.vst3 module>\n";
		return 2;
	}
	return again ? reload (argv[2], argv[3]) : first (argv[1], argv[2]);
}

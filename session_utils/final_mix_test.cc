/*
 * MusicDAWM WP7 + Final Mix test, headless.
 *
 * M1: the bundled x42 plug-ins (limiter, EQ with high-pass) load and do
 * their job, and freezing a track with a plug-in works and can be undone.
 * M2: the master bus loudness meter (LUFS, true peak) while playing.
 * M3: the Final Mix model: roles, per-track tools, FX on/off, removing a
 * track from the mix (undoable), the master limiter.
 *
 * The x42 bundles must be on the LV2 path (LV2_PATH, or the program's own
 * LV2 folder in the Windows bundle).
 *
 * Usage: <prefix>-final_mix_test <session-dir>
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

#include <glibmm/timer.h>

#include <glibmm/miscutils.h>

#include "pbd/compose.h"

#include "ardour/amp.h"
#include "ardour/audio_track.h"
#include "ardour/audioregion.h"
#include "ardour/audiosource.h"
#include "ardour/automation_control.h"
#include "ardour/dB.h"
#include "ardour/final_mix.h"
#include "ardour/internal_send.h"
#include "ardour/mute_control.h"
#include "ardour/interthread_info.h"
#include "ardour/lv2_plugin.h"
#include "ardour/playlist.h"
#include "ardour/plugin.h"
#include "ardour/plugin_insert.h"
#include "ardour/plugin_manager.h"
#include "ardour/region_factory.h"
#include "ardour/region_fx_plugin.h"
#include "ardour/route.h"
#include "ardour/session.h"
#include "ardour/source_factory.h"

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

static string const DPL_MONO   = "http://gareus.org/oss/lv2/dpl#mono";
static string const FIL4_MONO  = "http://gareus.org/oss/lv2/fil4#mono";
static string const DPL_STEREO = "http://gareus.org/oss/lv2/dpl#stereo";

static PluginPtr
load_lv2 (Session& s, string const& uri)
{
	for (auto const& i : PluginManager::instance ().lv2_plugin_info ()) {
		if (i->unique_id == uri) {
			return i->load (s);
		}
	}
	return PluginPtr ();
}

/* a plug-in parameter, by its LV2 symbol, through the processor's control */
static bool
set_param (std::shared_ptr<PlugInsertBase> proc, PluginPtr p, string const& symbol, float v)
{
	std::shared_ptr<LV2Plugin> lv2 = std::dynamic_pointer_cast<LV2Plugin> (p);
	if (!lv2) {
		return false;
	}
	uint32_t const port = lv2->port_index (symbol.c_str ());
	if (port == (uint32_t)-1) {
		return false;
	}
	std::shared_ptr<AutomationControl> c = std::dynamic_pointer_cast<AutomationControl> (proc->control (Evoral::Parameter (PluginAutomation, 0, port)));
	if (!c) {
		return false;
	}
	c->set_value (v, PBD::Controllable::NoGroup);
	return true;
}

/* a mono WAV of a function of time */
template <typename F>
static string
write_wav (string const& dir, string const& name, double seconds, F f)
{
	string const      path = Glib::build_filename (dir, name + ".wav");
	samplecnt_t const n    = (samplecnt_t)(seconds * SR);
	FILE*             fp   = fopen (path.c_str (), "wb");
	auto              u32  = [fp] (uint32_t v) { fwrite (&v, 4, 1, fp); };
	auto              u16  = [fp] (uint16_t v) { fwrite (&v, 2, 1, fp); };
	fwrite ("RIFF", 1, 4, fp);
	u32 (36 + n * 4);
	fwrite ("WAVEfmt ", 1, 8, fp);
	u32 (16);
	u16 (3);
	u16 (1);
	u32 (SR);
	u32 (SR * 4);
	u16 (4);
	u16 (32);
	fwrite ("data", 1, 4, fp);
	u32 (n * 4);
	for (samplecnt_t i = 0; i < n; ++i) {
		float const x = f (i / double (SR));
		fwrite (&x, 4, 1, fp);
	}
	fclose (fp);
	return path;
}

static std::shared_ptr<AudioRegion>
add_track (Session& s, string const& name, string const& wav, std::shared_ptr<AudioTrack>* track = 0)
{
	std::shared_ptr<AudioSource> src = std::dynamic_pointer_cast<AudioSource> (
	    SourceFactory::createExternal (DataType::AUDIO, s, wav, 0, Source::Flag (0), true, false));
	PBD::PropertyList plist;
	plist.add (Properties::start, timepos_t (Temporal::AudioTime));
	plist.add (Properties::length, src->length ());
	plist.add (Properties::name, name);
	plist.add (Properties::whole_file, true);
	plist.add (Properties::external, true);
	std::shared_ptr<Region>      whole = RegionFactory::create (src, plist);
	std::shared_ptr<AudioRegion> r     = std::dynamic_pointer_cast<AudioRegion> (RegionFactory::create (whole, false));
	std::shared_ptr<AudioTrack>  t     = s.new_audio_track (1, 2, 0, 1, name, PresentationInfo::max_order, Normal, true).front ();
	t->playlist ()->add_region (r, timepos_t (Temporal::AudioTime));
	/* as an import does: the session range covers the region (freeze renders it) */
	s.maybe_update_session_range (timepos_t (Temporal::AudioTime), timepos_t (src->length ().samples ()));
	if (track) {
		*track = t;
	}
	return r;
}

/* read the region (with its Region FX) */
static vector<Sample>
render (std::shared_ptr<AudioRegion> r, samplecnt_t n)
{
	vector<Sample> buf (n), mix (n);
	vector<gain_t> g (n);
	r->read_at (buf.data (), mix.data (), g.data (), r->position ().samples (), n, 0);
	return buf;
}

/* level (dB) of one frequency in x, by correlation */
static double
level_at (vector<Sample> const& x, double hz, samplecnt_t from)
{
	double re = 0, im = 0;
	samplecnt_t n = 0;
	for (samplecnt_t i = from; i < (samplecnt_t)x.size (); ++i, ++n) {
		re += x[i] * cos (2 * M_PI * hz * i / SR);
		im += x[i] * sin (2 * M_PI * hz * i / SR);
	}
	return 20 * log10 (2 * sqrt (re * re + im * im) / n + 1e-12);
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
	Session* s = SessionUtils::create_session (dir, "finalmix", SR, &bus_profile);
	if (!s) {
		return 1;
	}

	/* ---- M1: the bundled plug-ins ---- */
	PluginPtr lim = load_lv2 (*s, DPL_MONO);
	PluginPtr eq  = load_lv2 (*s, FIL4_MONO);
	check (lim && load_lv2 (*s, DPL_STEREO), "x42 limiter (mono and stereo) found");
	check ((bool)eq, "x42-fil4 EQ found");
	if (!lim || !eq) {
		printf ("FAILED: %d failure(s)\n", failures);
		fflush (stdout);
		_exit (1);
	}

	/* the limiter keeps a loud signal (peaks +6 dBFS) under its ceiling */
	string const loud = write_wav (dir, "loud", 3, [] (double t) { return float (2.0 * sin (2 * M_PI * 220 * t) * (0.5 + 0.5 * sin (2 * M_PI * 3 * t))); });
	std::shared_ptr<AudioRegion>    rl  = add_track (*s, "Loud", loud);
	std::shared_ptr<RegionFxPlugin> fxl (new RegionFxPlugin (*s, Temporal::AudioTime, lim));
	check (rl->add_plugin (fxl), "limiter added");
	check (set_param (fxl, lim, "threshold", -1.0) && set_param (fxl, lim, "gain", 0.0), "limiter: ceiling -1 dB");
	vector<Sample> yl   = render (rl, 3 * SR);
	float          peak = 0;
	for (samplecnt_t i = SR / 10; i < (samplecnt_t)yl.size (); ++i) { /* after the attack */
		peak = max (peak, fabsf (yl[i]));
	}
	double const peak_db = 20 * log10 (peak + 1e-12);
	printf ("   limited peak %.2f dBFS (input +6 dBFS)\n", peak_db);
	check (peak_db <= -0.9 && peak_db > -3, "limiter: peaks at the -1 dB ceiling");

	/* the EQ's high-pass removes rumble: 40 Hz + 1 kHz, high-pass at 120 Hz */
	string const rumble = write_wav (dir, "rumble", 2, [] (double t) { return float (0.3 * sin (2 * M_PI * 40 * t) + 0.3 * sin (2 * M_PI * 1000 * t)); });
	std::shared_ptr<AudioRegion> re  = add_track (*s, "Rumble", rumble);
	vector<Sample>               dry = render (re, 2 * SR);
	std::shared_ptr<RegionFxPlugin> fxe (new RegionFxPlugin (*s, Temporal::AudioTime, eq));
	check (re->add_plugin (fxe), "EQ added");
	check (set_param (fxe, eq, "HighPass", 1) && set_param (fxe, eq, "HPfreq", 120) && set_param (fxe, eq, "HPQ", 0.7), "EQ: high-pass on, 120 Hz");
	vector<Sample> wet = render (re, 2 * SR);
	double const   lo  = level_at (wet, 40, SR / 2) - level_at (dry, 40, SR / 2);
	double const   hi  = level_at (wet, 1000, SR / 2) - level_at (dry, 1000, SR / 2);
	printf ("   high-pass: 40 Hz %.1f dB, 1 kHz %.1f dB\n", lo, hi);
	check (lo < -15 && fabs (hi) < 0.5, "EQ: 40 Hz rumble cut by more than 15 dB, 1 kHz untouched");

	/* ---- M1: freeze a track with a plug-in, then unfreeze ---- */
	std::shared_ptr<AudioTrack>  ft;
	std::shared_ptr<AudioRegion> fr = add_track (*s, "Freeze me", rumble, &ft);
	PluginPtr                    eq2 = load_lv2 (*s, FIL4_MONO);
	std::shared_ptr<Processor>   pi (new PluginInsert (*s, *ft, eq2));
	ft->add_processor (pi, PreFader);
	size_t const regions_before = ft->playlist ()->region_list ()->size ();
	string const playlist_before = ft->playlist ()->name ();
	{
		InterThreadInfo itt;
		ft->freeze_me (itt);
	}
	printf ("   frozen: state %d, playlist '%s'\n", (int)ft->freeze_state (), ft->playlist ()->name ().c_str ());
	check (ft->freeze_state () == Track::Frozen && ft->playlist ()->name () != playlist_before, "freeze: the track plays a rendered copy");
	ft->unfreeze ();
	check (ft->freeze_state () == Track::UnFrozen && ft->playlist ()->name () == playlist_before && ft->playlist ()->region_list ()->size () == regions_before, "unfreeze: the original regions and plug-ins are back");
	(void)fr;

	/* ---- M2: master loudness while playing ---- */
	Session::MasterLoudness l0 = s->master_loudness ();
	check (!s->master_loudness_metering () && l0.integrated <= -200, "loudness: off until asked for");
	/* only a 1 kHz tone at -20 dBFS plays; centre panning (-3 dB per side) */
	for (auto const& r : *s->get_routes ()) {
		if (!r->is_master () && !r->is_monitor ()) {
			r->mute_control ()->set_value (1, PBD::Controllable::NoGroup);
		}
	}
	string const tone = write_wav (dir, "tone", 8, [] (double t) { return float (0.1 * sin (2 * M_PI * 1000 * t)); });
	std::shared_ptr<AudioTrack> tt;
	add_track (*s, "Tone", tone, &tt);
	s->set_master_loudness_metering (true);
	/* let the new track's disk reader fill before playing */
	s->request_locate (0, false, MustStop);
	Glib::usleep (1000000);
	s->request_locate (0, false, MustRoll);
	Glib::usleep (4000000);
	Session::MasterLoudness const l1 = s->master_loudness ();
	s->request_stop ();
	Glib::usleep (300000);
	/* two channels at 0.1/sqrt(2) peak; at 1 kHz the K-weighting (+0.69 dB)
	 * cancels the -0.691 of the LUFS formula */
	double const expect = 10 * log10 (2 * 0.5 * pow (0.1 * M_SQRT1_2, 2));
	printf ("   tone: momentary %.2f, integrated %.2f LUFS (expected %.2f), true peak %.2f dBTP\n", l1.momentary, l1.integrated, expect, l1.true_peak);
	check (fabs (l1.integrated - expect) < 0.5 && fabs (l1.momentary - expect) < 0.5, "loudness: a steady tone measures its known loudness");
	check (fabs (l1.true_peak - 20 * log10 (0.1 * M_SQRT1_2)) < 0.5, "loudness: true peak of the tone");

	/* a new playback run starts a new measurement: 6 dB quieter */
	tt->gain_control ()->set_value (dB_to_coefficient (-6), PBD::Controllable::NoGroup);
	s->request_locate (0, false, MustRoll);
	Glib::usleep (3000000);
	Session::MasterLoudness const l2 = s->master_loudness ();
	s->request_stop ();
	Glib::usleep (300000);
	printf ("   6 dB quieter: integrated %.2f LUFS\n", l2.integrated);
	check (fabs (l2.integrated - (expect - 6)) < 0.5, "loudness: a new playback run measures afresh");
	s->set_master_loudness_metering (false);

	/* ---- M3: the Final Mix model ---- */
	check (FinalMix::guess_role ("Kick In") == FinalMix::Drums && FinalMix::guess_role ("Bass DI") == FinalMix::Bass
	       && FinalMix::guess_role ("Lead Guitar") == FinalMix::Guitars && FinalMix::guess_role ("Lead Vox") == FinalMix::Vocals
	       && FinalMix::guess_role ("BV Choir") == FinalMix::BackingVocals && FinalMix::guess_role ("Piano") == FinalMix::Keys
	       && FinalMix::guess_role ("TRACK07") == FinalMix::Other,
	       "roles are guessed from track names");

	FinalMix fm (*s);
	check (fm.tracks ().size () == 4, string_compose ("the window lists the 4 tracks (%1)", fm.tracks ().size ()));
	FinalMix::Settings st = fm.settings (tt);
	check (st.neutral () && st.role == FinalMix::Other, "a new track has neutral settings");
	st.high_pass     = true;
	st.hp_freq       = 120;
	st.brightness_db = 3;
	st.punch         = 0.5;
	st.space         = 0.5;
	fm.set_settings (tt, st);

	vector<string> names;
	size_t         fader_pos = 0;
	tt->foreach_processor ([&names, &fader_pos, &tt] (std::weak_ptr<Processor> w) {
		if (auto p = w.lock ()) {
			if (p == tt->amp ()) {
				fader_pos = names.size ();
			}
			names.push_back (p->name ());
		}
	});
	string chain;
	for (auto const& n : names) {
		chain += n + " | ";
	}
	printf ("   chain: %s\n", chain.c_str ());
	auto pos = [&names] (string const& n) { return std::find (names.begin (), names.end (), n) - names.begin (); };
	check (pos (FinalMix::eq_name) < pos (FinalMix::punch_name) && pos (FinalMix::punch_name) < (long)fader_pos,
	       "EQ, then Punch, both before the fader");
	std::shared_ptr<Route> bus = fm.space_bus ();
	std::shared_ptr<Send>  snd = bus ? tt->internal_send_for (bus) : std::shared_ptr<Send> ();
	check (bus && snd && snd->active () && fabs (accurate_coefficient_to_dB (snd->gain_control ()->get_value ()) + 12) < 0.1,
	       "Space: a send at -12 dB to the reverb bus");
	FinalMix::Settings back = fm.settings (tt);
	check (back.high_pass && back.hp_freq == 120 && back.brightness_db == 3 && back.punch == 0.5f && back.space == 0.5f, "settings read back");

	fm.set_fx_enabled (false);
	bool all_off = !snd->active ();
	tt->foreach_processor ([&all_off] (std::weak_ptr<Processor> w) {
		auto p = w.lock ();
		if (p && (p->name () == FinalMix::eq_name || p->name () == FinalMix::punch_name)) {
			all_off &= !p->active ();
		}
	});
	check (all_off && !fm.fx_enabled (), "FX off: the tools are bypassed");
	fm.set_fx_enabled (true);
	check (snd->active (), "FX on again");

	fm.reset (tt);
	check (fm.settings (tt).neutral () && !snd->active () && tt->gain_control ()->get_value () == GAIN_COEFF_UNITY, "reset: neutral, fader at 0 dB");

	/* removing a track from the mix mutes it; undo and redo */
	/* mute changes reach the route in the next process cycle */
	auto settle = [] () { Glib::usleep (200000); };
	std::shared_ptr<Route> loud_r = s->route_by_name ("Loud");
	loud_r->mute_control ()->set_value (0, PBD::Controllable::NoGroup);
	settle ();
	fm.set_excluded (loud_r, true);
	settle ();
	check (fm.excluded (loud_r) && loud_r->mute_control ()->muted_by_self (), "removed from the mix: excluded and muted");
	s->undo (1);
	settle ();
	check (!fm.excluded (loud_r) && !loud_r->mute_control ()->muted_by_self (), "undo: back in the mix, unmuted");
	s->redo (1);
	settle ();
	check (fm.excluded (loud_r) && loud_r->mute_control ()->muted_by_self (), "redo: removed again");
	fm.set_excluded (loud_r, false);
	settle ();
	check (!fm.excluded (loud_r) && !loud_r->mute_control ()->muted_by_self (), "brought back: its earlier mute state returns");

	/* the master limiter */
	fm.set_limiter_ceiling (-1.5);
	std::shared_ptr<Processor> last_plugin;
	s->master_out ()->foreach_processor ([&last_plugin] (std::weak_ptr<Processor> w) {
		if (auto p = std::dynamic_pointer_cast<PluginInsert> (w.lock ())) {
			last_plugin = p;
		}
	});
	check (last_plugin && last_plugin->name () == FinalMix::limiter_name && fabs (fm.limiter_ceiling () + 1.5) < 0.01, "master limiter made, ceiling -1.5 dBTP");
	fm.set_target (FinalMix::Loud);
	check (fm.target_lufs () == -9, "loudness target: Loud is -9 LUFS");

	s->save_state ("");
	printf ("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
	fflush (stdout);
	SessionUtils::unload_session (s);
	_exit (failures ? 1 : 0);
}

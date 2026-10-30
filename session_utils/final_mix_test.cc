/*
 * MusicDAWM WP7 + Final Mix test, headless.
 *
 * M1: the bundled x42 plug-ins (limiter, EQ with high-pass) load and do
 * their job, and freezing a track with a plug-in works and can be undone.
 * M2: the master bus loudness meter (LUFS, true peak) while playing.
 * M3: the Final Mix model: roles, per-track tools, FX on/off, removing a
 * track from the mix (undoable), the master limiter.
 * M4: Starting mix (balance from measured loudness, pan, effects per genre
 * and role), Randomise FX, both undoable.
 * M5: analysis, Before/After/Ref listening, Finalise Mix (export at the
 * loudness target). R1: rule-based matching to a reference song.
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

#include <glibmm/fileutils.h>
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

/* a stereo WAV of two functions of time */
template <typename F, typename G>
static string
write_wav2 (string const& dir, string const& name, double seconds, F fl, G fr)
{
	string const      path = Glib::build_filename (dir, name + ".wav");
	samplecnt_t const n    = (samplecnt_t)(seconds * SR);
	FILE*             fp   = fopen (path.c_str (), "wb");
	auto              u32  = [fp] (uint32_t v) { fwrite (&v, 4, 1, fp); };
	auto              u16  = [fp] (uint16_t v) { fwrite (&v, 2, 1, fp); };
	fwrite ("RIFF", 1, 4, fp);
	u32 (36 + n * 8);
	fwrite ("WAVEfmt ", 1, 8, fp);
	u32 (16);
	u16 (3);
	u16 (2);
	u32 (SR);
	u32 (SR * 8);
	u16 (8);
	u16 (32);
	fwrite ("data", 1, 4, fp);
	u32 (n * 8);
	for (samplecnt_t i = 0; i < n; ++i) {
		float const l = fl (i / double (SR));
		float const r = fr (i / double (SR));
		fwrite (&l, 4, 1, fp);
		fwrite (&r, 4, 1, fp);
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

	/* ---- M4: Starting mix ---- */
	for (auto const& r : fm.tracks ()) {
		fm.set_excluded (r, true);
	}
	std::shared_ptr<AudioTrack> vox, bass, gl, gr, kick;
	auto tone_at = [&dir] (string const& name, double amp) {
		return write_wav (dir, name, 4, [amp] (double t) { return float (amp * sin (2 * M_PI * 1000 * t)); });
	};
	add_track (*s, "Lead Vox", tone_at ("vox", 0.3), &vox);
	add_track (*s, "Bass DI", tone_at ("bass", 0.05), &bass);
	add_track (*s, "Guitar L", tone_at ("gl", 0.2), &gl);
	add_track (*s, "Guitar R", tone_at ("gr", 0.2), &gr);
	add_track (*s, "Kick", tone_at ("kick", 0.5), &kick);
	float const lv = fm.track_loudness (vox);
	printf ("   Lead Vox (sine 0.3 peak, mono) measures %.2f LUFS\n", lv);
	check (fabs (lv - 20 * log10 (0.3) + 3.01) < 0.3, "track loudness: as it will sound centred in the mix");

	FinalMix::Snapshot const before_sm = fm.snapshot ();
	int const n_set = fm.starting_mix (FinalMix::Rock);
	auto in_mix = [&fm] (std::shared_ptr<Route> r) { return fm.track_loudness (r) + accurate_coefficient_to_dB (r->gain_control ()->get_value ()); };
	printf ("   Rock: vox %.1f, kick %.1f, bass %.1f, guitar L %.1f LUFS in the mix\n", in_mix (vox), in_mix (kick), in_mix (bass), in_mix (gl));
	check (n_set == 5, "starting mix sets the 5 tracks in the mix");
	check (fabs (in_mix (vox) + 22) < 0.3 && fabs (in_mix (kick) + 24) < 0.3 && fabs (in_mix (bass) + 26) < 0.3
	       && fabs (in_mix (gl) - (-26 - 10 * log10 (2.))) < 0.3,
	       "balance: vocal -22, drums -2, bass -4, two guitars sharing -4 dB (Rock)");
	check (gl->pan_azimuth_control ()->get_value () < 0.2 && gr->pan_azimuth_control ()->get_value () > 0.8
	       && fabs (vox->pan_azimuth_control ()->get_value () - 0.5) < 0.01 && fabs (bass->pan_azimuth_control ()->get_value () - 0.5) < 0.01,
	       "pan: guitars L and R by name, vocal and bass centred");
	FinalMix::Settings const sv = fm.settings (vox);
	check (sv.high_pass && sv.hp_freq == 100 && sv.punch == 0.5f && sv.brightness_db == 1.5f && sv.space > 0, "effects: vocal high-pass 100 Hz, punch, brightness, space");

	float const g_vox = vox->gain_control ()->get_value ();
	s->undo (1);
	check (vox->gain_control ()->get_value () == before_sm[0].gain || fabs (accurate_coefficient_to_dB (vox->gain_control ()->get_value ())) < 0.01, "undo: the faders come back");
	check (fm.settings (vox).neutral (), "undo: the effects come back");
	s->redo (1);
	check (fabs (vox->gain_control ()->get_value () - g_vox) < 1e-6, "redo: the starting mix again");

	fm.randomise (7);
	FinalMix::Settings const r1 = fm.settings (vox);
	check (!(r1.hp_freq == sv.hp_freq && r1.punch == sv.punch && r1.brightness_db == sv.brightness_db)
	       && fabs (vox->gain_control ()->get_value () - g_vox) < 1e-6,
	       "randomise: new effect amounts, the faders stay");
	s->undo (1);
	check (fm.settings (vox).punch == sv.punch, "randomise: undo");

	/* ---- M5: analysis ---- */
	{
		auto sine = [] (double hz, double a) { return [hz, a] (double t) { return float (a * sin (2 * M_PI * hz * t)); }; };
		/* 1 kHz, 0.1 peak on both sides: -20 LUFS (the K-weighting is +0.69 dB at 1 kHz) */
		FinalMix::Analysis const l = FinalMix::analyse_file (write_wav2 (dir, "an0", 4, sine (1000, 0.1), sine (1000, 0.1)));
		check (l.valid && fabs (l.lufs + 20) < 0.3 && fabs (l.true_peak + 20) < 0.2, "analysis: loudness and true peak");
		FinalMix::Analysis const a = FinalMix::analyse_file (write_wav2 (dir, "an1", 4, sine (2000, 0.1), sine (2000, 0.1)));
		printf ("   2 kHz stereo: %.2f LUFS, peak %.2f, bands %.0f %.0f %.0f %.1f %.0f %.0f dB, width %.0f\n", a.lufs, a.true_peak,
		        a.band_db[0], a.band_db[1], a.band_db[2], a.band_db[3], a.band_db[4], a.band_db[5], a.width_db);
		check (a.valid && a.band_db[3] > -0.5 && a.band_db[1] < -20 && a.band_db[5] < -20 && a.width_db <= -200,
		       "analysis: the 2 kHz tone is in the mids band, no width");
		FinalMix::Analysis const w = FinalMix::analyse_file (write_wav2 (dir, "an2", 4, sine (2000, 0.1), [] (double) { return 0.f; }));
		check (w.valid && fabs (w.width_db) < 0.1, "analysis: one-sided stereo is as wide as it is centred (side = mid)");
	}

	auto idle = [] () { Glib::usleep (50000); };

	/* ---- M5: Before / After ---- */
	float const vox_after = vox->gain_control ()->get_value ();
	float const gl_pan    = gl->pan_azimuth_control ()->get_value ();
	check (fm.set_listen (FinalMix::Before), "Before");
	settle ();
	float const master_before = accurate_coefficient_to_dB (s->master_out ()->gain_control ()->get_value ());
	printf ("   Before: master %+.1f dB to match the mix\n", master_before);
	check (vox->gain_control ()->get_value () == GAIN_COEFF_UNITY && fabs (gl->pan_azimuth_control ()->get_value () - 0.5) < 0.01 && !fm.fx_enabled ()
	       && master_before < -3,
	       "Before: faders 0 dB, pan centre, effects off, the master turned down to the mix's loudness");
	check (fm.set_listen (FinalMix::After), "After");
	settle ();
	check (vox->gain_control ()->get_value () == vox_after && gl->pan_azimuth_control ()->get_value () == gl_pan && fm.fx_enabled ()
	       && s->master_out ()->gain_control ()->get_value () == GAIN_COEFF_UNITY,
	       "After: the mix is back exactly");

	/* ---- M5: Finalise Mix ---- */
	fm.set_target (FinalMix::Streaming);
	fm.set_limiter_ceiling (-1);
	string const out_dir = Glib::build_filename (dir, "final");
	g_mkdir_with_parents (out_dir.c_str (), 0755);
	FinalMix::Finalised const fin = fm.finalise (out_dir, "Song", false, idle);
	printf ("   finalised: %s, %.2f LUFS, %.2f dBTP %s\n", fin.wav.c_str (), fin.lufs, fin.true_peak, fin.error.c_str ());
	check (fin.ok && Glib::file_test (fin.wav, Glib::FILE_TEST_EXISTS) && fabs (fin.lufs + 14) < 0.7 && fin.true_peak <= -0.7,
	       "Finalise Mix: a WAV at -14 LUFS, true peaks under -1 dBTP");

	/* ---- R1: the reference ---- */
	/* louder, brighter and wider than the mix */
	string const refwav = write_wav2 (dir, "reference", 6,
	                                  [] (double t) { return float (0.25 * sin (2 * M_PI * 2000 * t) + 0.15 * sin (2 * M_PI * 10000 * t) + 0.2 * sin (2 * M_PI * 100 * t)); },
	                                  [] (double t) { return float (0.25 * sin (2 * M_PI * 2000 * t + 1) + 0.15 * sin (2 * M_PI * 10000 * t + 2) + 0.2 * sin (2 * M_PI * 100 * t)); });
	check (fm.set_reference (refwav) == 0, "reference imported");
	std::shared_ptr<Route> ref = fm.reference_track ();
	FinalMix::Analysis const ra = fm.reference_analysis ();
	printf ("   reference: %.1f LUFS, width %.1f dB\n", ra.lufs, ra.width_db);
	std::vector<std::shared_ptr<Route>> const mix_tracks = fm.tracks ();
	check (ref && ra.valid && std::none_of (mix_tracks.begin (), mix_tracks.end (), [&ref] (std::shared_ptr<Route> const& r) { return r == ref; }),
	       "the reference track is not one of the mix's tracks");
	check (ref && !ref->output ()->connected_to (s->master_out ()->input ()), "the reference does not go through the master bus");
	settle ();
	check (ref && ref->mute_control ()->muted_by_self (), "the reference is muted while listening to the mix");

	FinalMix::Analysis const ma = fm.analyse_mix (idle);
	printf ("   mix: %.1f LUFS, width %.1f dB\n", ma.lufs, ma.width_db);
	check (ma.valid && ma.lufs > -40 && ma.lufs < -5, "the mix analysed from an export");

	check (fm.set_listen (FinalMix::Ref), "Ref");
	settle ();
	float const ref_gain = accurate_coefficient_to_dB (ref->gain_control ()->get_value ());
	printf ("   Ref: reference at %+.1f dB to match the mix\n", ref_gain);
	check (ref->solo_control ()->self_soloed () && !ref->mute_control ()->muted_by_self () && fabs (ref_gain - (ma.lufs - ra.lufs)) < 0.2,
	       "Ref: the reference alone, at the mix's loudness");
	fm.set_listen (FinalMix::After);
	settle ();
	check (!ref->solo_control ()->self_soloed () && ref->mute_control ()->muted_by_self (), "back to the mix: the reference is silent");

	FinalMix::Match const m = fm.compute_match (ma, ra);
	printf ("   match: target %.1f LUFS, EQ %+.1f %+.1f %+.1f %+.1f %+.1f %+.1f dB\n%s", m.target_lufs, m.eq_db[0], m.eq_db[1], m.eq_db[2], m.eq_db[3], m.eq_db[4], m.eq_db[5], m.report.c_str ());
	check (m.valid && fabs (m.target_lufs - roundf (ra.lufs * 2) / 2) < 0.01 && m.eq_db[5] > 0 && m.report.find ("wider") != string::npos,
	       "R1: target from the reference, more air, advice on width");
	fm.apply_match (m);
	std::shared_ptr<Processor> match_eq, mlim;
	size_t                     i_eq = 0, i_lim = 0, k = 0;
	s->master_out ()->foreach_processor ([&] (std::weak_ptr<Processor> w) {
		auto p = w.lock ();
		if (p && p->name () == FinalMix::match_eq_name) {
			match_eq = p;
			i_eq     = k;
		}
		if (p && p->name () == FinalMix::limiter_name) {
			mlim  = p;
			i_lim = k;
		}
		++k;
	});
	check (match_eq && mlim && i_eq < i_lim && match_eq->active () && fm.target () == FinalMix::Custom && fm.target_lufs () == m.target_lufs,
	       "R1 applied: master match EQ before the limiter, target set");
	fm.clear_match ();
	check (!match_eq->active () && fm.target () == FinalMix::Streaming, "R1 cleared: EQ off, the earlier target back");

	s->save_state ("");
	printf ("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
	fflush (stdout);
	SessionUtils::unload_session (s);
	_exit (failures ? 1 : 0);
}

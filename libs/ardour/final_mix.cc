/*
 * MusicDAWM WP7: the Final Mix model (see ardour/final_mix.h).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <algorithm>
#include <cmath>
#include <random>

#include <glib.h>

#include "pbd/compose.h"
#include "pbd/xml++.h"

#include "ardour/amp.h"
#include "ardour/audio_track.h"
#include "ardour/audioplaylist.h"
#include "ardour/dB.h"
#include "ardour/final_mix.h"
#include "ardour/internal_send.h"
#include "ardour/lufs_meter.h"
#include "ardour/lv2_plugin.h"
#include "ardour/mute_control.h"
#include "ardour/plugin_insert.h"
#include "ardour/plugin_manager.h"
#include "ardour/route.h"
#include "ardour/session.h"
#include "ardour/track.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using std::shared_ptr;
using std::string;

char const* const FinalMix::eq_name        = X_("Final Mix EQ");
char const* const FinalMix::punch_name     = X_("Final Mix Punch");
char const* const FinalMix::limiter_name   = X_("Final Mix Limiter");
char const* const FinalMix::space_bus_name = X_("Final Mix Space");

static char const* const node_name = X_("MusicDAWMFinalMix");

PBD::Signal<void()> FinalMix::ExclusionChanged;
PBD::Signal<void()> FinalMix::SettingsChanged;

namespace {

shared_ptr<AutomationControl>
param (shared_ptr<PluginInsert> pi, char const* symbol)
{
	if (!pi) {
		return shared_ptr<AutomationControl> ();
	}
	shared_ptr<LV2Plugin> lv2 = std::dynamic_pointer_cast<LV2Plugin> (pi->plugin ());
	if (!lv2) {
		return shared_ptr<AutomationControl> ();
	}
	uint32_t const port = lv2->port_index (symbol);
	if (port == (uint32_t)-1) {
		return shared_ptr<AutomationControl> ();
	}
	return std::dynamic_pointer_cast<AutomationControl> (pi->control (Evoral::Parameter (PluginAutomation, 0, port)));
}

void
set_param (shared_ptr<PluginInsert> pi, char const* symbol, float v)
{
	if (shared_ptr<AutomationControl> c = param (pi, symbol)) {
		c->set_value (v, PBD::Controllable::NoGroup);
	}
}

void
set_active (shared_ptr<Processor> p, bool yn)
{
	if (!p) {
		return;
	}
	if (yn) {
		p->activate ();
	} else {
		p->deactivate ();
	}
}

/* the channels at the route's fader: what a pre-fader plugin processes */
bool
stereo_at_fader (shared_ptr<Route> r)
{
	return r->amp ()->input_streams ().n_audio () >= 2;
}

} // namespace

FinalMix::FinalMix (Session& s)
	: _session (s)
{
}

string
FinalMix::genre_name (Genre g)
{
	switch (g) {
		case Pop:
			return _("Pop");
		case Rock:
			return _("Rock");
		case Acoustic:
			return _("Acoustic / Folk");
		case Electronic:
			return _("Electronic");
		case HipHop:
			return _("Hip-hop");
		case Metal:
			return _("Metal");
		default:
			return _("Jazz");
	}
}

string
FinalMix::role_name (Role r)
{
	switch (r) {
		case Drums:
			return _("Drums");
		case Bass:
			return _("Bass");
		case Guitars:
			return _("Guitars");
		case Keys:
			return _("Keys");
		case Vocals:
			return _("Vocals");
		case BackingVocals:
			return _("Backing vocals");
		default:
			return _("Other");
	}
}

FinalMix::Role
FinalMix::guess_role (string const& track_name)
{
	gchar* l = g_utf8_strdown (track_name.c_str (), -1);
	string const n (l);
	g_free (l);
	auto any = [&n] (std::initializer_list<char const*> words) {
		for (char const* w : words) {
			if (n.find (w) != string::npos) {
				return true;
			}
		}
		return false;
	};
	/* the order matters: "bass drum" is drums, "lead guitar" is a guitar */
	if (any ({ "backing", "bv", "choir", "chœur", "choeur", "harmon", "double" })) {
		return BackingVocals;
	}
	if (any ({ "drum", "kick", "snare", "hat", "tom", "overhead", "cymbal", "perc", "batterie", "grosse caisse", "caisse claire" })) {
		return Drums;
	}
	if (any ({ "bass", "basse" })) {
		return Bass;
	}
	if (any ({ "guitar", "gtr", "guit", "git" })) {
		return Guitars;
	}
	if (any ({ "key", "piano", "synth", "organ", "orgue", "pad", "rhodes", "clavier" })) {
		return Keys;
	}
	if (any ({ "vox", "vocal", "voc", "voice", "voix", "lead", "sing", "chant" })) {
		return Vocals;
	}
	return Other;
}

float
FinalMix::default_hp_freq (Role r)
{
	switch (r) {
		case Drums:
			return 30;
		case Bass:
			return 35;
		case Guitars:
			return 90;
		case Keys:
			return 70;
		case Vocals:
			return 100;
		case BackingVocals:
			return 150;
		default:
			return 80;
	}
}

std::vector<shared_ptr<Route>>
FinalMix::tracks () const
{
	std::vector<shared_ptr<Route>> v;
	for (auto const& r : *_session.get_routes ()) {
		if (std::dynamic_pointer_cast<Track> (r) && !r->is_private_route ()) {
			v.push_back (r);
		}
	}
	std::sort (v.begin (), v.end (), [] (shared_ptr<Route> const& a, shared_ptr<Route> const& b) {
		return a->presentation_info ().order () < b->presentation_info ().order ();
	});
	return v;
}

/* ---- removing a track from the mix ---- */

bool
FinalMix::excluded (shared_ptr<Route> r) const
{
	XMLNode* n = r->extra_xml (node_name);
	bool     x = false;
	return n && n->get_property (X_("excluded"), x) && x;
}

void
FinalMix::store_excluded (Session&, shared_ptr<Route> r, bool yn)
{
	XMLNode* n         = r->extra_xml (node_name, true);
	bool     was_muted = false;
	if (yn) {
		n->set_property (X_("was-muted"), r->mute_control ()->muted_by_self ());
		r->mute_control ()->set_value (1, PBD::Controllable::NoGroup);
	} else {
		n->get_property (X_("was-muted"), was_muted);
		r->mute_control ()->set_value (was_muted ? 1 : 0, PBD::Controllable::NoGroup);
	}
	n->set_property (X_("excluded"), yn);
	ExclusionChanged (); /* EMIT SIGNAL */
}

void
FinalMix::set_excluded (shared_ptr<Route> r, bool yn)
{
	if (excluded (r) == yn) {
		return;
	}
	ExcludeCommand* cmd = new ExcludeCommand (_session, r, yn);
	_session.begin_reversible_command (yn ? _("remove from the final mix") : _("bring back into the final mix"));
	(*cmd) ();
	_session.commit_reversible_command (cmd);
	_session.set_dirty ();
}

FinalMix::ExcludeCommand::ExcludeCommand (Session& s, shared_ptr<Route> r, bool yn)
	: _session (s)
	, _route (r->id ())
	, _exclude (yn)
{
	set_name (X_("FinalMixExcludeCommand"));
}

FinalMix::ExcludeCommand::ExcludeCommand (Session& s, XMLNode const& node)
	: _session (s)
	, _exclude (false)
{
	set_name (X_("FinalMixExcludeCommand"));
	string id;
	if (node.get_property (X_("route"), id)) {
		_route = PBD::ID (id);
	}
	node.get_property (X_("exclude"), _exclude);
}

void
FinalMix::ExcludeCommand::apply (bool yn) const
{
	if (shared_ptr<Route> r = _session.route_by_id (_route)) {
		store_excluded (_session, r, yn);
	}
}

void
FinalMix::ExcludeCommand::operator() ()
{
	apply (_exclude);
}

void
FinalMix::ExcludeCommand::undo ()
{
	apply (!_exclude);
}

XMLNode&
FinalMix::ExcludeCommand::get_state () const
{
	XMLNode* n = new XMLNode (X_("FinalMixExcludeCommand"));
	n->set_property (X_("route"), _route.to_s ());
	n->set_property (X_("exclude"), _exclude);
	return *n;
}

/* ---- settings ---- */

FinalMix::Settings
FinalMix::settings (shared_ptr<Route> r) const
{
	Settings s;
	XMLNode* n = r->extra_xml (node_name);
	int      role;
	if (n && n->get_property (X_("role"), role) && role >= 0 && role < NumRoles) {
		s.role = (Role)role;
	} else {
		s.role = guess_role (r->name ());
	}
	s.hp_freq = default_hp_freq (s.role);
	if (n) {
		n->get_property (X_("high-pass"), s.high_pass);
		n->get_property (X_("hp-freq"), s.hp_freq);
		n->get_property (X_("brightness"), s.brightness_db);
		n->get_property (X_("punch"), s.punch);
		n->get_property (X_("space"), s.space);
	}
	return s;
}

shared_ptr<PluginInsert>
FinalMix::find_plugin (shared_ptr<Route> r, char const* name) const
{
	shared_ptr<PluginInsert> found;
	r->foreach_processor ([&found, name] (std::weak_ptr<Processor> w) {
		shared_ptr<PluginInsert> pi = std::dynamic_pointer_cast<PluginInsert> (w.lock ());
		if (pi && !found && pi->name () == name) {
			found = pi;
		}
	});
	return found;
}

shared_ptr<PluginInsert>
FinalMix::make_plugin (shared_ptr<Route> r, char const* name, string const& uri, bool pre_fader)
{
	PluginPtr p;
	for (auto const& i : PluginManager::instance ().lv2_plugin_info ()) {
		if (i->unique_id == uri) {
			p = i->load (_session);
			break;
		}
	}
	if (!p) {
		return shared_ptr<PluginInsert> ();
	}
	shared_ptr<PluginInsert> pi (new PluginInsert (_session, *r, p));
	pi->set_name (name);
	shared_ptr<Processor> before;
	if (pre_fader) {
		/* the EQ goes before the compressor, both just before the fader */
		before = (string (name) == eq_name) ? find_plugin (r, punch_name) : shared_ptr<PluginInsert> ();
		if (!before) {
			before = r->amp ();
		}
	} else {
		before = r->before_processor_for_placement (PostFader);
	}
	if (r->add_processor (pi, before)) {
		return shared_ptr<PluginInsert> ();
	}
	return pi;
}

shared_ptr<Route>
FinalMix::space_bus () const
{
	shared_ptr<Route> r = _session.route_by_name (space_bus_name);
	return (r && !std::dynamic_pointer_cast<Track> (r)) ? r : shared_ptr<Route> ();
}

shared_ptr<Route>
FinalMix::make_space_bus ()
{
	if (shared_ptr<Route> b = space_bus ()) {
		return b;
	}
	RouteList rl = _session.new_audio_route (2, 2, 0, 1, space_bus_name, PresentationInfo::AudioBus, PresentationInfo::max_order);
	if (rl.empty ()) {
		return shared_ptr<Route> ();
	}
	shared_ptr<Route>        b  = rl.front ();
	shared_ptr<PluginInsert> rv = make_plugin (b, "Final Mix Reverb", X_("urn:ardour:a-reverb"), true);
	set_param (rv, "blend", 1.0); /* the bus is all reverb; the tracks stay dry */
	set_param (rv, "roomsz", 0.6);
	return b;
}

void
FinalMix::set_settings (shared_ptr<Route> r, Settings const& s)
{
	XMLNode* n = r->extra_xml (node_name, true);
	n->set_property (X_("role"), (int)s.role);
	n->set_property (X_("high-pass"), s.high_pass);
	n->set_property (X_("hp-freq"), s.hp_freq);
	n->set_property (X_("brightness"), s.brightness_db);
	n->set_property (X_("punch"), s.punch);
	n->set_property (X_("space"), s.space);

	bool const fx = fx_enabled ();

	/* EQ: high-pass and brightness */
	bool const               want_eq = s.high_pass || s.brightness_db != 0;
	shared_ptr<PluginInsert> eq      = find_plugin (r, eq_name);
	if (want_eq && !eq) {
		eq = make_plugin (r, eq_name, stereo_at_fader (r) ? X_("http://gareus.org/oss/lv2/fil4#stereo") : X_("http://gareus.org/oss/lv2/fil4#mono"), true);
	}
	if (eq) {
		set_param (eq, "HighPass", s.high_pass ? 1 : 0);
		set_param (eq, "HPfreq", s.hp_freq);
		set_param (eq, "HPQ", 0.7);
		set_param (eq, "HSsec", 1);
		set_param (eq, "HSfreq", 6000);
		set_param (eq, "HSq", 0.7);
		set_param (eq, "HSgain", s.brightness_db);
		set_param (eq, "enable", 1);
		set_active (eq, want_eq && fx);
	}

	/* punch: a gentle compressor whose attack lets the hits through */
	bool const               want_punch = s.punch > 0;
	shared_ptr<PluginInsert> comp       = find_plugin (r, punch_name);
	if (want_punch && !comp) {
		comp = make_plugin (r, punch_name, stereo_at_fader (r) ? X_("urn:ardour:a-comp#stereo") : X_("urn:ardour:a-comp"), true);
	}
	if (comp) {
		set_param (comp, "att", 15);
		set_param (comp, "rel", 80);
		set_param (comp, "kn", 3);
		set_param (comp, "rat", 1 + 3 * s.punch);
		set_param (comp, "thr", -6 - 18 * s.punch);
		set_param (comp, "mak", 4 * s.punch);
		set_param (comp, "enable", 1);
		set_active (comp, want_punch && fx);
	}

	/* space: a post-fader send to the reverb bus */
	shared_ptr<Route> bus = s.space > 0 ? make_space_bus () : space_bus ();
	if (bus) {
		if (s.space > 0 && !r->internal_send_for (bus)) {
			r->add_aux_send (bus, r->before_processor_for_placement (PostFader));
		}
		if (shared_ptr<Send> send = r->internal_send_for (bus)) {
			send->gain_control ()->set_value (dB_to_coefficient (-24 + 24 * s.space), PBD::Controllable::NoGroup);
			set_active (send, s.space > 0 && fx);
		}
	}
	_session.set_dirty ();
}

void
FinalMix::reset (shared_ptr<Route> r)
{
	Settings s;
	s.role    = settings (r).role;
	s.hp_freq = default_hp_freq (s.role);
	set_settings (r, s);
	r->gain_control ()->set_value (GAIN_COEFF_UNITY, PBD::Controllable::NoGroup);
	if (shared_ptr<AutomationControl> pan = r->pan_azimuth_control ()) {
		pan->set_value (0.5, PBD::Controllable::NoGroup);
	}
}

/* ---- window-wide settings ---- */

XMLNode*
FinalMix::session_node () const
{
	return _session.extra_xml (node_name, true);
}

bool
FinalMix::fx_enabled () const
{
	bool on = true;
	session_node ()->get_property (X_("fx"), on);
	return on;
}

void
FinalMix::set_fx_enabled (bool yn)
{
	session_node ()->set_property (X_("fx"), yn);
	for (auto const& r : tracks ()) {
		if (r->extra_xml (node_name)) {
			set_settings (r, settings (r));
		}
	}
}

FinalMix::Target
FinalMix::target () const
{
	int t = Streaming;
	session_node ()->get_property (X_("target"), t);
	return (t >= Streaming && t <= Custom) ? (Target)t : Streaming;
}

float
FinalMix::target_lufs () const
{
	switch (target ()) {
		case Loud:
			return -9;
		case Custom: {
			float v = -14;
			session_node ()->get_property (X_("custom-lufs"), v);
			return v;
		}
		default:
			return -14;
	}
}

void
FinalMix::set_target (Target t, float custom_lufs)
{
	session_node ()->set_property (X_("target"), (int)t);
	if (t == Custom) {
		session_node ()->set_property (X_("custom-lufs"), custom_lufs);
	}
	_session.set_dirty ();
}

shared_ptr<PluginInsert>
FinalMix::limiter (bool create)
{
	shared_ptr<Route> m = _session.master_out ();
	if (!m) {
		return shared_ptr<PluginInsert> ();
	}
	shared_ptr<PluginInsert> l = find_plugin (m, limiter_name);
	if (!l && create) {
		bool const stereo = m->n_outputs ().n_audio () >= 2;
		l = make_plugin (m, limiter_name, stereo ? X_("http://gareus.org/oss/lv2/dpl#stereo") : X_("http://gareus.org/oss/lv2/dpl#mono"), false);
		set_param (l, "gain", 0);
		set_param (l, "truepeak", 1);
		set_param (l, "threshold", -1);
		set_param (l, "enable", 1);
	}
	return l;
}

float
FinalMix::limiter_ceiling () const
{
	shared_ptr<PluginInsert> l = const_cast<FinalMix*> (this)->limiter (false);
	if (shared_ptr<AutomationControl> c = param (l, "threshold")) {
		return c->get_value ();
	}
	return -1;
}

void
FinalMix::set_limiter_ceiling (float db)
{
	set_param (limiter (true), "threshold", std::min (0.f, std::max (-10.f, db)));
	_session.set_dirty ();
}

/* ---- Starting mix ---- */

namespace {

/* the rules, per genre (rows: Pop, Rock, Acoustic, Electronic, Hip-hop,
 * Metal, Jazz) and role (columns: Drums, Bass, Guitars, Keys, Vocals,
 * Backing vocals, Other) */

/* loudness of the role relative to the lead vocal, dB */
float const level[FinalMix::NumGenres][FinalMix::NumRoles] = {
	{ -3, -5, -8, -7, 0, -7, -9 },
	{ -2, -4, -4, -8, 0, -7, -9 },
	{ -6, -6, -3, -6, 0, -6, -8 },
	{ -1, -2, -8, -4, -1, -7, -6 },
	{ -1, -1, -9, -6, 0, -7, -8 },
	{ -1, -4, -2, -8, -1, -7, -9 },
	{ -5, -3, -4, -2, 0, -6, -6 },
};

float const punch[FinalMix::NumGenres][FinalMix::NumRoles] = {
	{ .5, .5, .3, .2, .5, .4, .2 },
	{ .6, .5, .3, .2, .5, .4, .2 },
	{ .2, .3, .2, .1, .3, .3, .1 },
	{ .5, .5, .2, .3, .5, .4, .2 },
	{ .6, .6, .2, .2, .6, .4, .2 },
	{ .7, .6, .4, .2, .5, .4, .2 },
	{ .1, .2, .1, .1, .3, .2, .1 },
};

float const brightness[FinalMix::NumGenres][FinalMix::NumRoles] = {
	{ 1, 0, .5, .5, 2, 1.5, 0 },
	{ 1.5, 0, .5, .5, 1.5, 1.5, 0 },
	{ .5, 0, 1, .5, 1.5, 1, 0 },
	{ 1, 0, .5, 1, 2.5, 2, .5 },
	{ 1, 0, .5, .5, 2.5, 2, 0 },
	{ 1.5, 0, 0, .5, 1.5, 1.5, 0 },
	{ 0, 0, 0, 0, .5, .5, 0 },
};

/* reverb amount per role, scaled per genre */
float const space_role[FinalMix::NumRoles]    = { .1, 0, .2, .25, .3, .35, .2 };
float const space_genre[FinalMix::NumGenres]  = { 1, .8, 1.1, .9, .6, .6, 1.2 };

/* the lead vocal's loudness in the mix, before the limiter (LUFS) */
float const vocal_lufs = -22;

/* a pan hint in the track name: -1 left, 1 right, 0 none */
int
pan_hint (string const& name)
{
	gchar* l = g_utf8_strdown (name.c_str (), -1);
	string n (l);
	g_free (l);
	if (n.find ("left") != string::npos || n.find ("gauche") != string::npos) {
		return -1;
	}
	if (n.find ("right") != string::npos || n.find ("droite") != string::npos) {
		return 1;
	}
	if (n.size () >= 2 && n[n.size () - 2] == ' ') {
		if (n.back () == 'l') {
			return -1;
		}
		if (n.back () == 'r') {
			return 1;
		}
	}
	return 0;
}

/* -1 .. 1 to an azimuth */
float
azimuth (float p)
{
	return std::max (0.f, std::min (1.f, (p + 1) / 2));
}

} // namespace

FinalMix::Genre
FinalMix::genre () const
{
	int g = Pop;
	session_node ()->get_property (X_("genre"), g);
	return (g >= 0 && g < NumGenres) ? (Genre)g : Pop;
}

float
FinalMix::track_loudness (shared_ptr<Route> r) const
{
	shared_ptr<AudioTrack>    t  = std::dynamic_pointer_cast<AudioTrack> (r);
	shared_ptr<AudioPlaylist> pl = t ? std::dynamic_pointer_cast<AudioPlaylist> (t->playlist ()) : shared_ptr<AudioPlaylist> ();
	/* a copy of the list: keep it while iterating */
	std::shared_ptr<RegionList> const regions = pl ? pl->region_list () : std::shared_ptr<RegionList> ();
	if (!regions || regions->empty ()) {
		return -200;
	}
	uint32_t const nch = std::max<uint32_t> (1, std::min<uint32_t> (2, t->n_inputs ().n_audio ()));
	samplepos_t    pos = regions->front ()->position ().samples ();
	samplepos_t    end = 0;
	for (auto const& reg : *regions) {
		pos = std::min (pos, reg->position ().samples ());
		end = std::max (end, reg->end_position ().samples ());
	}
	LUFSMeter                meter (_session.nominal_sample_rate (), nch);
	samplecnt_t const        block = 65536;
	std::vector<Sample>      buf[2] = { std::vector<Sample> (block), std::vector<Sample> (block) };
	std::vector<Sample>      mix (block);
	std::vector<float>       gain (block);
	while (pos < end) {
		samplecnt_t const n = std::min (block, end - pos);
		float const*      data[2];
		for (uint32_t c = 0; c < nch; ++c) {
			std::fill (buf[c].begin (), buf[c].begin () + n, 0.f);
			pl->read (buf[c].data (), mix.data (), gain.data (), timepos_t (pos), timecnt_t (n), c);
			data[c] = buf[c].data ();
		}
		meter.run (data, n);
		pos += n;
	}
	float const l = meter.integrated_loudness ();
	if (l <= -200) {
		return -200;
	}
	/* LUFSMeter counts a mono signal twice (dual mono); at the centre of a
	 * stereo mix it gets half of that power on each side */
	return nch == 1 ? l - 3.0103f : l;
}

FinalMix::Snapshot
FinalMix::snapshot () const
{
	Snapshot v;
	for (auto const& r : tracks ()) {
		if (excluded (r)) {
			continue;
		}
		TrackState ts;
		ts.route    = r->id ();
		ts.gain     = r->gain_control ()->get_value ();
		ts.pan      = r->pan_azimuth_control () ? r->pan_azimuth_control ()->get_value () : -1;
		ts.settings = settings (r);
		v.push_back (ts);
	}
	return v;
}

void
FinalMix::restore (Snapshot const& v)
{
	for (auto const& ts : v) {
		shared_ptr<Route> r = _session.route_by_id (ts.route);
		if (!r) {
			continue;
		}
		r->gain_control ()->set_value (ts.gain, PBD::Controllable::NoGroup);
		if (ts.pan >= 0 && r->pan_azimuth_control ()) {
			r->pan_azimuth_control ()->set_value (ts.pan, PBD::Controllable::NoGroup);
		}
		set_settings (r, ts.settings);
	}
	SettingsChanged (); /* EMIT SIGNAL */
}

void
FinalMix::commit_snapshot (string const& name, Snapshot const& before)
{
	_session.begin_reversible_command (name);
	_session.commit_reversible_command (new SnapshotCommand (*this, name, before, snapshot ()));
	_session.set_dirty ();
	SettingsChanged (); /* EMIT SIGNAL */
}

int
FinalMix::starting_mix (Genre g)
{
	Snapshot const before = snapshot ();
	session_node ()->set_property (X_("genre"), (int)g);

	/* the tracks of each role, to share the role's level and spread them */
	std::vector<shared_ptr<Route>> by_role[NumRoles];
	for (auto const& r : tracks ()) {
		if (!excluded (r)) {
			by_role[settings (r).role].push_back (r);
		}
	}

	int n_set = 0;
	for (int role = 0; role < NumRoles; ++role) {
		std::vector<shared_ptr<Route>> const& rs = by_role[role];
		if (rs.empty ()) {
			continue;
		}
		/* tracks of one role share its loudness */
		float const each = vocal_lufs + level[g][role] - 10 * log10f ((float)rs.size ());
		int         side = 0; /* spreading: L, R, L, R ... */
		for (size_t k = 0; k < rs.size (); ++k) {
			shared_ptr<Route> r = rs[k];

			float const l = track_loudness (r);
			if (l > -200) {
				float const db = std::max (-40.f, std::min (12.f, each - l));
				r->gain_control ()->set_value (dB_to_coefficient (db), PBD::Controllable::NoGroup);
			}

			if (shared_ptr<AutomationControl> pan = r->pan_azimuth_control ()) {
				float      p    = 0;
				int const  hint = pan_hint (r->name ());
				if (hint) {
					p = 0.8f * hint;
				} else if (role == Drums || role == Bass || role == Vocals) {
					p = 0; /* the anchors stay in the centre */
				} else if (rs.size () == 1) {
					/* a single guitar, keyboard ... makes room for the vocal */
					p = (role == Keys || role == Other) ? 0.25f : -0.25f;
				} else {
					float const spread[] = { 0.5f, 0.3f, 0.7f, 0.4f, 0.6f };
					p = spread[(k / 2) % 5] * (side++ % 2 ? 1 : -1);
				}
				pan->set_value (azimuth (p), PBD::Controllable::NoGroup);
			}

			Settings s      = settings (r);
			s.high_pass     = true;
			s.hp_freq       = default_hp_freq (s.role);
			s.brightness_db = brightness[g][role];
			s.punch         = punch[g][role];
			s.space         = std::min (1.f, space_role[role] * space_genre[g]);
			set_settings (r, s);
			++n_set;
		}
	}
	commit_snapshot (string_compose (_("starting mix (%1)"), genre_name (g)), before);
	return n_set;
}

void
FinalMix::randomise (uint32_t seed)
{
	Snapshot const                        before = snapshot ();
	Genre const                           g      = genre ();
	std::mt19937                          rng (seed);
	std::uniform_real_distribution<float> u (-1, 1);
	for (auto const& r : tracks ()) {
		if (excluded (r)) {
			continue;
		}
		Settings s      = settings (r);
		int const role  = s.role;
		s.high_pass     = true;
		s.hp_freq       = default_hp_freq (s.role) * powf (1.25f, u (rng));
		s.brightness_db = std::max (-6.f, std::min (6.f, brightness[g][role] + 1.5f * u (rng)));
		s.punch         = std::max (0.f, std::min (1.f, punch[g][role] + 0.15f * u (rng)));
		s.space         = std::max (0.f, std::min (1.f, space_role[role] * space_genre[g] + 0.1f * u (rng)));
		/* the panel shows whole steps */
		s.hp_freq       = roundf (s.hp_freq);
		s.brightness_db = roundf (s.brightness_db * 2) / 2;
		s.punch         = roundf (s.punch * 100) / 100;
		s.space         = roundf (s.space * 100) / 100;
		set_settings (r, s);
	}
	commit_snapshot (_("randomise FX"), before);
}

void
FinalMix::reset_all ()
{
	Snapshot const before = snapshot ();
	for (auto const& r : tracks ()) {
		if (!excluded (r)) {
			reset (r);
		}
	}
	commit_snapshot (_("reset the final mix"), before);
}

/* ---- undo ---- */

FinalMix::SnapshotCommand::SnapshotCommand (FinalMix& fm, string const& name, Snapshot const& before, Snapshot const& after)
	: _fm (&fm)
	, _own_fm (false)
	, _before (before)
	, _after (after)
{
	set_name (name);
}

/* the window's FinalMix may be gone when the history is replayed; a
 * command restored from the session history has its own */
FinalMix::SnapshotCommand::SnapshotCommand (Session& s, XMLNode const& node)
	: _fm (new FinalMix (s))
	, _own_fm (true)
{
	string name;
	if (node.get_property (X_("name"), name)) {
		set_name (name);
	}
	for (auto const& c : node.children ()) {
		Snapshot& v = (c->name () == X_("Before")) ? _before : _after;
		for (auto const& t : c->children ()) {
			TrackState ts;
			string     id;
			int        role = Other;
			t->get_property (X_("route"), id);
			ts.route = PBD::ID (id);
			t->get_property (X_("gain"), ts.gain);
			t->get_property (X_("pan"), ts.pan);
			t->get_property (X_("role"), role);
			ts.settings.role = (role >= 0 && role < NumRoles) ? (Role)role : Other;
			t->get_property (X_("high-pass"), ts.settings.high_pass);
			t->get_property (X_("hp-freq"), ts.settings.hp_freq);
			t->get_property (X_("brightness"), ts.settings.brightness_db);
			t->get_property (X_("punch"), ts.settings.punch);
			t->get_property (X_("space"), ts.settings.space);
			v.push_back (ts);
		}
	}
}

FinalMix::SnapshotCommand::~SnapshotCommand ()
{
	if (_own_fm) {
		delete _fm;
	}
}

void
FinalMix::SnapshotCommand::operator() ()
{
	_fm->restore (_after);
}

void
FinalMix::SnapshotCommand::undo ()
{
	_fm->restore (_before);
}

XMLNode&
FinalMix::SnapshotCommand::get_state () const
{
	XMLNode* n = new XMLNode (X_("FinalMixSnapshotCommand"));
	n->set_property (X_("name"), name ());
	for (int k = 0; k < 2; ++k) {
		XMLNode* c = n->add_child (k == 0 ? X_("Before") : X_("After"));
		for (auto const& ts : (k == 0 ? _before : _after)) {
			XMLNode* t = c->add_child (X_("Track"));
			t->set_property (X_("route"), ts.route.to_s ());
			t->set_property (X_("gain"), ts.gain);
			t->set_property (X_("pan"), ts.pan);
			t->set_property (X_("role"), (int)ts.settings.role);
			t->set_property (X_("high-pass"), ts.settings.high_pass);
			t->set_property (X_("hp-freq"), ts.settings.hp_freq);
			t->set_property (X_("brightness"), ts.settings.brightness_db);
			t->set_property (X_("punch"), ts.settings.punch);
			t->set_property (X_("space"), ts.settings.space);
		}
	}
	return *n;
}

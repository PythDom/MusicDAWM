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
#include <glib/gstdio.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>
#include <sndfile.h>

#include "pbd/basename.h"
#include "pbd/compose.h"
#include "pbd/error.h"
#include "pbd/xml++.h"

#include "ardour/amp.h"
#include "ardour/audio_track.h"
#include "ardour/audioplaylist.h"
#include "ardour/audioregion.h"
#include "ardour/audio_port.h"
#include "ardour/broadcast_info.h"
#include "ardour/dB.h"
#include "ardour/export_channel_configuration.h"
#include "ardour/export_filename.h"
#include "ardour/export_format_specification.h"
#include "ardour/export_handler.h"
#include "ardour/export_status.h"
#include "ardour/export_timespan.h"
#include "ardour/filesystem_paths.h"
#include "ardour/import_status.h"
#include "ardour/io.h"
#include "ardour/final_mix.h"
#include "ardour/internal_send.h"
#include "ardour/lufs_meter.h"
#include "ardour/lv2_plugin.h"
#include "ardour/mute_control.h"
#include "ardour/plugin_insert.h"
#include "ardour/plugin_manager.h"
#include "ardour/region_factory.h"
#include "ardour/session_directory.h"
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
char const* const FinalMix::match_eq_name  = X_("Final Mix Match EQ");

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
		if (std::dynamic_pointer_cast<Track> (r) && !r->is_private_route () && !is_reference (r)) {
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
		/* the match EQ goes before the limiter, the limiter last */
		before = (string (name) == match_eq_name) ? find_plugin (r, limiter_name) : shared_ptr<PluginInsert> ();
		if (!before) {
			before = r->before_processor_for_placement (PostFader);
		}
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
	if (shared_ptr<Route> m = _session.master_out ()) {
		set_active (find_plugin (m, match_eq_name), yn && matched ());
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

string
FinalMix::region_signature (shared_ptr<Route> r) const
{
	shared_ptr<Track> t = std::dynamic_pointer_cast<Track> (r);
	if (!t || !t->playlist ()) {
		return string ();
	}
	std::shared_ptr<RegionList> const regions = t->playlist ()->region_list ();
	string                            sig     = t->playlist ()->id ().to_s ();
	for (auto const& reg : *regions) {
		shared_ptr<AudioRegion> ar = std::dynamic_pointer_cast<AudioRegion> (reg);
		sig += string_compose (" %1:%2:%3:%4:%5:%6", reg->id ().to_s (), reg->position ().samples (), reg->length ().samples (), reg->start ().samples (),
		                       ar ? ar->scale_amplitude () : 1.f, reg->muted ());
	}
	return sig;
}

float
FinalMix::track_loudness (shared_ptr<Route> r) const
{
	string const sig = region_signature (r);
	auto         i   = _loudness_cache.find (r->id ());
	if (i != _loudness_cache.end () && i->second.first == sig) {
		return i->second.second;
	}
	float const l              = analyse_track (r, false).lufs;
	_loudness_cache[r->id ()] = std::make_pair (sig, l);
	return l;
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

XMLNode&
FinalMix::snapshot_state (Snapshot const& v, XMLNode& c)
{
	for (auto const& ts : v) {
		XMLNode* t = c.add_child (X_("Track"));
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
	return c;
}

FinalMix::Snapshot
FinalMix::snapshot_from (XMLNode const& c)
{
	Snapshot v;
	for (auto const& t : c.children ()) {
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
	return v;
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
		(c->name () == X_("Before") ? _before : _after) = snapshot_from (*c);
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
	snapshot_state (_before, *n->add_child (X_("Before")));
	snapshot_state (_after, *n->add_child (X_("After")));
	return *n;
}

/* ---- analysis ---- */

namespace {

/* RBJ biquads, Q = 1/sqrt(2) */
struct Biquad {
	double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
	double z1 = 0, z2 = 0;

	static Biquad make (bool high, double f, double sr)
	{
		Biquad       q;
		double const w  = 2 * M_PI * f / sr;
		double const al = sin (w) / (2 * M_SQRT1_2);
		double const c  = cos (w);
		double const a0 = 1 + al;
		if (high) {
			q.b0 = (1 + c) / 2 / a0;
			q.b1 = -(1 + c) / a0;
		} else {
			q.b0 = (1 - c) / 2 / a0;
			q.b1 = (1 - c) / a0;
		}
		q.b2 = q.b0;
		q.a1 = -2 * c / a0;
		q.a2 = (1 - al) / a0;
		return q;
	}

	double run (double x)
	{
		double const y = b0 * x + z1;
		z1             = b1 * x - a1 * y + z2;
		z2             = b2 * x - a2 * y;
		return y;
	}
};

/* band edges, Hz */
double const edges[FinalMix::NumBands - 1] = { 80, 250, 1000, 4000, 8000 };

struct BandSplitter {
	std::vector<Biquad> f[FinalMix::NumBands];
	double              energy[FinalMix::NumBands] = { 0 };

	BandSplitter (double sr)
	{
		for (int b = 0; b < FinalMix::NumBands; ++b) {
			/* two of each: 4th order edges */
			for (int k = 0; k < 2; ++k) {
				if (b > 0) {
					f[b].push_back (Biquad::make (true, edges[b - 1], sr));
				}
				if (b < FinalMix::NumBands - 1) {
					f[b].push_back (Biquad::make (false, edges[b], sr));
				}
			}
		}
	}

	void run (float x)
	{
		for (int b = 0; b < FinalMix::NumBands; ++b) {
			double y = x;
			for (auto& q : f[b]) {
				y = q.run (y);
			}
			energy[b] += y * y;
		}
	}
};

typedef std::function<samplecnt_t (std::vector<float>* bufs, samplecnt_t n)> Reader;

/* nch: 1 or 2; mono is counted as it sounds centred in a stereo mix */
FinalMix::Analysis
analyse (uint32_t nch, double sr, Reader read, bool spectrum = true)
{
	FinalMix::Analysis a;
	LUFSMeter          meter (sr, nch);
	BandSplitter       bands (sr);
	samplecnt_t const  block = 8192;
	std::vector<float> buf[2] = { std::vector<float> (block), std::vector<float> (block) };
	double             e_mid = 0, e_side = 0;
	samplecnt_t        total = 0;
	for (;;) {
		samplecnt_t const n = read (buf, block);
		if (n <= 0) {
			break;
		}
		float const* data[2] = { buf[0].data (), buf[nch > 1 ? 1 : 0].data () };
		meter.run (data, n);
		for (samplecnt_t i = 0; spectrum && i < n; ++i) {
			float const m = nch > 1 ? 0.5f * (data[0][i] + data[1][i]) : data[0][i];
			float const d = nch > 1 ? 0.5f * (data[0][i] - data[1][i]) : 0.f;
			e_mid += m * m;
			e_side += d * d;
			bands.run (m);
		}
		total += n;
	}
	if (total == 0) {
		return a;
	}
	a.seconds   = total / sr;
	a.lufs      = meter.integrated_loudness ();
	a.true_peak = meter.dbtp ();
	if (nch == 1 && a.lufs > -200) {
		a.lufs -= 3.0103f;
	}
	double sum = 0;
	for (int b = 0; b < FinalMix::NumBands; ++b) {
		sum += bands.energy[b];
	}
	for (int b = 0; b < FinalMix::NumBands; ++b) {
		a.band_db[b] = sum > 0 ? 10 * log10 (std::max (1e-12, bands.energy[b] / sum)) : -120;
	}
	a.width_db = (nch > 1 && e_mid > 0 && e_side > 0) ? 10 * log10 (e_side / e_mid) : -200;
	a.valid    = a.lufs > -200;
	return a;
}

void
store_analysis (XMLNode& n, FinalMix::Analysis const& a)
{
	n.set_property (X_("valid"), a.valid);
	n.set_property (X_("lufs"), a.lufs);
	n.set_property (X_("true-peak"), a.true_peak);
	n.set_property (X_("width"), a.width_db);
	n.set_property (X_("seconds"), a.seconds);
	for (int b = 0; b < FinalMix::NumBands; ++b) {
		n.set_property (string_compose (X_("band%1"), b).c_str (), a.band_db[b]);
	}
}

FinalMix::Analysis
load_analysis (XMLNode const* n)
{
	FinalMix::Analysis a;
	if (!n) {
		return a;
	}
	n->get_property (X_("valid"), a.valid);
	n->get_property (X_("lufs"), a.lufs);
	n->get_property (X_("true-peak"), a.true_peak);
	n->get_property (X_("width"), a.width_db);
	n->get_property (X_("seconds"), a.seconds);
	for (int b = 0; b < FinalMix::NumBands; ++b) {
		n->get_property (string_compose (X_("band%1"), b).c_str (), a.band_db[b]);
	}
	return a;
}

} // namespace

string
FinalMix::band_name (int b)
{
	switch (b) {
		case 0:
			return _("sub bass (below 80 Hz)");
		case 1:
			return _("bass (80-250 Hz)");
		case 2:
			return _("low mids (250 Hz-1 kHz)");
		case 3:
			return _("mids (1-4 kHz)");
		case 4:
			return _("presence (4-8 kHz)");
		default:
			return _("air (above 8 kHz)");
	}
}

FinalMix::Analysis
FinalMix::analyse_file (string const& path)
{
	SF_INFO info;
	memset (&info, 0, sizeof (info));
	SNDFILE* sf = sf_open (path.c_str (), SFM_READ, &info);
	if (!sf) {
		return Analysis ();
	}
	uint32_t const     nch = std::min (2, std::max (1, info.channels));
	std::vector<float> inter;
	Analysis           a = analyse (nch, info.samplerate, [&] (std::vector<float>* bufs, samplecnt_t n) {
		inter.resize (n * info.channels);
		sf_count_t const got = sf_readf_float (sf, inter.data (), n);
		for (sf_count_t i = 0; i < got; ++i) {
			for (uint32_t c = 0; c < nch; ++c) {
				bufs[c][i] = inter[i * info.channels + c];
			}
		}
		return (samplecnt_t)got;
	});
	sf_close (sf);
	return a;
}

FinalMix::Analysis
FinalMix::analyse_track (shared_ptr<Route> r) const
{
	return analyse_track (r, true);
}

FinalMix::Analysis
FinalMix::analyse_track (shared_ptr<Route> r, bool spectrum) const
{
	shared_ptr<AudioTrack>    t  = std::dynamic_pointer_cast<AudioTrack> (r);
	shared_ptr<AudioPlaylist> pl = t ? std::dynamic_pointer_cast<AudioPlaylist> (t->playlist ()) : shared_ptr<AudioPlaylist> ();
	/* a copy of the list: keep it while iterating */
	std::shared_ptr<RegionList> const regions = pl ? pl->region_list () : std::shared_ptr<RegionList> ();
	if (!regions || regions->empty ()) {
		return Analysis ();
	}
	uint32_t const nch = std::max<uint32_t> (1, std::min<uint32_t> (2, t->n_inputs ().n_audio ()));
	samplepos_t    pos = regions->front ()->position ().samples ();
	samplepos_t    end = 0;
	for (auto const& reg : *regions) {
		pos = std::min (pos, reg->position ().samples ());
		end = std::max (end, reg->end_position ().samples ());
	}
	std::vector<Sample> mix (8192);
	std::vector<float>  gain (8192);
	return analyse (nch, _session.nominal_sample_rate (), [&] (std::vector<float>* bufs, samplecnt_t n) {
		n = std::min (n, end - pos);
		if (n <= 0) {
			return (samplecnt_t)0;
		}
		mix.resize (n);
		gain.resize (n);
		for (uint32_t c = 0; c < nch; ++c) {
			std::fill (bufs[c].begin (), bufs[c].begin () + n, 0.f);
			pl->read (bufs[c].data (), mix.data (), gain.data (), timepos_t (pos), timecnt_t (n), c);
		}
		pos += n;
		return n;
	}, spectrum);
}

/* ---- export ---- */

bool
FinalMix::mp3_available ()
{
	string ffmpeg, ffprobe;
	return ArdourVideoToolPaths::transcoder_exe (ffmpeg, ffprobe);
}

int
FinalMix::export_master (string const& folder, string const& name, bool wav, bool mp3, bool normalise, std::function<void()> idle)
{
	shared_ptr<Route> master = _session.master_out ();
	if (!master || (!wav && !mp3)) {
		return -1;
	}
	std::shared_ptr<ExportHandler> handler = _session.get_export_handler ();
	ExportTimespanPtr              tsp     = handler->add_timespan ();
	auto                           ccp     = handler->add_channel_config ();
	auto                           fnp     = handler->add_filename ();

	tsp->set_range (_session.current_start_sample (), _session.current_end_sample ());
	tsp->set_range_id (X_("session"));
	tsp->set_name (name);
	for (uint32_t n = 0; n < master->output ()->n_ports ().n_audio (); ++n) {
		PortExportChannel* channel = new PortExportChannel ();
		channel->add_port (master->output ()->audio (n));
		ccp->register_channel (ExportChannelPtr (channel));
	}
	fnp->set_folder (folder);
	fnp->set_timespan (tsp);
	fnp->include_label = false;

	string const norm = string_compose (X_("<Normalize enabled=\"%1\" loudness=\"1\" use-tp-limiter=\"1\" dbfs=\"0\" lufs=\"%2\" dbtp=\"%3\"/>"),
	                                    normalise ? 1 : 0, target_lufs (), limiter_ceiling ());
	string const tail = string (X_("<Silence><Start><Trim enabled=\"0\"/><Add enabled=\"0\"><Duration format=\"Timecode\" hours=\"0\" minutes=\"0\" seconds=\"0\" frames=\"0\"/></Add></Start>"
	                               "<End><Trim enabled=\"0\"/><Add enabled=\"0\"><Duration format=\"Timecode\" hours=\"0\" minutes=\"0\" seconds=\"0\" frames=\"0\"/></Add></End></Silence>"));
	std::vector<string> specs;
	if (wav) {
		specs.push_back (X_("<ExportFormatSpecification name=\"Final Mix WAV\" id=\"2f6f0c5e-6b8e-4d4e-9b0a-4b7d0f3a6a01\" analyse=\"0\">"
		                    "<Encoding id=\"F_WAV\" type=\"T_Sndfile\" extension=\"wav\" name=\"WAV\" has-sample-format=\"1\" channel-limit=\"256\"/>"
		                    "<SampleRate rate=\"SR_Session\"/><SRCQuality quality=\"SRC_SincBest\"/>"
		                    "<EncodingOptions><Option name=\"sample-format\" value=\"SF_24\"/><Option name=\"dithering\" value=\"D_None\"/>"
		                    "<Option name=\"tag-metadata\" value=\"0\"/><Option name=\"tag-support\" value=\"0\"/><Option name=\"broadcast-info\" value=\"0\"/></EncodingOptions>"
		                    "<Processing>") + norm + tail + X_("</Processing></ExportFormatSpecification>"));
	}
	if (mp3) {
		specs.push_back (X_("<ExportFormatSpecification name=\"Final Mix MP3\" id=\"2f6f0c5e-6b8e-4d4e-9b0a-4b7d0f3a6a02\" analyse=\"0\">"
		                    "<Encoding id=\"F_FFMPEG\" type=\"T_FFMPEG\" extension=\"mp3\" name=\"MP3\" has-sample-format=\"0\" channel-limit=\"256\"/>"
		                    "<SampleRate rate=\"SR_Session\"/><SRCQuality quality=\"SRC_SincBest\"/><CodecQuality quality=\"-2\"/>"
		                    "<EncodingOptions><Option name=\"sample-format\" value=\"SF_Float\"/><Option name=\"dithering\" value=\"D_None\"/>"
		                    "<Option name=\"tag-metadata\" value=\"0\"/><Option name=\"tag-support\" value=\"1\"/><Option name=\"broadcast-info\" value=\"0\"/></EncodingOptions>"
		                    "<Processing>") + norm + tail + X_("</Processing></ExportFormatSpecification>"));
	}
	for (auto const& spec : specs) {
		XMLTree tree;
		tree.read_buffer (spec.c_str ());
		auto fmp = handler->add_format (*tree.root ());
		fmp->set_soundcloud_upload (false);
		handler->add_export_config (tsp, ccp, fmp, fnp, std::shared_ptr<ARDOUR::BroadcastInfo> ());
	}
	if (handler->do_export ()) {
		return -1;
	}
	std::shared_ptr<ExportStatus> status = _session.get_export_status ();
	while (status->running ()) {
		idle ();
	}
	bool const aborted = status->aborted ();
	status->finish (TRS_UI);
	return aborted ? -1 : 0;
}

FinalMix::Analysis
FinalMix::analyse_mix (std::function<void()> idle)
{
	Listen const was = listen ();
	set_listen (After);
	string const folder = _session.session_directory ().export_path ();
	string const name   = X_("final-mix-analysis");
	Analysis     a;
	if (export_master (folder, name, true, false, false, idle) == 0) {
		string const path = Glib::build_filename (folder, name + ".wav");
		a                 = analyse_file (path);
		::g_unlink (path.c_str ());
	}
	XMLNode* n = session_node ();
	n->remove_nodes_and_delete (X_("MixAnalysis"));
	store_analysis (*n->add_child (X_("MixAnalysis")), a);
	set_listen (was);
	return a;
}

FinalMix::Analysis
FinalMix::last_mix_analysis () const
{
	return load_analysis (session_node ()->child (X_("MixAnalysis")));
}

FinalMix::Finalised
FinalMix::finalise (string const& folder, string const& name, bool mp3, std::function<void()> idle)
{
	Finalised f;
	Listen const was = listen ();
	set_listen (After);
	mp3 = mp3 && mp3_available ();
	if (export_master (folder, name, true, mp3, true, idle)) {
		f.error = _("The export failed or was stopped.");
	} else {
		f.wav = Glib::build_filename (folder, name + ".wav");
		if (mp3) {
			f.mp3 = Glib::build_filename (folder, name + ".mp3");
		}
		Analysis const a = analyse_file (f.wav);
		f.ok             = a.valid;
		f.lufs           = a.lufs;
		f.true_peak      = a.true_peak;
		if (!f.ok) {
			f.error = _("The exported file could not be read back.");
		} else if (a.true_peak > limiter_ceiling () + 0.5f || fabsf (a.lufs - target_lufs ()) > 1.5f) {
			/* never hand over a file that missed the target or clips (seen
			 * when the export's loudness analysis was not available) */
			f.ok    = false;
			f.error = string_compose (_("The file came out at %1 LUFS with peaks at %2 dBTP instead of %3 LUFS under %4 dBTP: the loudness normalisation did not work. The file was kept for inspection: %5"),
			                          roundf (a.lufs * 10) / 10, roundf (a.true_peak * 10) / 10, target_lufs (), limiter_ceiling (), f.wav);
		}
	}
	set_listen (was);
	return f;
}

/* ---- listening ---- */

float
FinalMix::estimate_mix_lufs (bool before) const
{
	double p = 0;
	for (auto const& r : tracks ()) {
		if (excluded (r) || r->mute_control ()->muted_by_self ()) {
			continue;
		}
		float const l = track_loudness (r);
		if (l <= -200) {
			continue;
		}
		float const g = before ? 0.f : accurate_coefficient_to_dB (r->gain_control ()->get_value ());
		p += pow (10.0, (l + g) / 10.0);
	}
	if (p <= 0) {
		return -200;
	}
	float const master = _session.master_out () ? accurate_coefficient_to_dB (_session.master_out ()->gain_control ()->get_value ()) : 0;
	return 10 * log10 (p) + (before ? 0 : master);
}

FinalMix::Listen
FinalMix::listen () const
{
	int l = After;
	session_node ()->get_property (X_("listen"), l);
	return (l >= After && l <= Ref) ? (Listen)l : After;
}

bool
FinalMix::set_listen (Listen l)
{
	Listen const      now    = listen ();
	XMLNode*          n      = session_node ();
	shared_ptr<Route> master = _session.master_out ();
	shared_ptr<Route> ref    = reference_track ();
	if (l == Ref && !ref) {
		return false;
	}
	if (l == now) {
		return true;
	}

	/* back to the mix first */
	if (now == Before) {
		if (XMLNode* st = n->child (X_("AfterState"))) {
			restore (snapshot_from (*st));
			bool  fx = true;
			float mg = GAIN_COEFF_UNITY;
			st->get_property (X_("fx"), fx);
			st->get_property (X_("master-gain"), mg);
			set_fx_enabled (fx);
			if (master) {
				master->gain_control ()->set_value (mg, PBD::Controllable::NoGroup);
			}
		}
		n->remove_nodes_and_delete (X_("AfterState"));
	} else if (now == Ref && ref) {
		ref->solo_control ()->set_value (0, PBD::Controllable::NoGroup);
		ref->mute_control ()->set_value (1, PBD::Controllable::NoGroup);
	}

	if (l == Before) {
		float const after  = estimate_mix_lufs (false);
		float const before = estimate_mix_lufs (true);
		XMLNode*    st     = n->add_child (X_("AfterState"));
		snapshot_state (snapshot (), *st);
		st->set_property (X_("fx"), fx_enabled ());
		st->set_property (X_("master-gain"), master ? (float)master->gain_control ()->get_value () : 1.f);
		for (auto const& r : tracks ()) {
			if (excluded (r)) {
				continue;
			}
			r->gain_control ()->set_value (GAIN_COEFF_UNITY, PBD::Controllable::NoGroup);
			if (r->pan_azimuth_control ()) {
				r->pan_azimuth_control ()->set_value (0.5, PBD::Controllable::NoGroup);
			}
		}
		set_fx_enabled (false);
		if (master && after > -200 && before > -200) {
			master->gain_control ()->set_value (dB_to_coefficient (std::min (12.f, after - before)), PBD::Controllable::NoGroup);
		}
	} else if (l == Ref) {
		/* at the loudness of the mix */
		Analysis const ra  = reference_analysis ();
		Analysis const ma  = last_mix_analysis ();
		float const    mix = ma.valid ? ma.lufs : estimate_mix_lufs (false);
		if (ra.valid && mix > -200) {
			ref->gain_control ()->set_value (dB_to_coefficient (std::max (-40.f, std::min (12.f, mix - ra.lufs))), PBD::Controllable::NoGroup);
		}
		ref->mute_control ()->set_value (0, PBD::Controllable::NoGroup);
		ref->solo_control ()->set_value (1, PBD::Controllable::NoGroup);
	}
	n->set_property (X_("listen"), (int)l);
	_session.set_dirty ();
	SettingsChanged (); /* EMIT SIGNAL */
	return true;
}

/* ---- the reference ---- */

bool
FinalMix::is_reference (shared_ptr<Route> r) const
{
	XMLNode* n = r->extra_xml (node_name);
	bool     y = false;
	return n && n->get_property (X_("reference"), y) && y;
}

shared_ptr<Route>
FinalMix::reference_track () const
{
	for (auto const& r : *_session.get_routes ()) {
		if (std::dynamic_pointer_cast<Track> (r) && is_reference (r)) {
			return r;
		}
	}
	return shared_ptr<Route> ();
}

FinalMix::Analysis
FinalMix::reference_analysis () const
{
	return load_analysis (session_node ()->child (X_("RefAnalysis")));
}

int
FinalMix::set_reference (string const& path)
{
	if (!Glib::file_test (path, Glib::FILE_TEST_EXISTS)) {
		PBD::error << string_compose (_("Final Mix: the reference \"%1\" does not exist."), path) << endmsg;
		return -1;
	}
	if (listen () != After) {
		set_listen (After);
	}
	if (shared_ptr<Route> old = reference_track ()) {
		_session.remove_route (old);
	}

	ImportStatus status;
	status.total                   = 1;
	status.current                 = 1;
	status.quality                 = SrcBest;
	status.freeze                  = false;
	status.replace_existing_source = false;
	status.split_midi_channels     = false;
	status.import_markers          = false;
	status.midi_track_name_source  = ARDOUR::SMFFileAndTrackName;
	status.paths.push_back (path);
	_session.import_files (status);
	if (status.cancel || status.sources.empty ()) {
		PBD::error << string_compose (_("Final Mix: the reference \"%1\" could not be imported."), path) << endmsg;
		return -1;
	}
	SourceList sl;
	for (auto const& src : status.sources) {
		sl.push_back (src);
	}
	uint32_t const nch = std::min<uint32_t> (2, sl.size ());
	sl.resize (nch);

	std::list<shared_ptr<AudioTrack>> tl = _session.new_audio_track (nch, 2, 0, 1, _("Reference"), PresentationInfo::max_order, Normal, true);
	if (tl.empty ()) {
		return -1;
	}
	shared_ptr<AudioTrack> t = tl.front ();
	XMLNode*               x = t->extra_xml (node_name, true);
	x->set_property (X_("reference"), true);
	x->set_property (X_("file"), path);

	PBD::PropertyList whole;
	whole.add (Properties::start, timepos_t (Temporal::AudioTime));
	whole.add (Properties::length, sl.front ()->length ());
	whole.add (Properties::name, string (PBD::basename_nosuffix (path)));
	whole.add (Properties::whole_file, true);
	shared_ptr<Region> wr = RegionFactory::create (sl, whole);
	PBD::PropertyList  plist;
	plist.add (Properties::start, timepos_t (Temporal::AudioTime));
	plist.add (Properties::length, sl.front ()->length ());
	plist.add (Properties::name, string (PBD::basename_nosuffix (path)));
	shared_ptr<Region> r = RegionFactory::create (wr, plist);
	t->playlist ()->add_region (r, timepos_t (_session.current_start_sample ()));

	/* straight to where the master goes: the mix's EQ and limiter must
	 * not colour the reference */
	if (shared_ptr<Route> m = _session.master_out ()) {
		t->output ()->disconnect ();
		for (uint32_t c = 0; c < t->output ()->n_ports ().n_audio () && c < m->output ()->n_ports ().n_audio (); ++c) {
			std::vector<string> conns;
			m->output ()->audio (c)->get_connections (conns);
			for (auto const& name : conns) {
				t->output ()->connect (t->output ()->audio (c), name);
			}
		}
	}
	t->mute_control ()->set_value (1, PBD::Controllable::NoGroup);

	Analysis const a = analyse_track (t);
	if (!a.valid) {
		PBD::error << string_compose (_("Final Mix: the reference \"%1\" could not be analysed (silent?)."), path) << endmsg;
	}
	XMLNode*       n = session_node ();
	n->remove_nodes_and_delete (X_("RefAnalysis"));
	store_analysis (*n->add_child (X_("RefAnalysis")), a);
	_session.set_dirty ();
	SettingsChanged (); /* EMIT SIGNAL */
	return a.valid ? 0 : -1;
}

/* ---- R1: matching the reference ---- */

FinalMix::Match
FinalMix::compute_match (Analysis const& mix, Analysis const& ref) const
{
	Match m;
	if (!mix.valid || !ref.valid) {
		return m;
	}
	m.valid       = true;
	m.target_lufs = std::max (-20.f, std::min (-6.f, roundf (ref.lufs * 2) / 2));

	std::vector<string> lines;
	lines.push_back (string_compose (_("Loudness target set to the reference's: %1 LUFS (the mix measures %2)."), m.target_lufs, roundf (mix.lufs * 10) / 10));

	/* tonal balance: move a bit more than half way towards the reference,
	 * at most 4 dB per band */
	string more, less;
	for (int b = 0; b < NumBands; ++b) {
		float const d = ref.band_db[b] - mix.band_db[b];
		float       g = std::max (-4.f, std::min (4.f, 0.6f * d));
		g             = roundf (g * 2) / 2;
		if (fabsf (g) < 0.5f) {
			g = 0;
		}
		m.eq_db[b] = g;
		if (g >= 1) {
			more += (more.empty () ? "" : ", ") + band_name (b);
		} else if (g <= -1) {
			less += (less.empty () ? "" : ", ") + band_name (b);
		}
	}
	if (!more.empty ()) {
		lines.push_back (string_compose (_("Master EQ: more %1."), more));
	}
	if (!less.empty ()) {
		lines.push_back (string_compose (_("Master EQ: less %1."), less));
	}
	if (more.empty () && less.empty ()) {
		lines.push_back (_("The tonal balance is already close to the reference's."));
	}

	/* stereo width and dynamics: advice only */
	if (ref.width_db > -100 && mix.width_db > -100) {
		if (ref.width_db - mix.width_db > 3) {
			lines.push_back (_("The reference is wider: pan guitars, keys and backing vocals further out, or add Space."));
		} else if (mix.width_db - ref.width_db > 3) {
			lines.push_back (_("The mix is wider than the reference: bring the side tracks closer to the centre."));
		}
	}
	float const plr_ref = ref.true_peak - ref.lufs;
	float const plr_mix = mix.true_peak - mix.lufs;
	if (plr_mix - plr_ref > 3) {
		lines.push_back (_("The reference is more compressed (smaller peaks above its loudness): try more Punch on drums, bass and vocals."));
	} else if (plr_ref - plr_mix > 3) {
		lines.push_back (_("The reference is more dynamic: try less Punch."));
	}
	for (auto const& l : lines) {
		m.report += l + "\n";
	}
	return m;
}

bool
FinalMix::matched () const
{
	bool y = false;
	session_node ()->get_property (X_("matched"), y);
	return y;
}

void
FinalMix::apply_match (Match const& m)
{
	if (!m.valid) {
		return;
	}
	XMLNode* n = session_node ();
	if (!matched ()) {
		/* what Clear match goes back to */
		n->set_property (X_("pre-match-target"), (int)target ());
		n->set_property (X_("pre-match-lufs"), target_lufs ());
	}
	set_target (Custom, m.target_lufs);

	shared_ptr<Route> master = _session.master_out ();
	if (master) {
		shared_ptr<PluginInsert> eq = find_plugin (master, match_eq_name);
		if (!eq) {
			bool const stereo = master->n_outputs ().n_audio () >= 2;
			eq = make_plugin (master, match_eq_name, stereo ? X_("http://gareus.org/oss/lv2/fil4#stereo") : X_("http://gareus.org/oss/lv2/fil4#mono"), false);
		}
		if (eq) {
			static float const freq[NumBands] = { 80, 150, 500, 2000, 5500, 9000 };
			set_param (eq, "HighPass", 0);
			set_param (eq, "LowPass", 0);
			set_param (eq, "LSsec", 1);
			set_param (eq, "LSfreq", freq[0]);
			set_param (eq, "LSq", 0.7);
			set_param (eq, "LSgain", m.eq_db[0]);
			char const* sec[]  = { "sec1", "sec2", "sec3", "sec4" };
			char const* fq[]   = { "freq1", "freq2", "freq3", "freq4" };
			char const* q[]    = { "q1", "q2", "q3", "q4" };
			char const* gain[] = { "gain1", "gain2", "gain3", "gain4" };
			for (int k = 0; k < 4; ++k) {
				set_param (eq, sec[k], 1);
				set_param (eq, fq[k], freq[k + 1]);
				set_param (eq, q[k], 0.8);
				set_param (eq, gain[k], m.eq_db[k + 1]);
			}
			set_param (eq, "HSsec", 1);
			set_param (eq, "HSfreq", freq[5]);
			set_param (eq, "HSq", 0.7);
			set_param (eq, "HSgain", m.eq_db[5]);
			set_param (eq, "gain", 0);
			set_param (eq, "enable", 1);
			set_active (eq, fx_enabled ());
		}
	}
	n->set_property (X_("matched"), true);
	n->set_property (X_("match-report"), m.report);
	_session.set_dirty ();
	SettingsChanged (); /* EMIT SIGNAL */
}

void
FinalMix::clear_match ()
{
	if (!matched ()) {
		return;
	}
	XMLNode* n = session_node ();
	int      t = Streaming;
	float    l = -14;
	n->get_property (X_("pre-match-target"), t);
	n->get_property (X_("pre-match-lufs"), l);
	set_target ((Target)t, l);
	if (shared_ptr<Route> master = _session.master_out ()) {
		set_active (find_plugin (master, match_eq_name), false);
	}
	n->set_property (X_("matched"), false);
	_session.set_dirty ();
	SettingsChanged (); /* EMIT SIGNAL */
}

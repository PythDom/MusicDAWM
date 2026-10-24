/*
 * MusicDAWM: import a DAWproject file into a session (see dawproject_import.h).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include "pbd/basename.h"
#include "pbd/compose.h"
#include "pbd/error.h"
#include "pbd/file_archive.h"
#include "pbd/file_utils.h"
#include "pbd/xml++.h"

#include "evoral/Event.h"

#include "temporal/tempo.h"

#include "ardour/audio_port.h"
#include "ardour/audio_track.h"
#include "ardour/audioregion.h"
#include "ardour/automation_control.h"
#include "ardour/automation_list.h"
#include "ardour/dB.h"
#include "ardour/dawproject_import.h"
#include "ardour/gain_control.h"
#include "ardour/import_status.h"
#include "ardour/io.h"
#include "ardour/location.h"
#include "ardour/midi_region.h"
#include "ardour/midi_source.h"
#include "ardour/midi_track.h"
#include "ardour/mute_control.h"
#include "ardour/pannable.h"
#include "ardour/playlist.h"
#include "ardour/port.h"
#include "ardour/region_factory.h"
#include "ardour/route.h"
#include "ardour/send.h"
#include "ardour/session.h"
#include "ardour/smf_source.h"
#include "ardour/solo_control.h"
#include "ardour/source.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using namespace PBD;
using namespace Temporal;
using std::string;
using std::vector;

namespace {

/* ---- small XML helpers ------------------------------------------------- */

string
attr (XMLNode const* n, char const* name, string const& dflt = string ())
{
	XMLProperty const* p = n ? n->property (name) : 0;
	return p ? p->value () : dflt;
}

bool
has_attr (XMLNode const* n, char const* name)
{
	return n && n->property (name);
}

double
num (XMLNode const* n, char const* name, double dflt = 0)
{
	XMLProperty const* p = n ? n->property (name) : 0;
	if (!p) {
		return dflt;
	}
	/* XML numbers use a '.' whatever the locale */
	return g_ascii_strtod (p->value ().c_str (), 0);
}

bool
flag (XMLNode const* n, char const* name, bool dflt)
{
	XMLProperty const* p = n ? n->property (name) : 0;
	if (!p) {
		return dflt;
	}
	return p->value () == "true" || p->value () == "1";
}

XMLNode const*
child (XMLNode const* n, char const* name)
{
	if (!n) {
		return 0;
	}
	for (auto const* c : n->children ()) {
		if (c->name () == name) {
			return c;
		}
	}
	return 0;
}

/* "#rrggbb" -> 0xrrggbbff */
bool
parse_color (string const& s, uint32_t& rgba)
{
	if (s.size () != 7 || s[0] != '#') {
		return false;
	}
	char*         end;
	unsigned long v = strtoul (s.c_str () + 1, &end, 16);
	if (*end) {
		return false;
	}
	rgba = (uint32_t) (v << 8) | 0xff;
	return true;
}

/* A Volume parameter as a gain coefficient */
gain_t
volume_coeff (XMLNode const* p, double v)
{
	string const unit = attr (p, "unit", "linear");
	if (unit == "decibel") {
		return dB_to_coefficient (v);
	}
	return std::max (0.0, v); /* linear (Bitwig: 0..2), normalized */
}

/* A Pan parameter as Ardour's azimuth: 0 left, 0.5 centre, 1 right */
double
pan_azimuth (XMLNode const* p, double v)
{
	if (has_attr (p, "min") && has_attr (p, "max")) {
		double const lo = num (p, "min"), hi = num (p, "max");
		if (hi > lo) {
			return std::min (1.0, std::max (0.0, (v - lo) / (hi - lo)));
		}
	}
	string const unit = attr (p, "unit", "normalized");
	if (unit == "percent") {
		v = (v + 100.0) / 200.0;
	} else if (unit == "linear") {
		v = (v + 1.0) / 2.0;
	}
	return std::min (1.0, std::max (0.0, v));
}

/* ---- the importer ------------------------------------------------------ */

enum Unit { Beats_, Seconds_ };

Unit
unit_of (XMLNode const* n, Unit dflt)
{
	string const u = attr (n, "timeUnit");
	return u == "seconds" ? Seconds_ : u == "beats" ? Beats_ : dflt;
}

/* How the times of one timeline map onto the session timeline. */
struct Context {
	std::shared_ptr<Route> route; /* the track the content belongs to */
	Unit                   unit = Beats_; /* unit of times in this timeline */
	timepos_t              origin; /* session position of time 0 of this timeline */
	timepos_t              win_start; /* only what lies in [win_start, win_end) plays */
	timepos_t              win_end;
	bool                   muted = false;
	string                 clip_name;
};

class Importer
{
public:
	Importer (Session& s, DAWProjectImport::Result& r)
		: _session (s)
		, _result (r)
		, _sr (s.sample_rate ())
	{
	}

	bool run (string const& path);
	bool run_xml (string const& xml, string const& base_dir);

private:
	Session&                  _session;
	DAWProjectImport::Result& _result;
	samplecnt_t               _sr;
	string                    _dir; /* extracted archive */

	std::map<string, std::shared_ptr<Route>> _route_of_track;   /* Track id -> route */
	std::map<string, std::shared_ptr<Route>> _route_of_channel; /* Channel id -> route */
	std::map<string, XMLNode const*>         _channel_of_route_id;
	struct Param {
		std::shared_ptr<Route> route;
		enum { Volume, Pan, Mute, SendVolume } what;
		XMLNode const*         node;
		std::shared_ptr<Send>  send;
	};
	std::map<string, Param>          _params;  /* parameter id -> what it controls */
	std::map<string, XMLNode const*> _by_id;   /* every element with an id */
	std::map<string, vector<std::shared_ptr<Source>>> _audio_sources; /* file -> sources */
	std::set<string>                 _said;    /* report lines already given */

	void note (string const& s)
	{
		if (_said.insert (s).second) {
			_result.notes.push_back (s);
		}
	}

	bool extract (string const& path);
	bool build (XMLNode const* project);
	void index_ids (XMLNode const*);
	void transport (XMLNode const* project, XMLNode const* arrangement);
	void structure (XMLNode const*);
	void track (XMLNode const*, int depth);
	void channel_settings (std::shared_ptr<Route>, XMLNode const* channel);
	void routing ();
	void devices (std::shared_ptr<Route>, XMLNode const* channel);

	/* timeline content */
	timepos_t at (Context const&, double t) const;
	void      timeline (XMLNode const*, Context const&);
	void      clips (XMLNode const*, Context const&);
	void      clip (XMLNode const*, Context const&);
	void      content_segment (XMLNode const* content, Context ctx, Unit cu, double c_from, double c_to, timepos_t const& at_pos, timepos_t const& win_end);
	void      audio (XMLNode const*, Context const&);
	void      warps (XMLNode const*, Context const&);
	void      notes (XMLNode const*, Context const&);
	void      points (XMLNode const*, Context const&);
	void      markers (XMLNode const*, Context const&);

	vector<std::shared_ptr<Source>> audio_sources (string const& file);
	vector<std::shared_ptr<Region>> _clip_regions; /* regions made for the clip being imported */
};

bool
Importer::extract (string const& path)
{
	gchar* tmp = g_dir_make_tmp ("musicdawm-dawproject-XXXXXX", 0);
	if (!tmp) {
		return false;
	}
	_dir = tmp;
	g_free (tmp);

	try {
		FileArchive ar (path);
		for (string name = ar.next_file_name (); !name.empty (); name = ar.next_file_name ()) {
			/* never write outside the folder: no absolute paths, no ".." */
			std::replace (name.begin (), name.end (), '\\', '/');
			if (name[0] == '/' || name.find (':') != string::npos || ("/" + name + "/").find ("/../") != string::npos) {
				note (string_compose (_("Skipped a file with an unsafe name in the archive: %1"), name));
				continue;
			}
			if (name.back () == '/') {
				continue;
			}
			string const dest = Glib::build_filename (_dir, name);
			g_mkdir_with_parents (Glib::path_get_dirname (dest).c_str (), 0755);
			ar.extract_current_file (dest);
		}
	} catch (...) {
		return false;
	}
	return Glib::file_test (Glib::build_filename (_dir, "project.xml"), Glib::FILE_TEST_EXISTS);
}

void
Importer::index_ids (XMLNode const* n)
{
	if (has_attr (n, "id")) {
		_by_id[attr (n, "id")] = n;
	}
	for (auto const* c : n->children ()) {
		index_ids (c);
	}
}

/* session position of time t of a timeline */
timepos_t
Importer::at (Context const& ctx, double t) const
{
	if (ctx.unit == Beats_) {
		return timepos_t (ctx.origin.beats () + Beats::from_double (t));
	}
	return timepos_t (ctx.origin.samples () + (samplepos_t) llrint (t * _sr));
}

bool
Importer::run (string const& path)
{
	if (!extract (path)) {
		note (_("The file is not a DAWproject archive (no project.xml)."));
		return false;
	}
	XMLTree tree;
	bool    ok = tree.read (Glib::build_filename (_dir, "project.xml"));
	if (!ok) {
		note (_("project.xml could not be read."));
	} else {
		ok = build (tree.root ());
	}
	PBD::remove_directory (_dir);
	return ok;
}

bool
Importer::run_xml (string const& xml, string const& base_dir)
{
	_dir = base_dir;
	XMLTree tree;
	if (!tree.read_buffer (xml.c_str ())) {
		note (_("The project could not be read."));
		return false;
	}
	return build (tree.root ());
}

bool
Importer::build (XMLNode const* project)
{
	if (!project || project->name () != "Project") {
		note (_("project.xml is not a DAWproject project."));
		return false;
	}
	if (XMLNode const* app = child (project, "Application")) {
		_result.application = attr (app, "name") + " " + attr (app, "version");
	}
	index_ids (project);

	XMLNode const* arrangement = child (project, "Arrangement");
	transport (project, arrangement);
	structure (child (project, "Structure"));
	routing ();

	if (arrangement) {
		Context ctx;
		ctx.unit      = Beats_;
		ctx.origin    = timepos_t (Beats ());
		ctx.win_start = timepos_t (Beats ());
		ctx.win_end   = timepos_t::max (BeatTime);
		ctx.muted     = false;
		for (auto const* c : arrangement->children ()) {
			if (c->name () == "Lanes") {
				timeline (c, ctx);
			} else if (c->name () == "Markers" || c->name () == "markers") {
				markers (c, ctx);
			}
		}
	}

	if (child (project, "Scenes") && !child (project, "Scenes")->children ().empty ()) {
		note (_("Clip launcher scenes were not imported."));
	}

	/* the session range covers everything imported */
	timepos_t first = timepos_t::max (AudioTime), last (AudioTime);
	for (auto const& [id, r] : _route_of_track) {
		std::shared_ptr<Track> t = std::dynamic_pointer_cast<Track> (r);
		if (!t || t->playlist ()->region_list ()->empty ()) {
			continue;
		}
		std::pair<timepos_t, timepos_t> ext = t->playlist ()->get_extent ();
		first = std::min (first, ext.first);
		last  = std::max (last, ext.second);
	}
	if (first < last) {
		_session.maybe_update_session_range (first, last);
	}
	return true;
}

void
Importer::transport (XMLNode const* project, XMLNode const* arrangement)
{
	XMLNode const* tr = child (project, "Transport");
	double         bpm = 0;
	int            num = 0, den = 0;
	if (XMLNode const* t = child (tr, "Tempo")) {
		bpm = ::num (t, "value");
	}
	if (XMLNode const* ts = child (tr, "TimeSignature")) {
		num = (int) ::num (ts, "numerator");
		den = (int) ::num (ts, "denominator");
	}

	TempoMap::WritableSharedPtr tmap (TempoMap::write_copy ());
	if (bpm > 0) {
		tmap->set_tempo (Tempo (bpm, 4), timepos_t (Beats ()));
	}
	if (num > 0 && den > 0) {
		tmap->set_meter (Meter (num, den), timepos_t (Beats ()));
	}

	/* changes over time; times are in the arrangement's unit (beats by default) */
	XMLNode const* lanes = child (arrangement, "Lanes");
	Unit const     au    = unit_of (lanes, Beats_);
	if (XMLNode const* ta = child (arrangement, "TempoAutomation")) {
		Unit const u = unit_of (ta, au);
		for (auto const* p : ta->children ()) {
			if (p->name () != "RealPoint") {
				continue;
			}
			double const t = ::num (p, "time"), v = ::num (p, "value");
			if (v <= 0) {
				continue;
			}
			timepos_t const pos = u == Beats_ ? timepos_t (Beats::from_double (t)) : timepos_t ((samplepos_t) llrint (t * _sr));
			/* a linear point ramps to the next one */
			Tempo tempo (v, 4);
			tmap->set_tempo (tempo, pos);
			if (attr (p, "interpolation") == "linear") {
				note (_("Tempo ramps were imported as steps."));
			}
		}
	}
	if (XMLNode const* sa = child (arrangement, "TimeSignatureAutomation")) {
		Unit const u = unit_of (sa, au);
		for (auto const* p : sa->children ()) {
			if (p->name () != "TimeSignaturePoint") {
				continue;
			}
			int const n = (int) ::num (p, "numerator"), d = (int) ::num (p, "denominator");
			if (n <= 0 || d <= 0) {
				continue;
			}
			double const t = ::num (p, "time");
			timepos_t pos  = u == Beats_ ? timepos_t (Beats::from_double (t)) : timepos_t ((samplepos_t) llrint (t * _sr));
			/* a time signature starts on a bar */
			BBT_Argument bbt = tmap->bbt_at (pos);
			if (bbt.beats != 1 || bbt.ticks != 0) {
				bbt = BBT_Argument (bbt.reference (), tmap->round_up_to_bar (bbt));
				note (_("A time signature change that was not on a bar line was moved to the next bar."));
			}
			tmap->set_meter (Meter (n, d), timepos_t (tmap->quarters_at (bbt)));
		}
	}
	TempoMap::update (tmap);
}

void
Importer::structure (XMLNode const* st)
{
	if (!st) {
		return;
	}
	for (auto const* c : st->children ()) {
		if (c->name () == "Track") {
			track (c, 0);
		} else if (c->name () == "Channel") {
			/* a channel without a track: master, effect returns, groups */
			string const role = attr (c, "role", "regular");
			if (role == "master") {
				if (_session.master_out ()) {
					_route_of_channel[attr (c, "id")] = _session.master_out ();
					channel_settings (_session.master_out (), c);
				}
			} else {
				int const ch = std::max (1, (int) num (c, "audioChannels", 2));
				RouteList rl = _session.new_audio_route (ch, 2, std::shared_ptr<RouteGroup> (), 1, attr (c, "name", _("Bus")), PresentationInfo::AudioBus, PresentationInfo::max_order);
				if (!rl.empty ()) {
					_route_of_channel[attr (c, "id")] = rl.front ();
					channel_settings (rl.front (), c);
					++_result.busses;
				}
			}
		}
	}
}

void
Importer::track (XMLNode const* t, int depth)
{
	XMLNode const* ch   = child (t, "Channel");
	string const   role = attr (ch, "role", "regular");
	string const   name = attr (t, "name", _("Track"));
	string const   ct   = " " + attr (t, "contentType") + " ";
	bool const     is_audio = ct.find (" audio ") != string::npos;
	bool const     is_notes = ct.find (" notes ") != string::npos;

	std::shared_ptr<Route> route;

	if (role == "master") {
		route = _session.master_out ();
	} else if (role == "effect" || role == "submix" || (ch && !is_audio && !is_notes && ct.find (" tracks ") != string::npos)) {
		/* effect returns, groups and folder tracks with their own channel become busses */
		int const n = std::max (1, (int) num (ch, "audioChannels", 2));
		RouteList rl = _session.new_audio_route (n, 2, std::shared_ptr<RouteGroup> (), 1, name, PresentationInfo::AudioBus, PresentationInfo::max_order);
		if (!rl.empty ()) {
			route = rl.front ();
			++_result.busses;
		}
	} else if (role == "vca") {
		note (string_compose (_("VCA \"%1\" was not imported."), name));
	} else if (is_notes && !is_audio) {
		std::list<std::shared_ptr<MidiTrack>> tl = _session.new_midi_track (
		    ChanCount (DataType::MIDI, 1), ChanCount (DataType::MIDI, 1), false,
		    std::shared_ptr<PluginInfo> (), 0, std::shared_ptr<RouteGroup> (), 1, name,
		    PresentationInfo::max_order, Normal, true);
		if (!tl.empty ()) {
			route = tl.front ();
			++_result.tracks;
		}
	} else if (is_audio) {
		int const n = std::min (2, std::max (1, (int) num (ch, "audioChannels", 2)));
		AudioTrackList tl = _session.new_audio_track (n, 2, std::shared_ptr<RouteGroup> (), 1, name, PresentationInfo::max_order, Normal, true);
		if (!tl.empty ()) {
			route = tl.front ();
			++_result.tracks;
		}
	}

	if (route) {
		_route_of_track[attr (t, "id")] = route;
		if (ch) {
			_route_of_channel[attr (ch, "id")] = route;
			_channel_of_route_id[route->id ().to_s ()] = ch;
			channel_settings (route, ch);
		}
		uint32_t rgba;
		if (parse_color (attr (t, "color"), rgba) && route != _session.master_out ()) {
			route->presentation_info ().set_color (rgba);
		}
	}

	/* folder: the children are tracks of their own */
	for (auto const* c : t->children ()) {
		if (c->name () == "Track") {
			track (c, depth + 1);
		}
	}
	if (depth == 0 && !route && ct.find (" tracks ") != string::npos) {
		note (string_compose (_("Folder \"%1\" was flattened: its tracks are imported one by one."), name));
	}
}

void
Importer::channel_settings (std::shared_ptr<Route> route, XMLNode const* ch)
{
	if (XMLNode const* v = child (ch, "Volume")) {
		route->gain_control ()->set_value (volume_coeff (v, num (v, "value", 1)), Controllable::NoGroup);
		_params[attr (v, "id")] = Param { route, Param::Volume, v, std::shared_ptr<Send> () };
	}
	if (XMLNode const* p = child (ch, "Pan")) {
		if (route->pan_azimuth_control ()) {
			route->pan_azimuth_control ()->set_value (pan_azimuth (p, num (p, "value", 0.5)), Controllable::NoGroup);
		}
		_params[attr (p, "id")] = Param { route, Param::Pan, p, std::shared_ptr<Send> () };
	}
	if (XMLNode const* m = child (ch, "Mute")) {
		route->mute_control ()->set_value (flag (m, "value", false) ? 1 : 0, Controllable::NoGroup);
		_params[attr (m, "id")] = Param { route, Param::Mute, m, std::shared_ptr<Send> () };
	}
	if (flag (ch, "solo", false) && route != _session.master_out ()) {
		route->solo_control ()->set_value (1, Controllable::NoGroup);
	}
	devices (route, ch);
}

void
Importer::devices (std::shared_ptr<Route> route, XMLNode const* ch)
{
	XMLNode const* devs = child (ch, "Devices");
	if (!devs) {
		return;
	}
	for (auto const* d : devs->children ()) {
		string const what = d->name ();
		string const dev  = attr (d, "deviceName", attr (d, "name", what));
		if (what == "Equalizer" || what == "Compressor" || what == "NoiseGate" || what == "Limiter" || what == "BuiltinDevice") {
			note (string_compose (_("\"%1\": built-in %2 \"%3\" was not imported."), route->name (), what, dev));
		} else {
			note (string_compose (_("\"%1\": plug-in \"%2\" (%3) was not imported; insert it again by hand."), route->name (), dev, what));
		}
	}
}

/* outputs and sends, once all routes exist */
void
Importer::routing ()
{
	for (auto const& [rid, ch] : _channel_of_route_id) {
		std::shared_ptr<Route> route;
		for (auto const& [cid, r] : _route_of_channel) {
			if (r->id ().to_s () == rid) {
				route = r;
				break;
			}
		}
		if (!route) {
			continue;
		}

		/* output to another channel than the master */
		string const dest = attr (ch, "destination");
		auto         d    = _route_of_channel.find (dest);
		if (!dest.empty () && d != _route_of_channel.end () && d->second != _session.master_out () && d->second != route) {
			std::shared_ptr<IO> out = route->output ();
			std::shared_ptr<IO> in  = d->second->input ();
			out->disconnect ();
			uint32_t const n_in = in->n_ports ().n_audio ();
			for (uint32_t i = 0; i < out->n_ports ().n_audio () && n_in > 0; ++i) {
				out->connect (out->audio (i), in->audio (i % n_in)->name ());
			}
		}

		/* sends */
		if (XMLNode const* sends = child (ch, "Sends")) {
			for (auto const* s : sends->children ()) {
				if (s->name () != "Send") {
					continue;
				}
				auto target = _route_of_channel.find (attr (s, "destination"));
				if (target == _route_of_channel.end () || target->second == route) {
					continue;
				}
				if (XMLNode const* en = child (s, "Enable")) {
					if (!flag (en, "value", true)) {
						continue;
					}
				}
				bool const pre = attr (s, "type", "post") == "pre";
				route->add_aux_send (target->second, pre ? route->before_processor_for_placement (PreFader) : route->before_processor_for_placement (PostFader));
				std::shared_ptr<Send> send = route->internal_send_for (target->second);
				if (!send) {
					continue;
				}
				if (XMLNode const* v = child (s, "Volume")) {
					send->gain_control ()->set_value (volume_coeff (v, num (v, "value", 1)), Controllable::NoGroup);
					_params[attr (v, "id")] = Param { route, Param::SendVolume, v, send };
				}
				if (child (s, "Pan")) {
					note (_("Send pan was not imported."));
				}
			}
		}
	}
}

/* ---- timelines ---------------------------------------------------------- */

void
Importer::timeline (XMLNode const* n, Context const& parent)
{
	Context ctx = parent;
	ctx.unit    = unit_of (n, parent.unit);
	if (has_attr (n, "track")) {
		auto t = _route_of_track.find (attr (n, "track"));
		ctx.route = t != _route_of_track.end () ? t->second : std::shared_ptr<Route> ();
	}

	string const what = n->name ();
	if (what == "Lanes") {
		for (auto const* c : n->children ()) {
			timeline (c, ctx);
		}
	} else if (what == "Clips") {
		clips (n, ctx);
	} else if (what == "Audio") {
		audio (n, ctx);
	} else if (what == "Warps") {
		warps (n, ctx);
	} else if (what == "Notes") {
		notes (n, ctx);
	} else if (what == "Points") {
		points (n, ctx);
	} else if (what == "Markers" || what == "markers") {
		markers (n, ctx);
	} else if (what == "Video") {
		note (_("Video was not imported."));
	} else if (what == "ClipSlot") {
		note (_("Clip launcher slots were not imported."));
	}
}

void
Importer::clips (XMLNode const* n, Context const& ctx)
{
	for (auto const* c : n->children ()) {
		if (c->name () == "Clip") {
			clip (c, ctx);
		}
	}
}

void
Importer::clip (XMLNode const* c, Context const& parent)
{
	/* the content: a child timeline or a reference to one */
	XMLNode const* content = 0;
	for (auto const* ch : c->children ()) {
		content = ch;
		break;
	}
	if (!content && has_attr (c, "reference")) {
		auto r  = _by_id.find (attr (c, "reference"));
		content = r != _by_id.end () ? r->second : 0;
	}
	if (!content) {
		return;
	}

	Unit const cu         = has_attr (c, "contentTimeUnit") ? (attr (c, "contentTimeUnit") == "seconds" ? Seconds_ : Beats_) : unit_of (content, parent.unit);
	double const t        = num (c, "time");
	double const play_from = num (c, "playStart", 0);
	timepos_t const start = at (parent, t);
	timepos_t       end;
	if (has_attr (c, "duration")) {
		end = at (parent, t + num (c, "duration"));
	} else if (has_attr (c, "playStop")) {
		/* duration given in content time */
		Context tmp = parent;
		tmp.unit    = cu;
		tmp.origin  = start;
		end         = at (tmp, num (c, "playStop") - play_from);
	} else {
		return;
	}
	timepos_t const win_start = std::max (start, parent.win_start);
	timepos_t const win_end   = std::min (end, parent.win_end);
	if (win_end <= win_start) {
		return;
	}

	Context ctx  = parent;
	ctx.muted    = parent.muted || !flag (c, "enable", true);
	if (has_attr (c, "name")) {
		ctx.clip_name = attr (c, "name");
	}

	_clip_regions.clear ();

	bool const looped = has_attr (c, "loopStart") && has_attr (c, "loopEnd") && num (c, "loopEnd") > num (c, "loopStart");
	if (!looped) {
		Context seg  = ctx;
		seg.win_start = win_start;
		seg.win_end   = win_end;
		content_segment (content, seg, cu, play_from, 0, start, win_end);
	} else {
		/* play from playStart to loopEnd, then loopStart..loopEnd again and again */
		double const ls = num (c, "loopStart"), le = num (c, "loopEnd");
		double       from = play_from < le ? play_from : ls;
		timepos_t    pos  = start;
		for (int guard = 0; pos < end && guard < 10000; ++guard) {
			Context tmp = ctx;
			tmp.unit    = cu;
			tmp.origin  = pos;
			timepos_t const seg_end = std::min (at (tmp, le - from), end);
			Context seg   = ctx;
			seg.win_start = std::max (pos, win_start);
			seg.win_end   = std::min (seg_end, win_end);
			if (seg.win_start < seg.win_end) {
				content_segment (content, seg, cu, from, 0, pos, seg.win_end);
			}
			if (seg_end <= pos) {
				break;
			}
			pos  = seg_end;
			from = ls;
		}
	}

	/* fades belong to the clip's outer edges */
	double const fin = num (c, "fadeInTime", 0), fout = num (c, "fadeOutTime", 0);
	if ((fin > 0 || fout > 0) && !_clip_regions.empty ()) {
		Unit const fu = has_attr (c, "fadeTimeUnit") ? (attr (c, "fadeTimeUnit") == "seconds" ? Seconds_ : Beats_) : parent.unit;
		auto len = [&] (double d, timepos_t const& from) -> samplecnt_t {
			Context tmp = parent;
			tmp.unit    = fu;
			tmp.origin  = from;
			return from.distance (at (tmp, d)).samples ();
		};
		for (auto const& r : _clip_regions) {
			std::shared_ptr<AudioRegion> ar = std::dynamic_pointer_cast<AudioRegion> (r);
			if (!ar) {
				continue;
			}
			if (fin > 0 && r->position ().samples () == win_start.samples ()) {
				ar->set_fade_in_length (len (fin, win_start));
				ar->set_fade_in_active (true);
			}
			if (fout > 0 && (r->position () + r->length ()).samples () >= win_end.samples ()) {
				samplecnt_t const l = len (fout, win_end);
				ar->set_fade_out_length (l);
				ar->set_fade_out_active (true);
			}
		}
		if (fin < 0 || fout < 0) {
			note (_("Crossfades were imported as plain overlaps."));
		}
	}
	_clip_regions.clear ();
}

/* `content`, from content time `c_from` (unit `cu`), starts playing at `at_pos` */
void
Importer::content_segment (XMLNode const* content, Context ctx, Unit cu, double c_from, double, timepos_t const& at_pos, timepos_t const&)
{
	/* origin: where content time 0 would be */
	Context tmp = ctx;
	tmp.unit    = cu;
	tmp.origin  = at_pos;
	if (cu == Beats_) {
		ctx.origin = timepos_t (at_pos.beats () - Beats::from_double (c_from));
	} else {
		ctx.origin = timepos_t (at_pos.samples () - (samplepos_t) llrint (c_from * _sr));
	}
	ctx.unit = cu;
	timeline (content, ctx);
}

vector<std::shared_ptr<Source>>
Importer::audio_sources (string const& file)
{
	auto i = _audio_sources.find (file);
	if (i != _audio_sources.end ()) {
		return i->second;
	}
	vector<std::shared_ptr<Source>> srcs;

	ImportStatus status;
	status.total                   = 1;
	status.current                 = 1;
	status.quality                 = SrcBest;
	status.freeze                  = false;
	status.replace_existing_source = false;
	status.split_midi_channels     = false;
	status.import_markers          = false;
	status.midi_track_name_source  = ARDOUR::SMFFileAndTrackName;
	status.paths.push_back (file);
	_session.import_files (status);
	if (!status.cancel) {
		for (auto const& s : status.sources) {
			srcs.push_back (s);
		}
	}
	_audio_sources[file] = srcs;
	return srcs;
}

void
Importer::audio (XMLNode const* a, Context const& ctx)
{
	std::shared_ptr<AudioTrack> track = std::dynamic_pointer_cast<AudioTrack> (ctx.route);
	if (!track) {
		return;
	}
	XMLNode const* f = child (a, "File");
	if (!f) {
		return;
	}
	string const rel = attr (f, "path");
	string       file;
	if (flag (f, "external", false)) {
		file = rel;
		if (!Glib::path_is_absolute (file)) {
			file = Glib::build_filename (_dir, rel);
		}
	} else {
		string r = rel;
		std::replace (r.begin (), r.end (), '\\', '/');
		file = Glib::build_filename (_dir, r);
	}
	if (!Glib::file_test (file, Glib::FILE_TEST_EXISTS)) {
		note (string_compose (_("Audio file \"%1\" is missing."), rel));
		return;
	}

	vector<std::shared_ptr<Source>> srcs = audio_sources (file);
	if (srcs.empty ()) {
		note (string_compose (_("Audio file \"%1\" could not be imported."), rel));
		return;
	}

	/* the file plays from ctx.origin (its time 0) within the window */
	timepos_t const file_start = ctx.origin;
	timepos_t const file_end   = timepos_t (file_start.samples () + srcs.front ()->length ().samples ());
	timepos_t const pos        = std::max (ctx.win_start, file_start);
	timepos_t const end        = std::min (ctx.win_end, file_end);
	if (end <= pos) {
		return;
	}

	SourceList sl (srcs.begin (), srcs.end ());
	PropertyList whole;
	whole.add (Properties::start, timepos_t (AudioTime));
	whole.add (Properties::length, srcs.front ()->length ());
	whole.add (Properties::name, PBD::basename_nosuffix (file));
	whole.add (Properties::whole_file, true);
	std::shared_ptr<Region> wr = RegionFactory::create (sl, whole);

	PropertyList plist;
	plist.add (Properties::start, timepos_t (file_start.distance (pos).samples ()));
	plist.add (Properties::length, timecnt_t (pos.distance (end).samples ()));
	plist.add (Properties::name, ctx.clip_name.empty () ? string (PBD::basename_nosuffix (file)) : ctx.clip_name);
	std::shared_ptr<Region> r = RegionFactory::create (wr, plist);
	if (!r) {
		return;
	}
	if (ctx.muted) {
		r->set_muted (true);
	}
	track->playlist ()->add_region (r, timepos_t (pos.samples ()));
	_clip_regions.push_back (r);
	++_result.regions;

	/* a mono track gets the first channel of a stereo file and vice versa */
	if (srcs.size () != track->input ()->n_ports ().n_audio ()) {
		note (string_compose (_("\"%1\": the audio file has %2 channel(s), the track %3."), track->name (), srcs.size (), track->input ()->n_ports ().n_audio ()));
	}
}

void
Importer::warps (XMLNode const* w, Context const& ctx)
{
	/* time (ctx unit) <- contentTime (contentTimeUnit) */
	vector<std::pair<double, double>> pts;
	XMLNode const*                    content = 0;
	for (auto const* c : w->children ()) {
		if (c->name () == "Warp") {
			pts.push_back (std::make_pair (num (c, "time"), num (c, "contentTime")));
		} else if (!content) {
			content = c;
		}
	}
	if (!content || pts.size () < 2) {
		if (content) {
			timeline (content, ctx);
		}
		return;
	}
	Unit const cu = attr (w, "contentTimeUnit") == "beats" ? Beats_ : Seconds_;

	/* the content plays at its own speed from the first warp point; tell the
	 * user when the source DAW stretched it */
	timepos_t const a0 = at (ctx, pts.front ().first), a1 = at (ctx, pts.back ().first);
	Context         c  = ctx;
	c.unit             = cu;
	c.origin           = a0;
	timepos_t const b1 = at (c, pts.back ().second - pts.front ().second);
	double const    ratio = (double) a0.distance (a1).samples () / std::max ((samplecnt_t) 1, a0.distance (b1).samples ());
	if (fabs (ratio - 1.0) > 0.002) {
		note (string_compose (_("Time-stretched clip%1 plays at its original speed (stretch %2%%)."),
		                      ctx.clip_name.empty () ? string () : " \"" + ctx.clip_name + "\"", (int) lrint (ratio * 100)));
	}
	content_segment (content, ctx, cu, pts.front ().second, 0, a0, ctx.win_end);
}

void
Importer::notes (XMLNode const* n, Context const& ctx)
{
	std::shared_ptr<MidiTrack> track = std::dynamic_pointer_cast<MidiTrack> (ctx.route);
	if (!track) {
		return;
	}

	struct N { timepos_t on, off; uint8_t ch, key, vel, rel; };
	vector<N> ns;
	timepos_t lo = timepos_t::max (BeatTime), hi (BeatTime);
	for (auto const* c : n->children ()) {
		if (c->name () != "Note") {
			continue;
		}
		N x;
		x.on  = at (ctx, num (c, "time"));
		x.off = at (ctx, num (c, "time") + num (c, "duration"));
		x.ch  = (uint8_t) std::min (15, std::max (0, (int) num (c, "channel")));
		x.key = (uint8_t) std::min (127, std::max (0, (int) num (c, "key")));
		x.vel = (uint8_t) std::min (127L, std::max (1L, lrint (num (c, "vel", 0.8) * 127)));
		x.rel = (uint8_t) std::min (127L, std::max (0L, lrint (num (c, "rel", 0.5) * 127)));
		if (x.on < ctx.win_start || x.on >= ctx.win_end) {
			continue; /* outside the clip */
		}
		x.off = std::min (x.off, ctx.win_end);
		if (!c->children ().empty ()) {
			note (_("Note expressions were not imported."));
		}
		ns.push_back (x);
		lo = std::min (lo, x.on);
		hi = std::max (hi, x.off);
	}

	/* the region spans the clip, or the notes when they are not in a clip */
	timepos_t const start = ctx.win_end == timepos_t::max (BeatTime) ? lo : ctx.win_start;
	timepos_t const end   = ctx.win_end == timepos_t::max (BeatTime) ? hi : ctx.win_end;
	if (ns.empty () && ctx.win_end == timepos_t::max (BeatTime)) {
		return;
	}
	if (end <= start) {
		return;
	}
	Beats const b0 = start.beats ();
	Beats const len = end.beats () - b0;

	std::shared_ptr<MidiSource> src = _session.create_midi_source_for_session (ctx.clip_name.empty () ? track->name () : ctx.clip_name);
	if (!src) {
		return;
	}
	{
		Source::WriterLock lm (src->mutex ());
		src->mark_streaming_write_started (lm);
		std::stable_sort (ns.begin (), ns.end (), [] (N const& a, N const& b) { return a.on < b.on; });
		/* write note-ons and note-offs in time order */
		vector<std::pair<Beats, std::array<uint8_t, 3>>> ev;
		for (auto const& x : ns) {
			ev.push_back (std::make_pair (x.on.beats () - b0, std::array<uint8_t, 3> { (uint8_t) (0x90 | x.ch), x.key, x.vel }));
			ev.push_back (std::make_pair (x.off.beats () - b0, std::array<uint8_t, 3> { (uint8_t) (0x80 | x.ch), x.key, x.rel }));
		}
		std::stable_sort (ev.begin (), ev.end (), [] (auto const& a, auto const& b) {
			if (a.first != b.first) {
				return a.first < b.first;
			}
			return (a.second[0] & 0xf0) == 0x80 && (b.second[0] & 0xf0) == 0x90; /* offs first */
		});
		for (auto const& e : ev) {
			src->append_event_beats (lm, Evoral::Event<Beats> (Evoral::MIDI_EVENT, e.first, 3, e.second.data ()));
		}
		src->mark_streaming_write_completed (lm, timecnt_t (len));
	}

	SourceList   sl (1, src);
	PropertyList whole;
	whole.add (Properties::start, timepos_t (Beats ()));
	whole.add (Properties::length, timecnt_t (len));
	whole.add (Properties::name, src->name ());
	whole.add (Properties::whole_file, true);
	std::shared_ptr<Region> wr = RegionFactory::create (sl, whole);
	std::shared_ptr<Region> r  = RegionFactory::create (wr, false);
	if (!r) {
		return;
	}
	if (!ctx.clip_name.empty ()) {
		r->set_name (ctx.clip_name);
	}
	if (ctx.muted) {
		r->set_muted (true);
	}
	track->playlist ()->add_region (r, start);
	++_result.regions;
}

void
Importer::points (XMLNode const* p, Context const& ctx)
{
	XMLNode const* target = child (p, "Target");
	if (!target) {
		return;
	}
	if (attr (target, "expression") == "gain" && !_clip_regions.empty ()) {
		/* clip gain: a constant value becomes the regions' gain */
		vector<double> vals;
		for (auto const* c : p->children ()) {
			if (c->name () == "RealPoint") {
				vals.push_back (num (c, "value"));
			}
		}
		if (!vals.empty ()) {
			gain_t const g = volume_coeff (p, vals.front ());
			for (auto const& r : _clip_regions) {
				if (std::shared_ptr<AudioRegion> ar = std::dynamic_pointer_cast<AudioRegion> (r)) {
					ar->set_scale_amplitude (g);
				}
			}
			if (std::any_of (vals.begin (), vals.end (), [&] (double v) { return fabs (v - vals.front ()) > 1e-6; })) {
				note (_("Clip gain envelopes were imported as a constant gain (their first value)."));
			}
		}
		return;
	}
	if (has_attr (target, "expression")) {
		note (string_compose (_("Automation of \"%1\" (%2) was not imported."), ctx.route ? ctx.route->name () : string ("?"), attr (target, "expression")));
		return;
	}
	auto i = _params.find (attr (target, "parameter"));
	if (i == _params.end ()) {
		note (_("Plug-in parameter automation was not imported."));
		return;
	}
	Param const& prm = i->second;

	std::shared_ptr<AutomationControl> ac;
	switch (prm.what) {
		case Param::Volume:
			ac = prm.route->gain_control ();
			break;
		case Param::Pan:
			ac = prm.route->pan_azimuth_control ();
			break;
		case Param::Mute:
			ac = prm.route->mute_control ();
			break;
		case Param::SendVolume:
			ac = prm.send ? prm.send->gain_control () : std::shared_ptr<AutomationControl> ();
			break;
	}
	if (!ac) {
		return;
	}
	std::shared_ptr<AutomationList> al = ac->alist ();
	if (!al) {
		return;
	}
	al->freeze ();
	al->clear ();
	XMLNode const* unit_node = has_attr (p, "unit") ? p : prm.node;
	int            n         = 0;
	for (auto const* c : p->children ()) {
		double v;
		if (c->name () == "RealPoint") {
			v = num (c, "value");
			v = (prm.what == Param::Pan) ? pan_azimuth (unit_node, v) : volume_coeff (unit_node, v);
		} else if (c->name () == "BoolPoint") {
			v = flag (c, "value", false) ? 1 : 0;
		} else {
			continue;
		}
		timepos_t const pos = at (ctx, num (c, "time"));
		al->editor_add (timepos_t (pos.samples ()), v, false);
		++n;
	}
	al->thaw ();
	if (n > 0) {
		ac->set_automation_state (Play);
	}
}

void
Importer::markers (XMLNode const* m, Context const& ctx)
{
	Context c = ctx;
	c.unit    = unit_of (m, ctx.unit);
	for (auto const* k : m->children ()) {
		if (k->name () != "Marker") {
			continue;
		}
		timepos_t const pos = at (c, num (k, "time"));
		_session.locations ()->add (new Location (_session, pos, pos, attr (k, "name", _("mark")), Location::IsMark));
		++_result.markers;
	}
}

} /* anonymous namespace */

DAWProjectImport::Result
DAWProjectImport::import (Session& session, string const& path)
{
	Result   r;
	Importer imp (session, r);
	r.ok = imp.run (path);
	return r;
}

DAWProjectImport::Result
DAWProjectImport::import_xml (Session& session, string const& xml, string const& base_dir)
{
	Result   r;
	Importer imp (session, r);
	r.ok = imp.run_xml (xml, base_dir);
	return r;
}

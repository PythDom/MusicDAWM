/*
 * MusicDAWM Drum Studio, see ardour/drum_studio.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <glib/gstdio.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include "pbd/basename.h"
#include "pbd/error.h"
#include "pbd/file_utils.h"
#include "pbd/stateful_diff_command.h"
#include "pbd/xml++.h"

#include "temporal/tempo.h"

#include "ardour/audioengine.h"
#include "ardour/chan_count.h"
#include "ardour/drum_studio.h"
#include "ardour/filesystem_paths.h"
#include "ardour/location.h"
#include "ardour/midi_region.h"
#include "ardour/dB.h"
#include "ardour/gain_control.h"
#include "ardour/midi_track.h"
#include "ardour/monitor_control.h"
#include "ardour/playlist.h"
#include "ardour/plugin_manager.h"
#include "ardour/region_factory.h"
#include "ardour/search_paths.h"
#include "ardour/session.h"
#include "ardour/smf_source.h"
#include "ardour/source_factory.h"
#include "ardour/triggerbox.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using namespace PBD;
using namespace Temporal;
using std::string;
using std::vector;

namespace {

vector<DrumStudio::Part>  _parts;
vector<DrumStudio::Style> _styles;
bool                      _loaded = false;

const char* const drum_studio_node = X_("DrumStudio");

/* AVL kits in x42-avldrums, in the order offered */
const char* const avl_kits[] = {
	"http://gareus.org/oss/lv2/avldrums#BlackPearl",
	"http://gareus.org/oss/lv2/avldrums#RedZeppelin",
	"http://gareus.org/oss/lv2/avldrums#BlondeBop",
	"http://gareus.org/oss/lv2/avldrums#BuskmansHoliday",
};

} // namespace

DrumStudio::Groove const*
DrumStudio::Style::groove (string const& part, int complexity) const
{
	for (auto const& g : grooves) {
		if (g.part == part && g.complexity == complexity) {
			return &g;
		}
	}
	return 0;
}

void
DrumStudio::load_library ()
{
	if (_loaded) {
		return;
	}
	_loaded = true;

	string index;
	Searchpath sp (ardour_data_search_path ());
	sp.add_subdirectory_to_paths (X_("drumstudio"));
	sp.add_subdirectory_to_paths (X_("grooves"));

	if (!find_file (sp, X_("index.xml"), index)) {
		/* a source tree (ardev, tests) has the library at share/drumstudio */
		string const top = Glib::getenv (X_("MUSICDAWM_DRUMSTUDIO_DIR"));
		if (top.empty () || !Glib::file_test (Glib::build_filename (top, X_("index.xml")), Glib::FILE_TEST_EXISTS)) {
			warning << _("Drum Studio: groove library not found") << endmsg;
			return;
		}
		index = Glib::build_filename (top, X_("index.xml"));
	}

	string const dir = Glib::path_get_dirname (index);
	XMLTree tree;
	if (!tree.read (index) || !tree.root () || tree.root ()->name () != X_("DrumStudioGrooves")) {
		error << string_compose (_("Drum Studio: cannot read %1"), index) << endmsg;
		return;
	}

	for (auto const& node : tree.root ()->children ()) {
		if (node->name () == X_("Part")) {
			Part p;
			node->get_property (X_("key"), p.key);
			node->get_property (X_("name"), p.name);
			_parts.push_back (p);
		} else if (node->name () == X_("Style")) {
			Style s;
			node->get_property (X_("key"), s.key);
			node->get_property (X_("name"), s.name);
			node->get_property (X_("meter-num"), s.meter_num);
			node->get_property (X_("meter-den"), s.meter_den);
			node->get_property (X_("tempo"), s.tempo);
			for (auto const& gn : node->children ()) {
				Groove g;
				string file;
				gn->get_property (X_("part"), g.part);
				gn->get_property (X_("complexity"), g.complexity);
				gn->get_property (X_("bars"), g.bars);
				gn->get_property (X_("file"), file);
				g.path = Glib::build_filename (dir, file);
				s.grooves.push_back (g);
			}
			_styles.push_back (s);
		}
	}
}

vector<DrumStudio::Part> const&
DrumStudio::parts ()
{
	load_library ();
	return _parts;
}

vector<DrumStudio::Style> const&
DrumStudio::styles ()
{
	load_library ();
	return _styles;
}

DrumStudio::Style const*
DrumStudio::style (string const& key)
{
	for (auto const& s : styles ()) {
		if (s.key == key) {
			return &s;
		}
	}
	return 0;
}

int
DrumStudio::part_index (string const& key)
{
	vector<Part> const& p (parts ());
	for (size_t i = 0; i < p.size (); ++i) {
		if (p[i].key == key) {
			return i;
		}
	}
	return -1;
}

bool
DrumStudio::part_loops (int part_index)
{
	return part_index >= 0 && part_index < (int)parts ().size () && parts ()[part_index].key != X_("ending");
}

bool
DrumStudio::part_is_fill (int part_index)
{
	return part_index >= 0 && part_index < (int)parts ().size () && parts ()[part_index].key == X_("fill");
}

int
DrumStudio::slot (int part_index, int complexity)
{
	return part_index * complexities + (complexity - 1);
}

int
DrumStudio::slot_count ()
{
	return parts ().size () * complexities;
}

void
DrumStudio::cell_of_slot (int slot, int& part_index, int& complexity)
{
	part_index = slot / complexities;
	complexity = slot % complexities + 1;
}

vector<std::shared_ptr<PluginInfo>>
DrumStudio::bundled_kits ()
{
	vector<std::shared_ptr<PluginInfo>> kits;
#ifdef LV2_SUPPORT
	PluginInfoList const& lv2 (PluginManager::instance ().lv2_plugin_info ());
	for (auto const* uri : avl_kits) {
		for (auto const& pi : lv2) {
			if (pi->unique_id == uri) {
				kits.push_back (pi);
				break;
			}
		}
	}
#endif
	return kits;
}

static void
configure_slot (std::shared_ptr<Trigger> t, int part_index)
{
	/* Switches are sent ahead of their boundary (DrumPerformer), so one beat
	 * of quantization makes each groove start exactly there. OneShot keeps a
	 * running groove going when its own cue comes again.
	 */
	t->set_launch_style (Trigger::OneShot);
	t->set_quantization (BBT_Offset (0, 1, 0));
	t->set_follow_action0 (FollowAction (DrumStudio::part_loops (part_index) ? FollowAction::Again : FollowAction::Stop));
	t->set_follow_action_probability (0);
	t->set_cue_isolated (false);
}

int
DrumStudio::load_style (std::shared_ptr<MidiTrack> track, string const& style_key, Feel feel)
{
	Style const* s = style (style_key);
	if (!track || !s) {
		return -1;
	}
	std::shared_ptr<TriggerBox> tb = track->triggerbox ();
	if (!tb) {
		return -1;
	}

	/* the default box has 16 slots; Drum Studio needs one per cell */
	{
		PBD::Mutex::Lock lm (AudioEngine::instance ()->process_lock ());
		int n = 0;
		while (tb->trigger (n)) {
			++n;
		}
		for (; n < slot_count (); ++n) {
			tb->add_trigger (std::make_shared<MIDITrigger> (n, *tb));
		}
	}

	int loaded = 0;
	for (size_t p = 0; p < parts ().size (); ++p) {
		for (int c = 1; c <= complexities; ++c) {
			Groove const* g = s->groove (parts ()[p].key, c);
			int const     n = slot (p, c);
			if (!g || !Glib::file_test (g->path, Glib::FILE_TEST_EXISTS)) {
				continue;
			}
			string const file = groove_file (g->path, feel);
			if (file.empty ()) {
				continue;
			}
			tb->set_from_path (n, file);
			std::shared_ptr<Trigger> t = tb->trigger (n);
			if (t) {
				configure_slot (t, p);
				++loaded;
			}
		}
	}

	XMLNode* node = track->extra_xml (drum_studio_node, true);
	node->set_property (X_("style"), style_key);
	node->set_property (X_("feel"), (int)feel);
	node->set_property (X_("version"), 1);
	return loaded == slot_count () ? 0 : -1;
}

void
DrumStudio::play_timeline_too (std::shared_ptr<MidiTrack> track)
{
	/* With clips in its slots, automatic monitoring would never play the
	 * track's regions (triggerbox-overrides-disk-monitoring), so flattened
	 * drums would be silent. Input + disk plays the regions, the cues and a
	 * live drum pad. */
	if (track && track->monitoring_control ()->monitoring_choice () == MonitorAuto) {
		track->monitoring_control ()->set_value (MonitorCue, PBD::Controllable::NoGroup);
	}
}

/* ---- MusicDAWM: half-time and double-time grooves ---- */

namespace {

struct SmfEvent {
	uint64_t      at;    /* ticks */
	int           order; /* within the file, to keep simultaneous events in order */
	vector<uint8_t> bytes; /* status byte included (running status expanded) */
};

bool
read_varlen (vector<uint8_t> const& d, size_t& i, size_t end, uint32_t& v)
{
	v = 0;
	for (int k = 0; k < 4; ++k) {
		if (i >= end) {
			return false;
		}
		uint8_t const b = d[i++];
		v               = (v << 7) | (b & 0x7f);
		if (!(b & 0x80)) {
			return true;
		}
	}
	return false;
}

void
write_varlen (vector<uint8_t>& o, uint32_t v)
{
	uint8_t buf[5];
	int     n = 0;
	buf[n++]  = v & 0x7f;
	while (v >>= 7) {
		buf[n++] = 0x80 | (v & 0x7f);
	}
	while (n--) {
		o.push_back (buf[n]);
	}
}

bool
is_note_off (vector<uint8_t> const& e)
{
	return e.size () == 3 && ((e[0] & 0xf0) == 0x80 || ((e[0] & 0xf0) == 0x90 && e[2] == 0));
}

} // namespace

bool
DrumStudio::convert_feel (string const& from, string const& to, Feel feel)
{
	FILE* f = g_fopen (from.c_str (), "rb");
	if (!f) {
		return false;
	}
	vector<uint8_t> d;
	uint8_t         buf[4096];
	size_t          got;
	while ((got = fread (buf, 1, sizeof (buf), f)) > 0) {
		d.insert (d.end (), buf, buf + got);
	}
	fclose (f);
	/* one-track files (the library is format 0) with ticks per quarter */
	if (d.size () < 22 || memcmp (d.data (), "MThd", 4) != 0 || (d[12] & 0x80)) {
		return false;
	}
	uint16_t const ntrk = (d[10] << 8) | d[11];
	size_t         i    = 8 + ((d[4] << 24) | (d[5] << 16) | (d[6] << 8) | d[7]);
	if (ntrk != 1 || i + 8 > d.size () || memcmp (d.data () + i, "MTrk", 4) != 0) {
		return false;
	}
	size_t const tlen = (d[i + 4] << 24) | (d[i + 5] << 16) | (d[i + 6] << 8) | d[i + 7];
	size_t const end  = std::min (d.size (), i + 8 + tlen);
	i += 8;

	vector<SmfEvent> ev;
	uint64_t         at = 0, eot = 0;
	uint8_t          status = 0;
	int              order  = 0;
	while (i < end) {
		uint32_t delta;
		if (!read_varlen (d, i, end, delta) || i >= end) {
			return false;
		}
		at += delta;
		SmfEvent e;
		e.at    = at;
		e.order = order++;
		uint8_t b = d[i];
		if (b == 0xff) { /* meta */
			if (i + 2 > end) {
				return false;
			}
			uint8_t const type = d[i + 1];
			size_t        j    = i + 2;
			uint32_t      len;
			if (!read_varlen (d, j, end, len) || j + len > end) {
				return false;
			}
			if (type == 0x2f) {
				eot = at;
				break;
			}
			e.bytes.assign (d.begin () + i, d.begin () + j + len);
			i = j + len;
		} else if (b == 0xf0 || b == 0xf7) { /* sysex */
			size_t   j = i + 1;
			uint32_t len;
			if (!read_varlen (d, j, end, len) || j + len > end) {
				return false;
			}
			e.bytes.assign (d.begin () + i, d.begin () + j + len);
			i = j + len;
		} else {
			if (b & 0x80) {
				status = b;
				++i;
			}
			if (!status) {
				return false;
			}
			int const n = ((status & 0xf0) == 0xc0 || (status & 0xf0) == 0xd0) ? 1 : 2;
			if (i + n > end) {
				return false;
			}
			e.bytes.push_back (status);
			e.bytes.insert (e.bytes.end (), d.begin () + i, d.begin () + i + n);
			i += n;
		}
		ev.push_back (e);
	}
	if (eot == 0) {
		eot = at;
	}

	vector<SmfEvent> out;
	if (feel == FeelHalf) {
		for (auto e : ev) {
			e.at *= 2;
			out.push_back (e);
		}
		eot *= 2;
	} else if (feel == FeelDouble) {
		/* twice as fast, twice: the groove keeps its length in bars */
		uint64_t const half = eot / 2;
		for (int rep = 0; rep < 2; ++rep) {
			for (auto e : ev) {
				if (rep == 1 && e.bytes[0] == 0xff) {
					continue; /* meta events (names, tempo) once */
				}
				e.at    = e.at / 2 + rep * half;
				e.order += rep * order;
				out.push_back (e);
			}
		}
	} else {
		out = ev;
	}
	/* at the same tick: note-offs first, then in file order */
	std::stable_sort (out.begin (), out.end (), [] (SmfEvent const& a, SmfEvent const& b) {
		if (a.at != b.at) {
			return a.at < b.at;
		}
		if (is_note_off (a.bytes) != is_note_off (b.bytes)) {
			return is_note_off (a.bytes);
		}
		return a.order < b.order;
	});

	vector<uint8_t> trk;
	uint64_t        last = 0;
	for (auto const& e : out) {
		write_varlen (trk, (uint32_t)(e.at - last));
		last = e.at;
		trk.insert (trk.end (), e.bytes.begin (), e.bytes.end ());
	}
	write_varlen (trk, (uint32_t)(eot > last ? eot - last : 0));
	trk.push_back (0xff);
	trk.push_back (0x2f);
	trk.push_back (0x00);

	vector<uint8_t> o (d.begin (), d.begin () + 14); /* the header as it was */
	o.insert (o.end (), { 'M', 'T', 'r', 'k' });
	uint32_t const n = trk.size ();
	o.insert (o.end (), { (uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n });
	o.insert (o.end (), trk.begin (), trk.end ());

	string const tmp = to + ".part";
	FILE*        w   = g_fopen (tmp.c_str (), "wb");
	if (!w) {
		return false;
	}
	bool const ok = fwrite (o.data (), 1, o.size (), w) == o.size ();
	fclose (w);
	if (!ok || g_rename (tmp.c_str (), to.c_str ()) != 0) {
		g_unlink (tmp.c_str ());
		return false;
	}
	return true;
}

string
DrumStudio::groove_file (string const& path, Feel feel)
{
	if (feel == FeelNormal) {
		return path;
	}
	/* <cache>/drumstudio/<parent>-<name>-half.mid (the parent is the style folder) */
	string const dir = Glib::build_filename (user_cache_directory (), X_("drumstudio"));
	g_mkdir_with_parents (dir.c_str (), 0755);
	string const name = Glib::path_get_basename (Glib::path_get_dirname (path)) + "-" + PBD::basename_nosuffix (path)
	                    + (feel == FeelHalf ? "-half.mid" : "-double.mid");
	string const out = Glib::build_filename (dir, name);
	GStatBuf     a, b;
	if (g_stat (out.c_str (), &b) == 0 && g_stat (path.c_str (), &a) == 0 && b.st_mtime >= a.st_mtime) {
		return out;
	}
	return convert_feel (path, out, feel) ? out : string ();
}

std::shared_ptr<Region>
DrumStudio::groove_region (Session& session, string const& style_key, int part_index, int complexity, Feel feel)
{
	Style const* st = style (style_key);
	if (!st || part_index < 0 || part_index >= (int)parts ().size ()) {
		return std::shared_ptr<Region> ();
	}
	Groove const* g = st->groove (parts ()[part_index].key, complexity);
	string const file = g ? groove_file (g->path, feel) : string ();
	if (file.empty () || !SMFSource::valid_midi_file (file)) {
		return std::shared_ptr<Region> ();
	}
	try {
		std::shared_ptr<SMFSource> ms = std::dynamic_pointer_cast<SMFSource> (
		    SourceFactory::createExternal (DataType::MIDI, session, file, 0, Source::Flag (0), false));
		if (!ms) {
			return std::shared_ptr<Region> ();
		}
		PBD::PropertyList plist;
		plist.add (Properties::start, timepos_t (Temporal::Beats ()));
		plist.add (Properties::length, ms->length ());
		plist.add (Properties::name, string_compose ("%1 %2 %3", st->name, parts ()[part_index].name, complexity));
		plist.add (Properties::layer, 0);
		std::shared_ptr<Region> r = RegionFactory::create (std::dynamic_pointer_cast<Source> (ms), plist, false);
		if (r) {
			r->set_position (timepos_t (Temporal::Beats ()));
		}
		return r;
	} catch (...) {
		return std::shared_ptr<Region> ();
	}
}

std::shared_ptr<MidiTrack>
DrumStudio::create_drum_track (Session& session, string const& style_key, std::shared_ptr<PluginInfo> instrument, Feel feel)
{
	if (!style (style_key)) {
		return std::shared_ptr<MidiTrack> ();
	}
	if (!instrument) {
		vector<std::shared_ptr<PluginInfo>> kits (bundled_kits ());
		if (!kits.empty ()) {
			instrument = kits.front ();
		}
	}

	std::list<std::shared_ptr<MidiTrack>> tl = session.new_midi_track (
	    ChanCount (DataType::MIDI, 1), ChanCount (DataType::MIDI, 1), false,
	    instrument, 0, std::shared_ptr<RouteGroup> (), 1, _("Drums"),
	    PresentationInfo::max_order, Normal, true, false /* not on the Cue page */);

	if (tl.empty ()) {
		return std::shared_ptr<MidiTrack> ();
	}
	std::shared_ptr<MidiTrack> track = tl.front ();
	play_timeline_too (track);
	/* the AVL kits peak above 0 dBFS on accented crashes at unity gain */
	track->gain_control ()->set_value (dB_to_coefficient (-6.f), PBD::Controllable::NoGroup);
	if (load_style (track, style_key, feel)) {
		warning << string_compose (_("Drum Studio: not all grooves of '%1' could be loaded"), style_key) << endmsg;
	}
	return track;
}

bool
DrumStudio::is_drum_track (std::shared_ptr<Route> r)
{
	return r && std::dynamic_pointer_cast<MidiTrack> (r) && r->extra_xml (drum_studio_node);
}

string
DrumStudio::style_of (std::shared_ptr<Route> r)
{
	string key;
	if (r) {
		XMLNode* node = r->extra_xml (drum_studio_node);
		if (node) {
			node->get_property (X_("style"), key);
		}
	}
	return key;
}

DrumStudio::Feel
DrumStudio::feel_of (std::shared_ptr<Route> r)
{
	int f = FeelNormal;
	if (r) {
		if (XMLNode* node = r->extra_xml (drum_studio_node)) {
			node->get_property (X_("feel"), f);
		}
	}
	return (f == FeelHalf || f == FeelDouble) ? (Feel)f : FeelNormal;
}

std::shared_ptr<MidiTrack>
DrumStudio::find_drum_track (Session& session)
{
	std::shared_ptr<RouteList const> rl (session.get_routes ());
	for (auto const& r : *rl) {
		if (is_drum_track (r)) {
			return std::dynamic_pointer_cast<MidiTrack> (r);
		}
	}
	return std::shared_ptr<MidiTrack> ();
}

timepos_t
DrumStudio::next_boundary (timepos_t const& pos, Quantize q)
{
	TempoMap::SharedPtr tmap (TempoMap::use ());
	BBT_Argument const  now (tmap->bbt_at (pos));
	TempoMetric const   metric (tmap->metric_at (pos));
	BBT_Argument        b;

	switch (q) {
	case QuantizeBeat:
		b = BBT_Argument (now.reference (), metric.meter ().round_up_to_beat (now));
		if (b == now) {
			b = tmap->bbt_walk (b, BBT_Offset (0, 1, 0));
		}
		break;
	case QuantizeBar:
	case QuantizeTwoBars:
		b = metric.round_up_to_bar (now);
		if (b == now) {
			b = tmap->bbt_walk (b, BBT_Offset (1, 0, 0));
		}
		if (q == QuantizeTwoBars && (b.bars - 1) % 2 != 0) {
			b = tmap->bbt_walk (b, BBT_Offset (1, 0, 0));
		}
		break;
	}
	return timepos_t (tmap->quarters_at (b));
}

int
DrumStudio::flatten (Session& session, std::shared_ptr<MidiTrack> track, timepos_t const& start, timepos_t const& end)
{
	if (!track || !track->triggerbox () || !track->playlist ()) {
		return -1;
	}
	std::shared_ptr<TriggerBox> tb = track->triggerbox ();
	std::shared_ptr<Playlist>   pl = track->playlist ();

	/* cue markers in range, in time order */
	vector<Location*> cues;
	for (auto const& l : session.locations ()->list ()) {
		if (l->is_cue_marker () && l->start () >= start && l->start () < end) {
			cues.push_back (l);
		}
	}
	std::sort (cues.begin (), cues.end (), [] (Location* a, Location* b) { return a->start () < b->start (); });

	Style const* st = style (style_of (track));

	session.begin_reversible_command (_("flatten drum performance"));
	pl->clear_changes ();
	int       made = 0;
	timepos_t first;
	timepos_t last;

	for (size_t i = 0; i < cues.size (); ++i) {
		int const cue = cues[i]->cue_id ();
		if (cue < 0 || cue >= slot_count ()) {
			continue; /* stop-all or a cue that is not a drum cell */
		}
		std::shared_ptr<Trigger>    t    = tb->trigger (cue);
		std::shared_ptr<MidiRegion> tmpl = t ? std::dynamic_pointer_cast<MidiRegion> (t->the_region ()) : std::shared_ptr<MidiRegion> ();
		if (!tmpl) {
			continue;
		}
		int part, complexity;
		cell_of_slot (cue, part, complexity);

		timepos_t const seg_start (cues[i]->start ().beats ());
		timepos_t const seg_end ((i + 1 < cues.size ()) ? cues[i + 1]->start ().beats () : std::min (end, session.current_end ()).beats ());
		if (seg_end <= seg_start) {
			continue;
		}
		Beats const clip = tmpl->length ().beats ();
		if (clip == Beats ()) {
			continue;
		}

		for (timepos_t pos = seg_start; pos < seg_end; pos = pos + timecnt_t (clip, pos)) {
			/* fork: every copy gets its own MIDI data, so editing one leaves the others alone */
			std::shared_ptr<Region> r = RegionFactory::create (std::shared_ptr<Region const> (tmpl), true, true);
			if (!r) {
				break;
			}
			/* e.g. "Rock Verse 2" (the slot's own name follows the region it was loaded from) */
			string name = st ? string_compose (X_("%1 %2 %3"), st->name, parts ()[part].name, complexity) : t->name ();
			std::replace (name.begin (), name.end (), '/', '-'); /* "6/8" is not a valid region name */
			r->set_name (name);
			timecnt_t len (clip, pos);
			if (pos + len > seg_end) {
				len = pos.distance (seg_end); /* last copy ends where the next cell starts */
			}
			r->set_length (len);
			pl->add_region (r, pos);
			if (made == 0 || pos < first) {
				first = pos;
			}
			if (made == 0 || pos + len > last) {
				last = pos + len;
			}
			++made;
			if (!part_loops (part)) {
				break; /* an ending plays once */
			}
		}
	}

	session.add_command (new StatefulDiffCommand (pl));
	session.commit_reversible_command ();

	if (made > 0) {
		/* so that export and "zoom to session" include the drums */
		session.maybe_update_session_range (first, last);
	}

	/* the regions now play the performance; stop the slots from playing it too */
	follow_cues (track, false);
	play_timeline_too (track);
	return made;
}

void
DrumStudio::follow_cues (std::shared_ptr<MidiTrack> track, bool yn)
{
	if (!track || !track->triggerbox ()) {
		return;
	}
	for (int n = 0; n < slot_count (); ++n) {
		std::shared_ptr<Trigger> t = track->triggerbox ()->trigger (n);
		if (t) {
			t->set_cue_isolated (!yn);
		}
	}
}

/* ------------------------------------------------------------------------ */

DrumPerformer::DrumPerformer (Session& s)
	: _session (s)
	, _quantize (DrumStudio::QuantizeBar)
	, _playing (-1)
	, _pending (-1)
	, _sent (false)
	, _before_fill (-1)
{
}

void
DrumPerformer::reset ()
{
	_pending     = -1;
	_sent        = false;
	_before_fill = -1;
	_return_at   = timepos_t ();
	if (!_session.transport_rolling ()) {
		_playing = -1;
	}
}

void
DrumPerformer::schedule (int slot, timepos_t const& at)
{
	_pending    = slot;
	_pending_at = at;
	_sent       = false;
}

void
DrumPerformer::request (int part_index, int complexity)
{
	int const slot = DrumStudio::slot (part_index, complexity);
	if (slot < 0 || slot >= DrumStudio::slot_count ()) {
		return;
	}

	if (!_session.transport_rolling ()) {
		/* audition: start the groove now; the trigger starts the transport */
		std::shared_ptr<MidiTrack> track = DrumStudio::find_drum_track (_session);
		if (track && track->triggerbox ()) {
			if (TriggerBox::cue_recording ()) {
				/* not a cue, so Rec Cues would miss it: record where it starts.
				 * The process thread writes cue records only while rolling. */
				CueRecord cr (slot, _session.transport_sample ());
				TriggerBox::cue_records.write (&cr, 1);
			}
			track->triggerbox ()->bang_trigger_at (slot);
			_playing = slot;
		}
		return;
	}

	if (DrumStudio::part_is_fill (part_index)) {
		/* return to what was playing (or about to play) before the fill */
		int cur = _pending >= 0 ? _pending : _playing;
		int p, c;
		if (cur >= 0) {
			DrumStudio::cell_of_slot (cur, p, c);
			if (!DrumStudio::part_is_fill (p)) {
				_before_fill = cur;
			}
		}
	} else {
		_before_fill = -1;
	}
	_return_at = timepos_t ();

	schedule (slot, DrumStudio::next_boundary (timepos_t (_session.transport_sample ()), _quantize));
}

void
DrumPerformer::tick ()
{
	if (!_session.transport_rolling ()) {
		return;
	}
	timepos_t const     now (_session.transport_sample ());
	TempoMap::SharedPtr tmap (TempoMap::use ());

	if (_pending < 0) {
		/* follow what really plays (e.g. a groove started while stopped,
		 * whose state a transport start may have reset), so that a fill
		 * knows where to return */
		std::shared_ptr<MidiTrack> track = DrumStudio::find_drum_track (_session);
		TriggerPtr                 cp    = (track && track->triggerbox ()) ? track->triggerbox ()->currently_playing () : TriggerPtr ();
		if (cp) {
			_playing = cp->index ();
		}
	}

	if (_pending >= 0) {
		/* send in the last (meter) beat before the boundary */
		TempoMetric const metric (tmap->metric_at (_pending_at));
		Beats const       beat (Beats::ticks (Temporal::ticks_per_beat * 4 / metric.meter ().note_value ()));
		timepos_t const   window (_pending_at.beats () - beat);

		if (!_sent && now >= window) {
			if (now >= _pending_at) {
				/* the timer was late: start on the next beat instead */
				_pending_at = DrumStudio::next_boundary (now, DrumStudio::QuantizeBeat);
			}
			_session.trigger_cue_row (_pending);
			_sent = true;
		}

		if (_sent && now >= _pending_at) {
			timepos_t const started (_pending_at);
			_playing = _pending;
			_pending = -1;
			_sent    = false;

			int p, c;
			DrumStudio::cell_of_slot (_playing, p, c);
			if (DrumStudio::part_is_fill (p) && _before_fill >= 0) {
				/* a fill lasts as long as its groove (one bar; two in
				 * half-time), then the previous part comes back */
				std::shared_ptr<MidiTrack> track = DrumStudio::find_drum_track (_session);
				TriggerPtr                 ft    = (track && track->triggerbox ()) ? track->triggerbox ()->trigger (_playing) : TriggerPtr ();
				if (ft && ft->the_region ()) {
					_return_at = timepos_t (started.beats () + ft->the_region ()->length ().beats ());
				} else {
					_return_at = timepos_t (tmap->quarters_at (tmap->bbt_walk (tmap->bbt_at (started), BBT_Offset (1, 0, 0))));
				}
				schedule (_before_fill, _return_at);
				_before_fill = -1;
			}
		}
	}
}

void
DrumPerformer::set_recording (bool yn)
{
	_session.config.set_cue_record_snap_to_beat (yn);
	TriggerBox::set_cue_recording (yn);
}

bool
DrumPerformer::recording () const
{
	return TriggerBox::cue_recording ();
}

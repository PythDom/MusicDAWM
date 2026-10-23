/*
 * MusicDAWM Drum Studio, see ardour/drum_studio.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <algorithm>

#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

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
DrumStudio::load_style (std::shared_ptr<MidiTrack> track, string const& style_key)
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
			tb->set_from_path (n, g->path);
			std::shared_ptr<Trigger> t = tb->trigger (n);
			if (t) {
				configure_slot (t, p);
				++loaded;
			}
		}
	}

	XMLNode* node = track->extra_xml (drum_studio_node, true);
	node->set_property (X_("style"), style_key);
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

std::shared_ptr<MidiTrack>
DrumStudio::create_drum_track (Session& session, string const& style_key, std::shared_ptr<PluginInfo> instrument)
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
	if (load_style (track, style_key)) {
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
				/* a fill lasts one bar from where it started, then the previous part comes back */
				_return_at = timepos_t (tmap->quarters_at (tmap->bbt_walk (tmap->bbt_at (started), BBT_Offset (1, 0, 0))));
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

/*
 * MusicDAWM Drum Studio panel (a page of the Editor sidebar).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <ytkmm/box.h>

#include "pbd/compose.h"

#include "ardour/location.h"
#include "ardour/midi_track.h"
#include "ardour/plugin.h"
#include "ardour/session.h"
#include "ardour/triggerbox.h"

#include "gtkmm2ext/keyboard.h"

#include "widgets/tooltips.h"

#include "ardour_message.h"
#include "ardour_ui.h"
#include "gui_thread.h"
#include "public_editor.h"
#include "tempo_map_change.h"
#include "drum_studio_panel.h"
#include "timers.h"
#include "utils.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using namespace ArdourWidgets;
using namespace Gtk;
using namespace Gtk::Menu_Helpers;
using namespace Temporal;

/* perform keys: part key -> keyboard letter */
static struct { char const* part; guint key; } const part_keys[] = {
	{ "intro",     GDK_i },
	{ "verse",     GDK_v },
	{ "prechorus", GDK_p },
	{ "chorus",    GDK_c },
	{ "bridge",    GDK_b },
	{ "fill",      GDK_f },
	{ "ending",    GDK_e },
};

static char
key_letter (std::string const& part)
{
	for (auto const& pk : part_keys) {
		if (part == pk.part) {
			return (char) ('A' + (pk.key - GDK_a));
		}
	}
	return 0;
}

DrumStudioPanel* DrumStudioPanel::_instance = 0;

DrumStudioPanel::DrumStudioPanel ()
	: VBox (false, 6)
	, _create (_("Create Drum Track"))
	, _keys (_("Keys"), ArdourButton::Element (ArdourButton::default_elements | ArdourButton::Indicator))
	, _record (_("Record"), ArdourButton::Element (ArdourButton::default_elements | ArdourButton::Indicator))
	, _stop (_("Stop"))
	, _flatten (_("Flatten"))
	, _follow (_("Play Cues"), ArdourButton::Element (ArdourButton::default_elements | ArdourButton::Indicator))
	, _kit_index (-1)
	, _part (DrumStudio::part_index (X_("verse")))
	, _complexity (2)
	, _keys_on (false)
	, _q (DrumStudio::QuantizeBar)
{
	_instance = this;

	std::vector<DrumStudio::Style> const& styles (DrumStudio::styles ());
	std::vector<DrumStudio::Part> const&  parts (DrumStudio::parts ());

	for (auto const& st : styles) {
		_style.add_menu_elem (MenuElem (st.name, sigc::bind (sigc::mem_fun (*this, &DrumStudioPanel::set_style), st.key)));
	}

	_kits = DrumStudio::bundled_kits ();
	for (size_t i = 0; i < _kits.size (); ++i) {
		_kit.add_menu_elem (MenuElem (_kits[i]->name, sigc::bind (sigc::mem_fun (*this, &DrumStudioPanel::set_kit), (int)i)));
	}
	_kit.add_menu_elem (MenuElem (_("No instrument (choose your own later)"), sigc::bind (sigc::mem_fun (*this, &DrumStudioPanel::set_kit), -1)));

	_quantize.add_menu_elem (MenuElem (_("Next beat"), sigc::bind (sigc::mem_fun (*this, &DrumStudioPanel::set_quantize), DrumStudio::QuantizeBeat)));
	_quantize.add_menu_elem (MenuElem (_("Next bar"), sigc::bind (sigc::mem_fun (*this, &DrumStudioPanel::set_quantize), DrumStudio::QuantizeBar)));
	_quantize.add_menu_elem (MenuElem (_("Next 2 bars"), sigc::bind (sigc::mem_fun (*this, &DrumStudioPanel::set_quantize), DrumStudio::QuantizeTwoBars)));

	_create.signal_clicked.connect (sigc::mem_fun (*this, &DrumStudioPanel::create_or_load));
	_keys.signal_clicked.connect (sigc::mem_fun (*this, &DrumStudioPanel::toggle_keys));
	_record.signal_clicked.connect (sigc::mem_fun (*this, &DrumStudioPanel::toggle_record));
	_stop.signal_clicked.connect (sigc::mem_fun (*this, &DrumStudioPanel::stop));
	_flatten.signal_clicked.connect (sigc::mem_fun (*this, &DrumStudioPanel::flatten));
	_follow.signal_clicked.connect (sigc::mem_fun (*this, &DrumStudioPanel::toggle_follow_cues));

	set_tooltip (_style, _("Drum style. Changing it on an existing drum track replaces its grooves."));
	set_tooltip (_kit, _("Drum kit used when the drum track is created"));
	set_tooltip (_quantize, _("When a requested part starts"));
	set_tooltip (_keys, _("Perform keys. While this is on and Drum Studio is shown, the keys I V P C B F E pick the part and 1-4 the complexity, instead of their usual shortcuts"));
	set_tooltip (_stop, _("Stop the drums at once (the transport keeps rolling)"));
	set_tooltip (_record, _("Record the parts you play as cue markers on the timeline"));
	set_tooltip (_flatten, _("Turn the recorded cue markers into MIDI regions you can edit"));
	set_tooltip (_follow, _("Let the drum track play the cue markers. Flatten turns this off so nothing plays twice."));

	/* the grid: one row per part, one column per complexity */
	_grid.resize (parts.size (), DrumStudio::complexities + 1);
	_grid.set_spacings (2);
	_cells.resize (DrumStudio::slot_count (), 0);
	for (size_t p = 0; p < parts.size (); ++p) {
		char const k = key_letter (parts[p].key);
		Label* l = manage (new Label (k ? string_compose ("%1 (%2)", parts[p].name, k) : parts[p].name));
		l->set_alignment (0, 0.5);
		_grid.attach (*l, 0, 1, p, p + 1, FILL, SHRINK, 4, 0);
		for (int c = 1; c <= DrumStudio::complexities; ++c) {
			ArdourButton* b = manage (new ArdourButton (string_compose ("%1", c)));
			set_tooltip (*b, string_compose (_("%1, complexity %2"), parts[p].name, c));
			b->set_can_focus (false);
			b->signal_clicked.connect (sigc::bind (sigc::mem_fun (*this, &DrumStudioPanel::cell_clicked), (int)p, c));
			_grid.attach (*b, c, c + 1, p, p + 1, FILL | EXPAND, SHRINK);
			_cells[DrumStudio::slot (p, c)] = b;
		}
	}

	/* narrow layout for the sidebar */
	Table* top = manage (new Table (2, 2));
	top->set_spacings (4);
	Label* sl = manage (new Label (_("Style:")));
	Label* kl = manage (new Label (_("Kit:")));
	sl->set_alignment (0, 0.5);
	kl->set_alignment (0, 0.5);
	top->attach (*sl, 0, 1, 0, 1, FILL, SHRINK);
	top->attach (_style, 1, 2, 0, 1, FILL | EXPAND, SHRINK);
	top->attach (*kl, 0, 1, 1, 2, FILL, SHRINK);
	top->attach (_kit, 1, 2, 1, 2, FILL | EXPAND, SHRINK);

	HBox* when = manage (new HBox (false, 4));
	when->pack_start (*manage (new Label (_("Switch at:"))), false, false);
	when->pack_start (_quantize, true, true);

	Table* perform = manage (new Table (3, 2, true));
	perform->set_spacings (4);
	perform->attach (_keys, 0, 1, 0, 1);
	perform->attach (_record, 1, 2, 0, 1);
	perform->attach (_stop, 0, 1, 1, 2);
	perform->attach (_flatten, 1, 2, 1, 2);
	perform->attach (_follow, 0, 1, 2, 3);

	Label* help = manage (new Label (_("Click a cell to start or switch. With Keys on: I V P C B F E pick the part, 1-4 the complexity. "
	                                   "A fill returns to the previous part after one bar.")));
	help->set_line_wrap (true);
	help->set_alignment (0, 0.5);
	help->set_size_request (200, -1); /* wrap instead of widening the sidebar */
	_status.set_line_wrap (true);
	_status.set_alignment (0, 0.5);
	_status.set_size_request (200, -1);

	set_border_width (4);
	pack_start (*top, false, false);
	pack_start (_create, false, false);
	pack_start (_grid, false, false);
	pack_start (*when, false, false);
	pack_start (*perform, false, false);
	pack_start (*help, false, false);
	pack_start (_status, false, false);
	show_all ();

	for (auto* w : std::vector<ArdourButton*> { &_style, &_kit, &_quantize, &_create, &_keys, &_record, &_stop, &_flatten, &_follow }) {
		w->set_can_focus (false);
	}

	if (!styles.empty ()) {
		set_style (styles.front ().key);
	} else {
		_status.set_text (_("The groove library is missing (drumstudio/grooves)."));
	}
	set_kit (_kits.empty () ? -1 : 0);
	set_quantize (DrumStudio::QuantizeBar);
	update_buttons ();
}

DrumStudioPanel::~DrumStudioPanel ()
{
	_timer.disconnect ();
	if (_instance == this) {
		_instance = 0;
	}
}

void
DrumStudioPanel::set_session (Session* s)
{
	SessionHandlePtr::set_session (s);
	_session_connections.drop_connections ();
	_timer.disconnect ();
	if (!_session) {
		return;
	}

	_performer.reset (new DrumPerformer (*_session));
	_performer->set_quantize (_q);
	_session->TransportStateChange.connect (_session_connections, invalidator (*this), std::bind (&DrumStudioPanel::transport_changed, this), gui_context ());
	_session->Located.connect (_session_connections, invalidator (*this), std::bind (&DrumStudioPanel::located, this), gui_context ());
	_timer = Timers::super_rapid_connect (sigc::mem_fun (*this, &DrumStudioPanel::tick));
	/* Rec Cues disarms itself when the transport stops */
	TriggerBox::CueRecordingChanged.connect (_session_connections, invalidator (*this), std::bind (&DrumStudioPanel::update_buttons, this), gui_context ());

	std::shared_ptr<MidiTrack> t = drum_track ();
	if (t && DrumStudio::style (DrumStudio::style_of (t))) {
		set_style (DrumStudio::style_of (t));
	}
	update_buttons ();
}

void
DrumStudioPanel::session_going_away ()
{
	_timer.disconnect ();
	_session_connections.drop_connections ();
	_performer.reset ();
	SessionHandlePtr::session_going_away ();
	update_buttons ();
}

std::shared_ptr<MidiTrack>
DrumStudioPanel::drum_track () const
{
	return _session ? DrumStudio::find_drum_track (*_session) : std::shared_ptr<MidiTrack> ();
}

void
DrumStudioPanel::set_style (std::string const& key)
{
	DrumStudio::Style const* st = DrumStudio::style (key);
	if (!st) {
		return;
	}
	_style_key = key;
	_style.set_text (st->name);
	update_buttons ();
}

void
DrumStudioPanel::set_kit (int i)
{
	_kit_index = (i >= 0 && i < (int)_kits.size ()) ? i : -1;
	_kit.set_text (_kit_index >= 0 ? _kits[_kit_index]->name : _("No instrument"));
}

void
DrumStudioPanel::set_quantize (DrumStudio::Quantize q)
{
	_q = q;
	_quantize.set_text (q == DrumStudio::QuantizeBeat ? _("Next beat") : q == DrumStudio::QuantizeBar ? _("Next bar") : _("Next 2 bars"));
	if (_performer) {
		_performer->set_quantize (q);
	}
}

void
DrumStudioPanel::match_meter ()
{
	/* grooves in 6/8 only line up with the bars in 6/8; change the time
	 * signature when the song has just one (undoable) */
	DrumStudio::Style const* st = DrumStudio::style (_style_key);
	TempoMap::SharedPtr      tmap (TempoMap::use ());
	if (!st || tmap->n_meters () != 1) {
		return;
	}
	Meter const& m (tmap->meter_at (timepos_t (Beats ())));
	if (m.divisions_per_bar () == st->meter_num && m.note_value () == st->meter_den) {
		return;
	}
	TempoMapChange tmc (PublicEditor::instance (), _("drum style time signature"));
	tmc.map ().set_meter (Meter (st->meter_num, st->meter_den), timepos_t (Beats ()));
	_status.set_text (string_compose (_("Time signature set to %1/%2 for %3 (suggested tempo %4 BPM)."), st->meter_num, st->meter_den, st->name, st->tempo));
}

void
DrumStudioPanel::create_or_load ()
{
	if (!_session || _style_key.empty ()) {
		return;
	}
	std::shared_ptr<MidiTrack> t = drum_track ();
	if (t) {
		if (DrumStudio::style_of (t) == _style_key) {
			return;
		}
		match_meter ();
		if (DrumStudio::load_style (t, _style_key)) {
			_status.set_text (_("Some grooves of this style could not be loaded."));
		}
	} else {
		match_meter ();
		std::shared_ptr<PluginInfo> kit = _kit_index >= 0 ? _kits[_kit_index] : std::shared_ptr<PluginInfo> ();
		t = DrumStudio::create_drum_track (*_session, _style_key, kit);
		if (!t) {
			ArdourMessageDialog msg (_("The drum track could not be created."));
			msg.run ();
			return;
		}
		if (_kit_index < 0) {
			_status.set_text (_("Drum track created without an instrument: add a General MIDI drum instrument to it."));
		}
	}
	update_buttons ();
}

void
DrumStudioPanel::cell_clicked (int part, int complexity)
{
	if (!_performer || !drum_track ()) {
		return;
	}
	_part       = part;
	_complexity = complexity;
	_performer->request (part, complexity);
	update_buttons ();
}

void
DrumStudioPanel::toggle_record ()
{
	if (_performer) {
		_performer->set_recording (!_performer->recording ());
	}
	update_buttons ();
}

void
DrumStudioPanel::stop ()
{
	if (!_session) {
		return;
	}
	if (_performer) {
		_performer->reset ();
	}
	_session->trigger_stop_all (false);
	update_buttons ();
}

void
DrumStudioPanel::flatten ()
{
	std::shared_ptr<MidiTrack> t = drum_track ();
	if (!_session || !t) {
		return;
	}

	/* the last part plays to the end of the session: make sure the session
	 * reaches at least one clip past the last cue marker */
	timepos_t last;
	bool      any = false;
	for (auto const& l : _session->locations ()->list ()) {
		if (l->is_cue_marker () && l->cue_id () >= 0 && l->cue_id () < DrumStudio::slot_count ()) {
			last = any ? std::max (last, l->start ()) : l->start ();
			any  = true;
		}
	}
	if (!any) {
		_status.set_text (_("Nothing to flatten: record a performance first."));
		return;
	}
	TempoMap::SharedPtr tmap (TempoMap::use ());
	timepos_t const     end (tmap->quarters_at (tmap->bbt_walk (tmap->bbt_at (last), BBT_Offset (2, 0, 0))));
	if (_session->current_end () < end) {
		_session->maybe_update_session_range (timepos_t (0), end);
	}

	if (_session->transport_rolling ()) {
		_session->request_stop ();
	}
	int const n = DrumStudio::flatten (*_session, t, timepos_t (0), timepos_t::max (BeatTime));
	if (n < 0) {
		_status.set_text (_("Flatten failed."));
	} else {
		_status.set_text (string_compose (_("Flattened to %1 MIDI regions. Undo brings the cue markers back into play."), n));
	}
	update_buttons ();
}

void
DrumStudioPanel::toggle_follow_cues ()
{
	std::shared_ptr<MidiTrack> t = drum_track ();
	if (!t || !t->triggerbox () || !t->triggerbox ()->trigger (0)) {
		return;
	}
	DrumStudio::follow_cues (t, t->triggerbox ()->trigger (0)->cue_isolated ());
	update_buttons ();
}

void
DrumStudioPanel::located ()
{
	/* a jump while rolling makes pending switches meaningless; the locate
	 * that starts the transport (e.g. for an audition) must not clear them */
	if (_performer && _session->transport_rolling ()) {
		_performer->reset ();
	}
	update_buttons ();
}

void
DrumStudioPanel::transport_changed ()
{
	if (_performer && !_session->transport_rolling ()) {
		_performer->reset ();
	}
	update_buttons ();
}

void
DrumStudioPanel::tick ()
{
	if (!_performer || !is_mapped ()) {
		return;
	}
	int const playing = _performer->playing_slot ();
	int const pending = _performer->pending_slot ();
	_performer->tick ();
	if (playing != _performer->playing_slot () || pending != _performer->pending_slot ()) {
		update_buttons ();
	}
}

void
DrumStudioPanel::update_buttons ()
{
	std::shared_ptr<MidiTrack> t = drum_track ();
	bool const has = (bool) t;

	if (has && DrumStudio::style_of (t) != _style_key) {
		_create.set_text (_("Load Style"));
		_create.set_sensitive (true);
	} else {
		_create.set_text (_("Create Drum Track"));
		_create.set_sensitive (_session && !has && !_style_key.empty ());
	}
	_kit.set_sensitive (!has);

	int const playing = _performer ? _performer->playing_slot () : -1;
	int const pending = _performer ? _performer->pending_slot () : -1;
	for (int s = 0; s < (int)_cells.size (); ++s) {
		_cells[s]->set_sensitive (has);
		_cells[s]->set_active_state (s == playing ? Gtkmm2ext::ExplicitActive : s == pending ? Gtkmm2ext::ImplicitActive : Gtkmm2ext::Off);
	}

	_keys.set_sensitive (has);
	_keys.set_active (_keys_on);
	_record.set_sensitive (has);
	_record.set_active (_performer && _performer->recording ());
	_stop.set_sensitive (has);
	_flatten.set_sensitive (has);
	bool const follows = has && t->triggerbox () && t->triggerbox ()->trigger (0) && !t->triggerbox ()->trigger (0)->cue_isolated ();
	_follow.set_sensitive (has);
	_follow.set_active (follows);

	if (!_session) {
		_status.set_text (_("Open a session to use Drum Studio."));
	} else if (!has) {
		_status.set_text (_("Pick a style and create the drum track."));
	} else if (_status.get_text ().empty () || _status.get_text () == _("Pick a style and create the drum track.") || _status.get_text () == _("Open a session to use Drum Studio.")) {
		_status.set_text (string_compose (_("Drum track: %1"), t->name ()));
	}
}

void
DrumStudioPanel::toggle_keys ()
{
	_keys_on = !_keys_on;
	update_buttons ();
}

bool
DrumStudioPanel::key_press (GdkEventKey* ev)
{
	return _instance && _instance->handle_key (ev);
}

bool
DrumStudioPanel::handle_key (GdkEventKey* ev)
{
	if (!_keys_on || !is_mapped () || !_performer || !drum_track ()) {
		return false;
	}
	if (ev->type != GDK_KEY_PRESS || (ev->state & Gtkmm2ext::Keyboard::RelevantModifierKeyMask) != 0) {
		return false;
	}
	guint const k = gdk_keyval_to_lower (ev->keyval);
	if (k >= GDK_1 && k <= GDK_4) {
		cell_clicked (_part, k - GDK_1 + 1);
		return true;
	}
	for (auto const& pk : part_keys) {
		if (k == pk.key) {
			int const p = DrumStudio::part_index (pk.part);
			if (p >= 0) {
				cell_clicked (p, _complexity);
				return true;
			}
		}
	}
	return false;
}

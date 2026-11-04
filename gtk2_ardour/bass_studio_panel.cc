/*
 * MusicDAWM Bass Studio panel (see bass_studio_panel.h).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <ytkmm/table.h>

#include "pbd/compose.h"

#include "ardour/auditioner.h"
#include "ardour/gm_sound.h"
#include "ardour/midi_track.h"
#include "ardour/region.h"
#include "ardour/session.h"

#include "widgets/tooltips.h"

#include "bass_studio_panel.h"
#include "gui_thread.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using namespace ArdourWidgets;
using namespace Gtk;
using namespace Gtk::Menu_Helpers;
using std::string;

BassStudioPanel::BassStudioPanel ()
	: VBox (false, 6)
	, _start_bar (1.0, 0)
	, _follow (_("Follow the drum parts"))
	, _listen (_("Listen"), ArdourButton::Element (ArdourButton::default_elements | ArdourButton::Indicator))
	, _make (_("Make Bass Line"))
	, _previewing (false)
{
	_chords.set_wrap_mode (WRAP_WORD);
	_chords.get_buffer ()->set_text ("C G Am F");
	_chords_scroll.add (_chords);
	_chords_scroll.set_policy (POLICY_NEVER, POLICY_AUTOMATIC);
	_chords_scroll.set_shadow_type (SHADOW_IN);
	_chords_scroll.set_size_request (200, 90);
	set_tooltip (_chords, _("One chord per bar: C G Am F. \"C,G\" is two chords in a bar, \"%\" repeats the bar before, \"N.C.\" is a bar without bass, "
	                        "\"C/E\" plays E in the bass, [Verse] is a comment. Bars lines | and new lines are ignored."));

	_start_bar.set_range (1, 999);
	_start_bar.set_increments (1, 4);
	_start_bar.set_value (1);
	set_tooltip (_start_bar, _("The bar where the first chord starts"));

	for (auto const& st : BassStudio::styles ()) {
		_style.add_menu_elem (MenuElem (st.name, sigc::bind (sigc::mem_fun (*this, &BassStudioPanel::set_style), st.key)));
	}
	for (int c = 1; c <= 4; ++c) {
		_complexity.add_menu_elem (MenuElem (string_compose ("%1", c), sigc::bind (sigc::mem_fun (*this, &BassStudioPanel::set_complexity), c)));
	}
	_feel.add_menu_elem (MenuElem (_("Normal"), sigc::bind (sigc::mem_fun (*this, &BassStudioPanel::set_feel), DrumStudio::FeelNormal)));
	_feel.add_menu_elem (MenuElem (_("Half-time"), sigc::bind (sigc::mem_fun (*this, &BassStudioPanel::set_feel), DrumStudio::FeelHalf)));
	_feel.add_menu_elem (MenuElem (_("Double-time"), sigc::bind (sigc::mem_fun (*this, &BassStudioPanel::set_feel), DrumStudio::FeelDouble)));
	for (size_t i = 0; i < GMSound::basses ().size (); ++i) {
		_sound.add_menu_elem (MenuElem (GMSound::basses ()[i].name, sigc::bind (sigc::mem_fun (*this, &BassStudioPanel::set_sound), (int)i)));
	}
	set_tooltip (_complexity, _("1 = simple .. 4 = busy. With \"Follow the drum parts\", each bar takes the complexity of the drum part played there."));
	set_tooltip (_feel, _("The bass against the song's tempo: normal, half-time (twice as slow) or double-time (twice as fast)"));
	set_tooltip (_follow, _("Each bar takes the complexity of the Drum Studio part played there (verse 1, chorus 4, ...)"));
	set_tooltip (_listen, _("Listen to the first 4 bars of the line, in a loop (the transport stays stopped)"));
	set_tooltip (_make, _("Make the bass line on the bass track (created if needed). A line made before is replaced; Edit > Undo brings it back."));

	_follow.set_active (true);
	_follow.signal_toggled ().connect (sigc::mem_fun (*this, &BassStudioPanel::options_changed));
	_start_bar.signal_value_changed ().connect (sigc::mem_fun (*this, &BassStudioPanel::options_changed));
	_listen.signal_clicked.connect ([this] () {
		if (_previewing) {
			stop_listening ();
		} else {
			listen ();
		}
	});
	_make.signal_clicked.connect (sigc::mem_fun (*this, &BassStudioPanel::make));

	Table* t = manage (new Table (6, 2));
	t->set_spacings (4);
	int row = 0;
	auto lbl = [] (char const* s) {
		Label* l = manage (new Label (s));
		l->set_alignment (0, 0.5);
		return l;
	};
	t->attach (*lbl (_("Start bar:")), 0, 1, row, row + 1, FILL, SHRINK);
	t->attach (_start_bar, 1, 2, row, row + 1, FILL | EXPAND, SHRINK);
	++row;
	t->attach (*lbl (_("Style:")), 0, 1, row, row + 1, FILL, SHRINK);
	t->attach (_style, 1, 2, row, row + 1, FILL | EXPAND, SHRINK);
	++row;
	t->attach (*lbl (_("Complexity:")), 0, 1, row, row + 1, FILL, SHRINK);
	t->attach (_complexity, 1, 2, row, row + 1, FILL | EXPAND, SHRINK);
	++row;
	t->attach (*lbl (_("Feel:")), 0, 1, row, row + 1, FILL, SHRINK);
	t->attach (_feel, 1, 2, row, row + 1, FILL | EXPAND, SHRINK);
	++row;
	t->attach (*lbl (_("Sound:")), 0, 1, row, row + 1, FILL, SHRINK);
	t->attach (_sound, 1, 2, row, row + 1, FILL | EXPAND, SHRINK);
	++row;
	t->attach (_follow, 0, 2, row, row + 1, FILL, SHRINK);

	HBox* buttons = manage (new HBox (true, 4));
	buttons->pack_start (_listen);
	buttons->pack_start (_make);

	Label* help = manage (new Label (_("Type the chords, one per bar, choose a style, then Listen and Make Bass Line. "
	                                   "The line is an ordinary MIDI region: edit it like any MIDI.")));
	help->set_line_wrap (true);
	help->set_alignment (0, 0.5);
	help->set_size_request (200, -1);
	_status.set_line_wrap (true);
	_status.set_alignment (0, 0.5);
	_status.set_size_request (200, -1);

	set_border_width (4);
	pack_start (*lbl (_("Chords:")), false, false);
	pack_start (_chords_scroll, false, false);
	pack_start (*t, false, false);
	pack_start (*buttons, false, false);
	pack_start (*help, false, false);
	pack_start (_status, false, false);
	show_all ();

	for (auto* w : std::vector<ArdourButton*> { &_style, &_complexity, &_feel, &_sound, &_listen, &_make }) {
		w->set_can_focus (false);
	}

	set_style (_opt.style);
	set_complexity (_opt.complexity);
	set_feel (_opt.feel);
	set_sound (1); /* Finger Bass */
	update_buttons ();
}

BassStudioPanel::~BassStudioPanel ()
{
}

void
BassStudioPanel::set_session (Session* s)
{
	SessionHandlePtr::set_session (s);
	_session_connections.drop_connections ();
	_previewing = false;
	if (_session) {
		_session->AuditionActive.connect (_session_connections, invalidator (*this), std::bind (&BassStudioPanel::audition_active, this, _1), gui_context ());
		string              saved_chart;
		int                 bar;
		BassStudio::Options o;
		BassStudio::saved (*_session, saved_chart, bar, o);
		if (!saved_chart.empty ()) {
			_chords.get_buffer ()->set_text (saved_chart);
			_start_bar.set_value (bar);
			_follow.set_active (o.follow_drums);
			set_style (o.style);
			set_complexity (o.complexity);
			set_feel (o.feel);
			for (size_t i = 0; i < GMSound::basses ().size (); ++i) {
				if (GMSound::basses ()[i].program == o.program && GMSound::basses ()[i].bank == o.bank) {
					set_sound (i);
				}
			}
		}
		/* the soundfont's place may have changed (the program folder moved) */
		if (std::shared_ptr<MidiTrack> t = BassStudio::find_bass_track (*_session)) {
			GMSound::load_soundfont (t);
		}
	}
	update_buttons ();
}

void
BassStudioPanel::session_going_away ()
{
	_session_connections.drop_connections ();
	_previewing = false;
	SessionHandlePtr::session_going_away ();
	update_buttons ();
}

string
BassStudioPanel::chart () const
{
	return _chords.get_buffer ()->get_text ();
}

void
BassStudioPanel::set_style (string const& key)
{
	BassStudio::Style const* st = BassStudio::style (key);
	if (!st) {
		return;
	}
	_opt.style = key;
	_style.set_text (st->name);
	options_changed ();
}

void
BassStudioPanel::set_complexity (int c)
{
	_opt.complexity = c;
	_complexity.set_text (string_compose ("%1", c));
	options_changed ();
}

void
BassStudioPanel::set_feel (DrumStudio::Feel f)
{
	_opt.feel = f;
	_feel.set_text (f == DrumStudio::FeelHalf ? _("Half-time") : f == DrumStudio::FeelDouble ? _("Double-time") : _("Normal"));
	options_changed ();
}

void
BassStudioPanel::set_sound (int i)
{
	if (i < 0 || i >= (int)GMSound::basses ().size ()) {
		return;
	}
	_opt.program = GMSound::basses ()[i].program;
	_opt.bank    = GMSound::basses ()[i].bank;
	_sound.set_text (GMSound::basses ()[i].name);
	options_changed ();
}

void
BassStudioPanel::options_changed ()
{
	_opt.follow_drums = _follow.get_active ();
	if (_previewing) {
		/* keep listening, with the new settings */
		listen ();
	}
}

void
BassStudioPanel::listen ()
{
	if (!_session || !_session->the_auditioner ()) {
		return;
	}
	if (!GMSound::available ()) {
		_status.set_text (_("The General MIDI soundfont or ACE Fluid Synth is missing."));
		return;
	}
	string                  err;
	std::shared_ptr<Region> r = BassStudio::preview_region (*_session, chart (), (int)_start_bar.get_value (), _opt, 4, err);
	if (!r) {
		_status.set_text (err);
		stop_listening ();
		return;
	}
	if (_session->transport_rolling ()) {
		_session->request_stop ();
	}
	_session->the_auditioner ()->synth_setup = [] (std::shared_ptr<Plugin> p) { GMSound::load_soundfont (p); };
	_session->the_auditioner ()->set_audition_synth_info (GMSound::synth ());
	_session->audition_region (r, true);
	_previewing = true;
	_status.set_text (_("Listening to the first 4 bars. Change the style, complexity, feel or sound while it plays; click Listen again to stop."));
	update_buttons ();
}

void
BassStudioPanel::stop_listening ()
{
	if (!_previewing) {
		return;
	}
	_previewing = false;
	if (_session) {
		_session->cancel_audition ();
	}
	update_buttons ();
}

void
BassStudioPanel::audition_active (bool yn)
{
	if (!yn && _previewing) {
		_previewing = false;
		update_buttons ();
	}
}

void
BassStudioPanel::make ()
{
	if (!_session) {
		return;
	}
	stop_listening ();
	string    err;
	int const n = BassStudio::make_bass_line (*_session, chart (), (int)_start_bar.get_value (), _opt, err);
	if (n < 0) {
		_status.set_text (err);
		return;
	}
	_status.set_text (string_compose (_("Bass line made: %1 notes on the bass track. Edit > Undo goes back."), n));
	update_buttons ();
}

void
BassStudioPanel::update_buttons ()
{
	bool const ok = _session && GMSound::available ();
	_listen.set_sensitive (ok);
	_make.set_sensitive (ok);
	_listen.set_active (_previewing);
	if (!_session) {
		_status.set_text (_("Open a session to use Bass Studio."));
	} else if (!GMSound::available ()) {
		_status.set_text (_("The General MIDI soundfont or ACE Fluid Synth is missing."));
	} else if (_status.get_text () == _("Open a session to use Bass Studio.")) {
		_status.set_text ("");
	}
}

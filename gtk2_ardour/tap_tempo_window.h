/*
 * MusicDAWM WP4: Tap Tempo Map window.
 *
 * While the song plays, the user taps T (or the Tap button) on every beat or
 * on every bar start. Each run of the transport is a take; a new take
 * replaces the earlier taps in the passage it covers. "Apply" turns the
 * taps into the session's tempo map (one undo step).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <vector>

#include <ytkmm/box.h>
#include <ytkmm/button.h>
#include <ytkmm/checkbutton.h>
#include <ytkmm/comboboxtext.h>
#include <ytkmm/label.h>
#include <ytkmm/radiobutton.h>
#include <ytkmm/spinbutton.h>

#include "pbd/signals.h"

#include "ardour/types.h"

#include "ardour_window.h"

class PublicEditor;

class TapTempoWindow : public ArdourWindow
{
public:
	TapTempoWindow (PublicEditor&);
	~TapTempoWindow ();

	/* called first by the global key handler: T taps when the key press
	 * is for this window (global bindings would see it first) */
	static bool key_press (GdkEventKey*);

	void set_session (ARDOUR::Session*);

private:
	static TapTempoWindow* _instance;
	PublicEditor&          _editor;

	Gtk::RadioButton   _on_beats;
	Gtk::RadioButton   _on_bars;
	Gtk::SpinButton    _divisions;
	Gtk::ComboBoxText  _note_value;
	Gtk::CheckButton   _smooth;
	Gtk::ComboBoxText  _snap_track;
	Gtk::Button        _tap;
	Gtk::Button        _clear;
	Gtk::Button        _apply;
	Gtk::Label         _status;

	std::vector<ARDOUR::samplepos_t> _taps; /* earlier takes, merged */
	std::vector<ARDOUR::samplepos_t> _take; /* the take being tapped */
	PBD::ScopedConnectionList        _session_connections;

	void tap ();
	void transport_state_changed ();
	void end_take ();
	void clear ();
	void apply ();
	void fill_tracks ();
	void update_status ();
	std::vector<ARDOUR::samplepos_t> all_taps () const;
};

/*
 * MusicDAWM Bass Studio panel (a page of the Editor sidebar): type the
 * chords, choose a style, complexity, feel and bass sound, listen, and make
 * the bass line (see ARDOUR::BassStudio).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <ytkmm/box.h>
#include <ytkmm/checkbutton.h>
#include <ytkmm/label.h>
#include <ytkmm/scrolledwindow.h>
#include <ytkmm/spinbutton.h>
#include <ytkmm/textview.h>

#include "pbd/signals.h"

#include "ardour/bass_studio.h"
#include "ardour/session_handle.h"

#include "widgets/ardour_button.h"
#include "widgets/ardour_dropdown.h"

class BassStudioPanel : public Gtk::VBox, public ARDOUR::SessionHandlePtr
{
public:
	BassStudioPanel ();
	~BassStudioPanel ();

	void set_session (ARDOUR::Session*);

private:
	void session_going_away ();

	Gtk::TextView                 _chords;
	Gtk::ScrolledWindow           _chords_scroll;
	Gtk::SpinButton               _start_bar;
	ArdourWidgets::ArdourDropdown _style;
	ArdourWidgets::ArdourDropdown _complexity;
	ArdourWidgets::ArdourDropdown _feel;
	ArdourWidgets::ArdourDropdown _sound;
	Gtk::CheckButton              _follow;
	ArdourWidgets::ArdourButton   _listen;
	ArdourWidgets::ArdourButton   _make;
	Gtk::Label                    _status;

	ARDOUR::BassStudio::Options _opt;
	bool                        _previewing;

	PBD::ScopedConnectionList _session_connections;

	void set_style (std::string const&);
	void set_complexity (int);
	void set_feel (ARDOUR::DrumStudio::Feel);
	void set_sound (int index);
	void listen ();
	void stop_listening ();
	void make ();
	void audition_active (bool);
	void options_changed ();
	std::string chart () const;
	void        update_buttons ();
};

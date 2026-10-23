/*
 * MusicDAWM Drum Studio window: pick a style, create the drum track, perform
 * song parts live (with keys), record the performance as cue markers and
 * flatten it to MIDI regions. The work is done by ARDOUR::DrumStudio.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <ytkmm/label.h>
#include <ytkmm/table.h>

#include "pbd/signals.h"

#include "ardour/drum_studio.h"

#include "widgets/ardour_button.h"
#include "widgets/ardour_dropdown.h"

#include "ardour_window.h"

namespace ARDOUR {
	class MidiTrack;
	class PluginInfo;
	class Session;
}

class DrumStudioWindow : public ArdourWindow
{
public:
	DrumStudioWindow ();
	~DrumStudioWindow ();

	void set_session (ARDOUR::Session*);

protected:
	void session_going_away ();
	bool on_key_press_event (GdkEventKey*);

private:
	void set_style (std::string const& key);
	void set_kit (int);
	void set_quantize (ARDOUR::DrumStudio::Quantize);
	void create_or_load ();
	void match_meter ();
	void cell_clicked (int part, int complexity);
	void toggle_record ();
	void flatten ();
	void toggle_follow_cues ();
	void stop ();
	void tick ();
	void update_buttons ();
	void transport_changed ();
	void located ();

	std::shared_ptr<ARDOUR::MidiTrack> drum_track () const;

	ArdourWidgets::ArdourDropdown _style;
	ArdourWidgets::ArdourDropdown _kit;
	ArdourWidgets::ArdourDropdown _quantize;
	ArdourWidgets::ArdourButton   _create;
	ArdourWidgets::ArdourButton   _record;
	ArdourWidgets::ArdourButton   _stop;
	ArdourWidgets::ArdourButton   _flatten;
	ArdourWidgets::ArdourButton   _follow;
	Gtk::Table                    _grid;
	Gtk::Label                    _status;

	std::vector<ArdourWidgets::ArdourButton*>       _cells; /* by slot */
	std::vector<std::shared_ptr<ARDOUR::PluginInfo>> _kits;

	std::string _style_key;
	int         _kit_index;   /* into _kits; -1: no instrument */
	int         _part;        /* last requested part, for the 1-4 keys */
	int         _complexity;  /* last requested complexity, for the part keys */

	ARDOUR::DrumStudio::Quantize          _q;
	std::unique_ptr<ARDOUR::DrumPerformer> _performer;

	sigc::connection          _timer;
	PBD::ScopedConnectionList _session_connections;
};

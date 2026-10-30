/*
 * MusicDAWM WP7: the Final Mix window.
 *
 * One column per track, grouped under role headers: name and reset, pan,
 * fader with level meter, the FX button (high-pass, brightness, punch,
 * space, role), mute/solo and a minus button that removes the track from
 * this mix ("+ Tracks" brings it back). A master column shows the
 * loudness (LUFS, true peak) against the chosen target and sets the
 * limiter's ceiling. The logic is in ARDOUR::FinalMix.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <map>
#include <memory>
#include <vector>

#include <ytkmm/box.h>
#include <ytkmm/comboboxtext.h>
#include <ytkmm/label.h>
#include <ytkmm/scrolledwindow.h>
#include <ytkmm/spinbutton.h>

#include "pbd/signals.h"

#include "ardour/final_mix.h"

#include "widgets/ardour_button.h"

#include "ardour_window.h"

class PublicEditor;
class FinalMixStrip;
class FinalMixFxWindow;
class AutomationController;

class FinalMixWindow : public ArdourWindow
{
public:
	FinalMixWindow (PublicEditor&);
	~FinalMixWindow ();

	void set_session (ARDOUR::Session*);

	ARDOUR::FinalMix* model () { return _model.get (); }
	void              rebuild ();
	void              queue_rebuild ();
	void              show_fx (std::shared_ptr<ARDOUR::Route>);
	void              reload_fx (std::shared_ptr<ARDOUR::Route>);

protected:
	void on_show ();
	void on_hide ();

private:
	PublicEditor&                     _editor;
	std::unique_ptr<ARDOUR::FinalMix> _model;

	/* top bar */
	ArdourWidgets::ArdourButton _fx_on;
	ArdourWidgets::ArdourButton _reset_all;
	ArdourWidgets::ArdourButton _classic;
	Gtk::ComboBoxText           _genre;
	ArdourWidgets::ArdourButton _starting_mix;
	ArdourWidgets::ArdourButton _randomise;
	ArdourWidgets::ArdourButton _before;
	ArdourWidgets::ArdourButton _after;
	ArdourWidgets::ArdourButton _ref;
	ArdourWidgets::ArdourButton _reference;
	ArdourWidgets::ArdourButton _match;
	ArdourWidgets::ArdourButton _clear_match;
	ArdourWidgets::ArdourButton _finalise;
	Gtk::Label                  _busy;
	/* transport */
	ArdourWidgets::ArdourButton _to_start;
	ArdourWidgets::ArdourButton _play;
	Gtk::Label                  _position;
	/* columns */
	Gtk::ScrolledWindow          _scroller;
	Gtk::HBox                    _columns;
	std::vector<FinalMixStrip*>  _strips;
	/* FX panels outlive the columns, which are rebuilt when roles change */
	std::map<PBD::ID, FinalMixFxWindow*> _fx_windows;
	ArdourWidgets::ArdourButton  _add_tracks;
	Gtk::Label                   _empty;
	/* master */
	Gtk::Label                            _lufs_i;
	Gtk::Label                            _lufs_m;
	Gtk::Label                            _true_peak;
	Gtk::Label                            _vs_target;
	Gtk::ComboBoxText                     _target;
	Gtk::SpinButton                       _custom;
	Gtk::SpinButton                       _ceiling;
	ArdourWidgets::ArdourButton           _reset_meter;
	std::shared_ptr<AutomationController> _master_fader;
	Gtk::VBox                             _master_fader_box;

	bool                      _ignore_changes = false;
	bool                      _rebuild_queued = false;
	sigc::connection          _fast_timer;
	sigc::connection          _slow_timer;
	PBD::ScopedConnectionList _session_connections;
	PBD::ScopedConnection     _exclusion_connection;
	PBD::ScopedConnection     _settings_connection;

	void fx_on_clicked ();
	void reset_all_clicked ();
	void starting_mix_clicked ();
	void randomise_clicked ();
	void settings_changed ();
	void listen_clicked (ARDOUR::FinalMix::Listen);
	void sync_listen ();
	void reference_clicked ();
	void match_clicked ();
	void clear_match_clicked ();
	void finalise_clicked ();
	void busy (std::string const&);
	void idle ();
	void add_tracks_clicked ();
	void target_changed ();
	void ceiling_changed ();
	void fast_update ();
	void slow_update ();
	void sync_master ();
	bool idle_rebuild ();
	void route_property_changed (PBD::PropertyChange const&);
	void drop_fx_windows ();
};

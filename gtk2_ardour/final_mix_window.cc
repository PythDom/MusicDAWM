/*
 * MusicDAWM WP7: the Final Mix window (see final_mix_window.h).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <cmath>

#include <ytkmm/adjustment.h>
#include <ytkmm/checkbutton.h>
#include <ytkmm/filechooserdialog.h>
#include <ytkmm/stock.h>
#include <ytkmm/frame.h>
#include <ytkmm/menu.h>
#include <ytkmm/scale.h>
#include <ytkmm/table.h>

#include "pbd/compose.h"
#include "pbd/openuri.h"

#include "ardour/amp.h"
#include "ardour/dB.h"
#include "ardour/mute_control.h"
#include "ardour/route.h"
#include "ardour/session.h"
#include "ardour/session_directory.h"
#include "ardour/solo_control.h"

#include "gtkmm2ext/utils.h"

#include "widgets/ardour_knob.h"
#include "widgets/tooltips.h"

#include "actions.h"
#include "ardour_message.h"
#include "automation_controller.h"
#include "final_mix_window.h"
#include "gui_thread.h"
#include "level_meter.h"
#include "mouse_cursors.h"
#include "public_editor.h"
#include "timers.h"
#include "ui_config.h"
#include "utils.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using namespace ArdourWidgets;
using namespace Gtk;
using std::shared_ptr;
using std::string;
using std::placeholders::_1;

#define PX_SCALE(px) std::max ((float)px, rintf ((float)px * UIConfiguration::instance ().get_ui_scale ()))

/* one decimal, as the meters show it */
static string
fmt1 (float v)
{
	char buf[32];
	snprintf (buf, sizeof (buf), "%.1f", v);
	return buf;
}

/* ---- the FX panel of one track ---- */

class FinalMixFxWindow : public ArdourWindow
{
public:
	FinalMixFxWindow (FinalMixWindow& w, shared_ptr<Route> r)
		: ArdourWindow (string_compose (_("%1: Final Mix FX"), r->name ()))
		, _window (w)
		, _route (r)
		, _high_pass (_("High-pass (removes rumble below):"))
		, _hp_adj (80, 20, 400, 5, 20)
		, _bright_adj (0, -6, 6, 0.5, 1)
		, _punch_adj (0, 0, 100, 5, 10)
		, _space_adj (0, 0, 100, 5, 10)
		, _hp_freq (_hp_adj)
		, _bright (_bright_adj)
		, _punch (_punch_adj)
		, _space (_space_adj)
	{
		for (int i = 0; i < FinalMix::NumRoles; ++i) {
			_role.append_text (FinalMix::role_name ((FinalMix::Role)i));
		}
		_hp_freq.set_digits (0);
		_bright.set_digits (1);
		_punch.set_digits (0);
		_space.set_digits (0);
		for (HScale* s : { &_hp_freq, &_bright, &_punch, &_space }) {
			s->set_size_request (220, -1);
			s->set_value_pos (POS_RIGHT);
		}
		_hp_freq.signal_format_value ().connect ([] (double v) { return string_compose (_("%1 Hz"), (int)v); });
		_bright.signal_format_value ().connect ([] (double v) { return string_compose (_("%1%2 dB"), v > 0 ? "+" : "", v); });
		_punch.signal_format_value ().connect ([] (double v) { return string_compose ("%1 %%", (int)v); });
		_space.signal_format_value ().connect ([] (double v) { return string_compose ("%1 %%", (int)v); });

		Table* t   = manage (new Table (6, 2));
		int    row = 0;
		t->set_spacings (6);
		t->set_border_width (10);
		auto label = [] (string const& s) { return manage (new Label (s, 1.0, 0.5)); };
		t->attach (*label (_("Role:")), 0, 1, row, row + 1, FILL, SHRINK);
		t->attach (_role, 1, 2, row, row + 1, FILL, SHRINK);
		++row;
		t->attach (_high_pass, 0, 1, row, row + 1, FILL, SHRINK);
		t->attach (_hp_freq, 1, 2, row, row + 1, FILL, SHRINK);
		++row;
		t->attach (*label (_("Brightness:")), 0, 1, row, row + 1, FILL, SHRINK);
		t->attach (_bright, 1, 2, row, row + 1, FILL, SHRINK);
		++row;
		t->attach (*label (_("Punch:")), 0, 1, row, row + 1, FILL, SHRINK);
		t->attach (_punch, 1, 2, row, row + 1, FILL, SHRINK);
		++row;
		t->attach (*label (_("Space (reverb):")), 0, 1, row, row + 1, FILL, SHRINK);
		t->attach (_space, 1, 2, row, row + 1, FILL, SHRINK);
		add (*t);

		set_tooltip (_high_pass, _("Cuts the low end this track does not need (rumble, handling noise, mud), so the bass and kick stay clear."));
		set_tooltip (_bright, _("A gentle lift (or cut) of the highs above about 6 kHz: more air and presence, or less harshness."));
		set_tooltip (_punch, _("Compression that evens out the level while letting the attacks through. 0 % is off."));
		set_tooltip (_space, _("How much of the track goes to the shared reverb. 0 % is dry."));

		load ();
		_role.signal_changed ().connect (sigc::mem_fun (*this, &FinalMixFxWindow::changed));
		_high_pass.signal_toggled ().connect (sigc::mem_fun (*this, &FinalMixFxWindow::changed));
		for (Adjustment* a : { &_hp_adj, &_bright_adj, &_punch_adj, &_space_adj }) {
			a->signal_value_changed ().connect (sigc::mem_fun (*this, &FinalMixFxWindow::changed));
		}
		show_all_children ();
	}

	void load ()
	{
		FinalMix* m = _window.model ();
		if (!m) {
			return;
		}
		FinalMix::Settings const s = m->settings (_route);
		_ignore = true;
		_role.set_active ((int)s.role);
		_high_pass.set_active (s.high_pass);
		_hp_adj.set_value (s.hp_freq);
		_bright_adj.set_value (s.brightness_db);
		_punch_adj.set_value (s.punch * 100);
		_space_adj.set_value (s.space * 100);
		_hp_freq.set_sensitive (s.high_pass);
		_ignore = false;
	}

private:
	FinalMixWindow&   _window;
	shared_ptr<Route> _route;
	ComboBoxText      _role;
	CheckButton       _high_pass;
	Adjustment        _hp_adj, _bright_adj, _punch_adj, _space_adj;
	HScale            _hp_freq, _bright, _punch, _space;
	bool              _ignore = false;

	void changed ()
	{
		FinalMix* m = _window.model ();
		if (_ignore || !m) {
			return;
		}
		FinalMix::Settings s = m->settings (_route);
		FinalMix::Role const role = (FinalMix::Role)std::max (0, _role.get_active_row_number ());
		if (role != s.role && !_high_pass.get_active ()) {
			/* a new role brings its own high-pass frequency */
			_ignore = true;
			_hp_adj.set_value (FinalMix::default_hp_freq (role));
			_ignore = false;
		}
		bool const regroup = role != s.role;
		s.role          = role;
		s.high_pass     = _high_pass.get_active ();
		s.hp_freq       = _hp_adj.get_value ();
		s.brightness_db = _bright_adj.get_value ();
		s.punch         = _punch_adj.get_value () / 100.0;
		s.space         = _space_adj.get_value () / 100.0;
		_hp_freq.set_sensitive (s.high_pass);
		m->set_settings (_route, s);
		if (regroup) {
			_window.queue_rebuild ();
		}
	}
};

/* ---- one column ---- */

class FinalMixStrip : public VBox
{
public:
	FinalMixStrip (FinalMixWindow& w, Session* s, shared_ptr<Route> r)
		: VBox (false, 4)
		, _window (w)
		, _route (r)
		, _name (r->name ())
		, _reset (X_("↺"))
		, _pan (ArdourKnob::default_elements, ArdourKnob::Detent)
		, _meter (s)
		, _fx (_("FX"))
		, _mute (_("M"))
		, _solo (_("S"))
		, _remove (X_("⊖"))
	{
		set_size_request (PX_SCALE (104), -1);

		_name.set_name ("route button");
		_name.set_fixed_colors (r->presentation_info ().color (), r->presentation_info ().color ());
		_name.set_layout_ellipsize_width (PX_SCALE (76) * PANGO_SCALE);
		_name.set_text_ellipsize (Pango::ELLIPSIZE_END);
		set_tooltip (_name, r->name ());
		set_tooltip (_reset, _("Reset: no effects, fader at 0 dB, pan centre"));
		HBox* top = manage (new HBox (false, 2));
		top->pack_start (_name, true, true);
		top->pack_start (_reset, false, false);
		pack_start (*top, false, false);

		_pan.set_size_request (PX_SCALE (32), PX_SCALE (32));
		_pan.set_tooltip_prefix (_("Pan: "));
		_pan.set_name ("trim knob");
		if (shared_ptr<AutomationControl> pan = r->pan_azimuth_control ()) {
			_pan.set_controllable (pan);
		} else {
			_pan.set_sensitive (false);
		}
		HBox* pan_box = manage (new HBox);
		pan_box->pack_start (*manage (new Label (_("L"))), true, true);
		pan_box->pack_start (_pan, false, false);
		pan_box->pack_start (*manage (new Label (_("R"))), true, true);
		pack_start (*pan_box, false, false);

		shared_ptr<AutomationControl> gain = r->gain_control ();
		_fader = AutomationController::create (gain->parameter (), gain->desc (), gain, false);
		_fader->set_name (X_("ProcessorControlSlider"));
		_fader->set_size_request (PX_SCALE (34), PX_SCALE (220));
		_meter.set_meter (r->shared_peak_meter ().get ());
		_meter.clear_meters ();
		_meter.setup_meters (PX_SCALE (220), PX_SCALE (6), 6);
		HBox* fader_box = manage (new HBox (false, 3));
		fader_box->pack_start (*manage (new Label ("")), true, true);
		fader_box->pack_start (*_fader, false, false);
		fader_box->pack_start (_meter, false, false);
		fader_box->pack_start (*manage (new Label ("")), true, true);
		pack_start (*fader_box, true, true);
		pack_start (_db, false, false);

		set_tooltip (_fx, _("High-pass, brightness, punch, space and the track's role"));
		pack_start (_fx, false, false);

		HBox* ms = manage (new HBox (true, 2));
		ms->pack_start (_mute, true, true);
		ms->pack_start (_solo, true, true);
		pack_start (*ms, false, false);
		_mute.set_name ("mute button");
		_solo.set_name ("solo button");

		set_tooltip (_remove, _("Remove this track from the final mix (it is muted). \"+ Tracks\" brings it back."));
		pack_start (_remove, false, false);

		_reset.signal_clicked.connect (sigc::mem_fun (*this, &FinalMixStrip::reset_clicked));
		_fx.signal_clicked.connect (sigc::mem_fun (*this, &FinalMixStrip::fx_clicked));
		_mute.signal_clicked.connect ([this] () { _route->mute_control ()->set_value (_route->mute_control ()->muted_by_self () ? 0 : 1, PBD::Controllable::NoGroup); });
		_solo.signal_clicked.connect ([this] () { _route->solo_control ()->set_value (_route->solo_control ()->self_soloed () ? 0 : 1, PBD::Controllable::NoGroup); });
		_remove.signal_clicked.connect ([this] () {
			if (FinalMix* m = _window.model ()) {
				m->set_excluded (_route, true);
			}
		});

		gain->Changed.connect (_connections, invalidator (*this), std::bind (&FinalMixStrip::update_db, this), gui_context ());
		r->mute_control ()->Changed.connect (_connections, invalidator (*this), std::bind (&FinalMixStrip::update_ms, this), gui_context ());
		r->solo_control ()->Changed.connect (_connections, invalidator (*this), std::bind (&FinalMixStrip::update_ms, this), gui_context ());
		update_db ();
		update_ms ();
		show_all ();
	}

	shared_ptr<Route> route () const { return _route; }

	void fast_update ()
	{
		_meter.update_meters ();
	}

private:
	FinalMixWindow&                       _window;
	shared_ptr<Route>                     _route;
	ArdourButton                          _name;
	ArdourButton                          _reset;
	ArdourKnob                            _pan;
	std::shared_ptr<AutomationController> _fader;
	LevelMeterHBox                        _meter;
	Label                                 _db;
	ArdourButton                          _fx;
	ArdourButton                          _mute;
	ArdourButton                          _solo;
	ArdourButton                          _remove;
	PBD::ScopedConnectionList             _connections;

	void update_db ()
	{
		float const db = accurate_coefficient_to_dB (_route->gain_control ()->get_value ());
		_db.set_text (db < -199 ? string ("-inf") : string_compose ("%1 dB", fmt1 (db)));
	}

	void update_ms ()
	{
		_mute.set_active (_route->mute_control ()->muted_by_self ());
		_solo.set_active (_route->solo_control ()->self_soloed ());
	}

	void reset_clicked ()
	{
		if (FinalMix* m = _window.model ()) {
			m->reset (_route);
			_window.reload_fx (_route);
		}
	}

	void fx_clicked ()
	{
		_window.show_fx (_route);
	}
};

/* ---- the window ---- */

FinalMixWindow::FinalMixWindow (PublicEditor& e)
	: ArdourWindow (_("Final Mix"))
	, _editor (e)
	, _fx_on (_("FX ON"), ArdourButton::default_elements, true)
	, _reset_all (_("Reset settings"))
	, _classic (_("Open classic mixer"))
	, _starting_mix (_("Starting mix"))
	, _randomise (_("Randomise FX"))
	, _before (_("Before"))
	, _after (_("After"))
	, _ref (_("Ref"))
	, _reference (_("Reference..."))
	, _match (_("Match to reference"))
	, _clear_match (_("Clear match"))
	, _finalise (_("Finalise Mix..."))
	, _to_start (X_("⏮"))
	, _play (X_("▶"))
	, _add_tracks (_("+ Tracks"))
	, _empty (_("No tracks yet. Import or record some, then come back."))
	, _custom (1, 1)
	, _ceiling (0.1, 1)
	, _reset_meter (_("Restart"))
{
	/* top bar */
	HBox* top = manage (new HBox (false, 6));
	top->pack_start (_fx_on, false, false);
	top->pack_start (_reset_all, false, false);
	top->pack_start (*manage (new Label (_("Genre:"))), false, false, 6);
	for (int g = 0; g < FinalMix::NumGenres; ++g) {
		_genre.append_text (FinalMix::genre_name ((FinalMix::Genre)g));
	}
	_genre.set_active (0);
	top->pack_start (_genre, false, false);
	top->pack_start (_starting_mix, false, false);
	set_tooltip (_starting_mix, _("Set a first balance, pan and effects for the chosen genre, from each track's role and loudness. Edit > Undo goes back."));
	set_tooltip (_randomise, _("Try other effect amounts around the genre's (the faders and pans stay). Edit > Undo goes back."));
	top->pack_end (_classic, false, false);
	HBox* listen = manage (new HBox (true, 0));
	listen->pack_start (_before, true, true);
	listen->pack_start (_after, true, true);
	listen->pack_start (_ref, true, true);
	top->pack_end (*listen, false, false, 12);
	set_tooltip (_before, _("Hear the tracks as recorded (faders 0 dB, pan centre, no Final Mix effects) at the loudness of the mix"));
	set_tooltip (_after, _("Hear the mix"));
	set_tooltip (_ref, _("Hear the reference song alone, at the loudness of the mix (Reference... chooses it)"));
	set_tooltip (_fx_on, _("Switch all Final Mix effects off to hear the tracks without them (the settings are kept)."));
	set_tooltip (_reset_all, _("Reset every track: no effects, faders at 0 dB, pan centre. Edit > Undo goes back."));

	/* transport */
	HBox* transport = manage (new HBox (false, 6));
	_to_start.set_related_action (ActionManager::get_action (X_("Transport"), X_("GotoStart")));
	_play.set_related_action (ActionManager::get_action (X_("Transport"), X_("ToggleRoll")));
	_to_start.set_text (X_("⏮"));
	_play.set_text (X_("▶ / ■"));
	transport->pack_start (_to_start, false, false);
	transport->pack_start (_play, false, false);
	transport->pack_start (_position, false, false);

	/* columns */
	_scroller.set_policy (POLICY_AUTOMATIC, POLICY_NEVER);
	_scroller.add (_columns);
	_columns.set_spacing (8);
	_columns.set_border_width (4);

	/* master */
	for (char const* t : { _("Streaming (-14 LUFS)"), _("Loud (-9 LUFS)"), _("Custom") }) {
		_target.append_text (t);
	}
	_custom.set_range (-30, -5);
	_custom.set_increments (0.5, 1);
	_ceiling.set_range (-3, 0);
	_ceiling.set_increments (0.1, 0.5);
	_ceiling.set_digits (1);
	set_tooltip (_ceiling, _("The limiter keeps the true peaks of the mix under this level (dBTP). -1 is safe for streaming and MP3."));
	set_tooltip (_reset_meter, _("Start a new loudness measurement (playback does it too)"));

	VBox* master = manage (new VBox (false, 4));
	master->set_border_width (6);
	Label* mt = manage (new Label);
	mt->set_markup (string_compose ("<b>%1</b>", _("Master")));
	master->pack_start (*mt, false, false);
	_lufs_i.set_name (X_("FinalMixLUFS"));
	master->pack_start (_lufs_i, false, false);
	master->pack_start (_lufs_m, false, false);
	master->pack_start (_true_peak, false, false);
	master->pack_start (_reset_meter, false, false);
	master->pack_start (*manage (new Label (_("Target:"), 0, 0.5)), false, false);
	master->pack_start (_target, false, false);
	master->pack_start (_custom, false, false);
	master->pack_start (_vs_target, false, false);
	HBox* ceil = manage (new HBox (false, 4));
	ceil->pack_start (*manage (new Label (_("Limiter ceiling:"))), false, false);
	ceil->pack_start (_ceiling, false, false);
	ceil->pack_start (*manage (new Label (_("dBTP"))), false, false);
	master->pack_start (*ceil, false, false);
	master->pack_start (_master_fader_box, true, true);
	_lufs_m.set_alignment (0, 0.5);
	_true_peak.set_alignment (0, 0.5);
	_vs_target.set_alignment (0, 0.5);
	_vs_target.set_line_wrap (true);
	Frame* master_frame = manage (new Frame);
	master_frame->add (*master);
	master_frame->set_size_request (PX_SCALE (190), -1);

	HBox* middle = manage (new HBox (false, 6));
	VBox* cols   = manage (new VBox (false, 4));
	cols->pack_start (_scroller, true, true);
	cols->pack_start (_empty, true, true);
	HBox* bottom = manage (new HBox (false, 6));
	bottom->pack_start (_add_tracks, false, false);
	bottom->pack_start (_randomise, false, false);
	bottom->pack_start (_reference, false, false, 12);
	bottom->pack_start (_match, false, false);
	bottom->pack_start (_clear_match, false, false);
	bottom->pack_start (_busy, false, false, 6);
	bottom->pack_end (_finalise, false, false);
	_finalise.set_name (X_("record enable button"));
	set_tooltip (_reference, _("Choose a finished song you like (WAV, FLAC, MP3...) to compare with and to match"));
	set_tooltip (_match, _("Analyse the mix and the reference, then set the loudness target and a gentle master EQ towards the reference, with advice"));
	set_tooltip (_clear_match, _("Switch the reference's master EQ off and go back to the earlier loudness target"));
	set_tooltip (_finalise, _("Export the song as WAV (and MP3 when FFmpeg is installed) at the loudness target, with the true peaks under the ceiling"));
	cols->pack_start (*bottom, false, false);
	middle->pack_start (*cols, true, true);
	middle->pack_start (*master_frame, false, false);
	set_tooltip (_add_tracks, _("Bring back a track removed from this mix"));

	VBox* vb = manage (new VBox (false, 6));
	vb->set_border_width (8);
	vb->pack_start (*top, false, false);
	vb->pack_start (*transport, false, false);
	vb->pack_start (*middle, true, true);
	add (*vb);
	set_default_size (PX_SCALE (900), PX_SCALE (560));

	_fx_on.signal_clicked.connect (sigc::mem_fun (*this, &FinalMixWindow::fx_on_clicked));
	_reset_all.signal_clicked.connect (sigc::mem_fun (*this, &FinalMixWindow::reset_all_clicked));
	_starting_mix.signal_clicked.connect (sigc::mem_fun (*this, &FinalMixWindow::starting_mix_clicked));
	_randomise.signal_clicked.connect (sigc::mem_fun (*this, &FinalMixWindow::randomise_clicked));
	_before.signal_clicked.connect (sigc::bind (sigc::mem_fun (*this, &FinalMixWindow::listen_clicked), FinalMix::Before));
	_after.signal_clicked.connect (sigc::bind (sigc::mem_fun (*this, &FinalMixWindow::listen_clicked), FinalMix::After));
	_ref.signal_clicked.connect (sigc::bind (sigc::mem_fun (*this, &FinalMixWindow::listen_clicked), FinalMix::Ref));
	_reference.signal_clicked.connect (sigc::mem_fun (*this, &FinalMixWindow::reference_clicked));
	_match.signal_clicked.connect (sigc::mem_fun (*this, &FinalMixWindow::match_clicked));
	_clear_match.signal_clicked.connect (sigc::mem_fun (*this, &FinalMixWindow::clear_match_clicked));
	_finalise.signal_clicked.connect (sigc::mem_fun (*this, &FinalMixWindow::finalise_clicked));
	_classic.signal_clicked.connect ([] () {
		if (Glib::RefPtr<Gtk::Action> a = ActionManager::get_action (X_("Common"), X_("show-mixer"))) {
			a->activate ();
		}
	});
	_add_tracks.signal_clicked.connect (sigc::mem_fun (*this, &FinalMixWindow::add_tracks_clicked));
	_target.signal_changed ().connect (sigc::mem_fun (*this, &FinalMixWindow::target_changed));
	_custom.signal_value_changed ().connect (sigc::mem_fun (*this, &FinalMixWindow::target_changed));
	_ceiling.signal_value_changed ().connect (sigc::mem_fun (*this, &FinalMixWindow::ceiling_changed));
	_reset_meter.signal_clicked.connect ([this] () {
		if (_session) {
			_session->reset_master_loudness ();
		}
	});

	FinalMix::ExclusionChanged.connect (_exclusion_connection, invalidator (*this), std::bind (&FinalMixWindow::queue_rebuild, this), gui_context ());
	FinalMix::SettingsChanged.connect (_settings_connection, invalidator (*this), std::bind (&FinalMixWindow::settings_changed, this), gui_context ());

	show_all_children ();
}

FinalMixWindow::~FinalMixWindow ()
{
	_fast_timer.disconnect ();
	_slow_timer.disconnect ();
	for (auto s : _strips) {
		delete s;
	}
	drop_fx_windows ();
}

void
FinalMixWindow::drop_fx_windows ()
{
	for (auto& i : _fx_windows) {
		delete i.second;
	}
	_fx_windows.clear ();
}

void
FinalMixWindow::show_fx (shared_ptr<Route> r)
{
	FinalMixFxWindow*& w = _fx_windows[r->id ()];
	if (!w) {
		w = new FinalMixFxWindow (*this, r);
		w->set_transient_for (*this);
	}
	w->load ();
	w->present ();
}

void
FinalMixWindow::reload_fx (shared_ptr<Route> r)
{
	auto i = _fx_windows.find (r->id ());
	if (i != _fx_windows.end ()) {
		i->second->load ();
	}
}

void
FinalMixWindow::route_property_changed (PBD::PropertyChange const& what)
{
	if (what.contains (Properties::name)) {
		queue_rebuild ();
	}
}

void
FinalMixWindow::set_session (Session* s)
{
	ArdourWindow::set_session (s);
	_session_connections.drop_connections ();
	drop_fx_windows ();
	_model.reset (s ? new FinalMix (*s) : nullptr);
	if (_model && _model->listen () != FinalMix::After) {
		/* a session saved while listening to Before or Ref */
		_model->set_listen (FinalMix::After);
	}
	if (s) {
		s->RouteAdded.connect (_session_connections, invalidator (*this), std::bind (&FinalMixWindow::queue_rebuild, this), gui_context ());
		if (is_visible ()) {
			s->set_master_loudness_metering (true);
		}
	}
	rebuild ();
}

void
FinalMixWindow::on_show ()
{
	ArdourWindow::on_show ();
	if (_session) {
		_session->set_master_loudness_metering (true);
	}
	rebuild ();
	_fast_timer = Timers::super_rapid_connect (sigc::mem_fun (*this, &FinalMixWindow::fast_update));
	_slow_timer = Timers::rapid_connect (sigc::mem_fun (*this, &FinalMixWindow::slow_update));
}

void
FinalMixWindow::on_hide ()
{
	_fast_timer.disconnect ();
	_slow_timer.disconnect ();
	if (_session) {
		_session->set_master_loudness_metering (false);
	}
	/* the window gone, the mix plays */
	if (_model && _model->listen () != FinalMix::After) {
		_model->set_listen (FinalMix::After);
	}
	ArdourWindow::on_hide ();
}

void
FinalMixWindow::queue_rebuild ()
{
	if (!_rebuild_queued) {
		_rebuild_queued = true;
		Glib::signal_idle ().connect (sigc::mem_fun (*this, &FinalMixWindow::idle_rebuild));
	}
}

bool
FinalMixWindow::idle_rebuild ()
{
	_rebuild_queued = false;
	rebuild ();
	return false;
}

void
FinalMixWindow::rebuild ()
{
	/* the strips first (they leave their boxes), then the role groups */
	for (auto s : _strips) {
		delete s;
	}
	_strips.clear ();
	for (Widget* w : _columns.get_children ()) {
		_columns.remove (*w);
		delete w;
	}
	_session_connections.drop_connections ();
	_master_fader.reset ();
	for (Widget* w : _master_fader_box.get_children ()) {
		_master_fader_box.remove (*w);
	}

	if (!_session || !_model) {
		_empty.show ();
		_scroller.hide ();
		return;
	}
	_session->RouteAdded.connect (_session_connections, invalidator (*this), std::bind (&FinalMixWindow::queue_rebuild, this), gui_context ());

	/* FX panels of deleted tracks go */
	std::vector<shared_ptr<Route>> const tracks = _model->tracks ();
	for (auto i = _fx_windows.begin (); i != _fx_windows.end ();) {
		if (std::none_of (tracks.begin (), tracks.end (), [&i] (shared_ptr<Route> const& r) { return r->id () == i->first; })) {
			delete i->second;
			i = _fx_windows.erase (i);
		} else {
			++i;
		}
	}

	/* group the tracks by role, in role order */
	std::vector<shared_ptr<Route>> by_role[FinalMix::NumRoles];
	int                            n_excluded = 0;
	for (auto const& r : tracks) {
		r->DropReferences.connect (_session_connections, invalidator (*this), std::bind (&FinalMixWindow::queue_rebuild, this), gui_context ());
		r->PropertyChanged.connect (_session_connections, invalidator (*this), std::bind (&FinalMixWindow::route_property_changed, this, _1), gui_context ());
		if (_model->excluded (r)) {
			++n_excluded;
			continue;
		}
		by_role[_model->settings (r).role].push_back (r);
	}
	for (int role = 0; role < FinalMix::NumRoles; ++role) {
		if (by_role[role].empty ()) {
			continue;
		}
		VBox*  group  = new VBox (false, 4);
		Label* header = manage (new Label);
		header->set_markup (string_compose ("<b>%1</b>", Gtkmm2ext::markup_escape_text (FinalMix::role_name ((FinalMix::Role)role))));
		group->pack_start (*header, false, false);
		HBox* strips = manage (new HBox (false, 4));
		for (auto const& r : by_role[role]) {
			FinalMixStrip* s = new FinalMixStrip (*this, _session, r);
			_strips.push_back (s);
			strips->pack_start (*s, false, false);
		}
		group->pack_start (*strips, true, true);
		_columns.pack_start (*group, false, false);
	}
	_columns.show_all ();

	bool const any = !_strips.empty ();
	_scroller.set_visible (any);
	_empty.set_visible (!any);
	_empty.set_text (n_excluded ? _("All tracks are removed from this mix. \"+ Tracks\" brings them back.")
	                            : _("No tracks yet. Import or record some, then come back."));
	_add_tracks.set_sensitive (n_excluded > 0);
	_add_tracks.set_text (n_excluded ? string_compose (_("+ Tracks (%1)"), n_excluded) : _("+ Tracks"));

	if (shared_ptr<Route> m = _session->master_out ()) {
		shared_ptr<AutomationControl> g = m->gain_control ();
		_master_fader                   = AutomationController::create (g->parameter (), g->desc (), g, false);
		_master_fader->set_name (X_("ProcessorControlSlider"));
		_master_fader->set_size_request (PX_SCALE (34), PX_SCALE (120));
		HBox* c = manage (new HBox);
		c->pack_start (*_master_fader, true, false);
		_master_fader_box.pack_start (*c, true, true);
		_master_fader_box.show_all ();
	}
	sync_master ();
}

void
FinalMixWindow::sync_master ()
{
	if (!_model) {
		return;
	}
	_ignore_changes = true;
	_fx_on.set_active (_model->fx_enabled ());
	_genre.set_active ((int)_model->genre ());
	sync_listen ();
	_target.set_active ((int)_model->target ());
	_custom.set_value (_model->target () == FinalMix::Custom ? _model->target_lufs () : -14);
	_custom.set_visible (_model->target () == FinalMix::Custom);
	_ceiling.set_value (_model->limiter_ceiling ());
	_ignore_changes = false;
	slow_update ();
}

void
FinalMixWindow::fx_on_clicked ()
{
	if (_model) {
		_model->set_fx_enabled (!_model->fx_enabled ());
		_fx_on.set_active (_model->fx_enabled ());
	}
}

void
FinalMixWindow::reset_all_clicked ()
{
	if (!_model) {
		return;
	}
	_model->reset_all ();
}

void
FinalMixWindow::starting_mix_clicked ()
{
	if (!_model) {
		return;
	}
	PublicEditor::CursorRAII cr (_editor, _editor.cursors ()->wait);
	gdk_flush ();
	_model->starting_mix ((FinalMix::Genre)std::max (0, _genre.get_active_row_number ()));
}

void
FinalMixWindow::randomise_clicked ()
{
	if (_model) {
		_model->randomise (g_random_int ());
	}
}

void
FinalMixWindow::settings_changed ()
{
	for (auto& i : _fx_windows) {
		i.second->load ();
	}
	/* roles may have changed (undo) */
	queue_rebuild ();
	sync_listen ();
}

void
FinalMixWindow::sync_listen ()
{
	if (!_model) {
		return;
	}
	FinalMix::Listen const l = _model->listen ();
	_before.set_active (l == FinalMix::Before);
	_after.set_active (l == FinalMix::After);
	_ref.set_active (l == FinalMix::Ref);
	bool const has_ref = (bool)_model->reference_track ();
	_ref.set_sensitive (has_ref);
	_match.set_sensitive (has_ref);
	_clear_match.set_sensitive (_model->matched ());
	_reference.set_text (has_ref ? _("Reference: change...") : _("Reference..."));
	/* while listening to Before, the mix's controls would change the wrong thing */
	_starting_mix.set_sensitive (l == FinalMix::After);
	_randomise.set_sensitive (l == FinalMix::After);
	_reset_all.set_sensitive (l == FinalMix::After);
	_fx_on.set_sensitive (l == FinalMix::After);
	_columns.set_sensitive (l == FinalMix::After);
}

void
FinalMixWindow::listen_clicked (FinalMix::Listen l)
{
	if (_model) {
		PublicEditor::CursorRAII cr (_editor, _editor.cursors ()->wait);
		gdk_flush ();
		_model->set_listen (l);
		sync_listen ();
	}
}

void
FinalMixWindow::busy (string const& what)
{
	_busy.set_text (what);
	get_child ()->set_sensitive (what.empty ());
	idle ();
}

void
FinalMixWindow::idle ()
{
	while (gtk_events_pending ()) {
		gtk_main_iteration ();
	}
	Glib::usleep (20000);
}

void
FinalMixWindow::reference_clicked ()
{
	if (!_model) {
		return;
	}
	FileChooserDialog d (*this, _("Choose a reference song"), FILE_CHOOSER_ACTION_OPEN);
	d.add_button (Stock::CANCEL, RESPONSE_CANCEL);
	d.add_button (Stock::OPEN, RESPONSE_OK);
	FileFilter audio;
	audio.set_name (_("Audio files"));
	for (char const* e : { "*.wav", "*.WAV", "*.flac", "*.FLAC", "*.mp3", "*.MP3", "*.aif", "*.aiff", "*.ogg", "*.m4a" }) {
		audio.add_pattern (e);
	}
	d.add_filter (audio);
	if (d.run () != RESPONSE_OK) {
		return;
	}
	string const path = d.get_filename ();
	d.hide ();
	busy (_("Importing and analysing the reference..."));
	int const r = _model->set_reference (path);
	busy ("");
	if (r) {
		ArdourMessageDialog msg (*this, _("This file could not be used as a reference."), false, MESSAGE_ERROR);
		msg.set_secondary_text (_("MusicDAWM reads WAV, FLAC, AIFF and Ogg files directly, and MP3 or M4A when FFmpeg is installed."));
		msg.run ();
	}
	sync_listen ();
}

void
FinalMixWindow::match_clicked ()
{
	if (!_model || !_model->reference_track ()) {
		return;
	}
	busy (_("Analysing the mix..."));
	FinalMix::Analysis const mix = _model->analyse_mix (sigc::mem_fun (*this, &FinalMixWindow::idle));
	busy ("");
	FinalMix::Match const m = _model->compute_match (mix, _model->reference_analysis ());
	if (!m.valid) {
		ArdourMessageDialog msg (*this, _("The mix or the reference could not be analysed (is the song silent?)."), false, MESSAGE_ERROR);
		msg.run ();
		return;
	}
	_model->apply_match (m);
	sync_master ();
	ArdourMessageDialog msg (*this, _("Matched to the reference"), false, MESSAGE_INFO);
	msg.set_secondary_text (m.report + _("\nListen with After and Ref. Clear match goes back."));
	msg.run ();
}

void
FinalMixWindow::clear_match_clicked ()
{
	if (_model) {
		_model->clear_match ();
		sync_master ();
	}
}

void
FinalMixWindow::finalise_clicked ()
{
	if (!_model || !_session) {
		return;
	}
	FileChooserDialog d (*this, _("Finalise Mix: where to save the song"), FILE_CHOOSER_ACTION_SAVE);
	d.add_button (Stock::CANCEL, RESPONSE_CANCEL);
	d.add_button (_("Finalise"), RESPONSE_OK);
	d.set_current_folder (_session->session_directory ().export_path ());
	d.set_current_name (_session->name () + X_(".wav"));
	CheckButton mp3 (_("Also make an MP3"));
	bool const  have_mp3 = FinalMix::mp3_available ();
	mp3.set_active (have_mp3);
	mp3.set_sensitive (have_mp3);
	if (!have_mp3) {
		set_tooltip (mp3, _("MP3 needs FFmpeg: install it in C:\\Program Files\\ffmpeg (see the user guide)."));
	}
	Label info (string_compose (_("The song is made %1 LUFS loud, with the true peaks under %2 dBTP."), _model->target_lufs (), _model->limiter_ceiling ()));
	VBox  extra (false, 4);
	extra.pack_start (info, false, false);
	extra.pack_start (mp3, false, false);
	extra.show_all ();
	d.set_extra_widget (extra);
	if (d.run () != RESPONSE_OK) {
		return;
	}
	string folder = Glib::path_get_dirname (d.get_filename ());
	string name   = Glib::path_get_basename (d.get_filename ());
	if (name.size () > 4 && (name.compare (name.size () - 4, 4, ".wav") == 0 || name.compare (name.size () - 4, 4, ".WAV") == 0)) {
		name = name.substr (0, name.size () - 4);
	}
	bool const want_mp3 = mp3.get_active ();
	d.hide ();

	busy (_("Finalising the mix..."));
	FinalMix::Finalised const f = _model->finalise (folder, name, want_mp3, sigc::mem_fun (*this, &FinalMixWindow::idle));
	busy ("");
	if (!f.ok) {
		ArdourMessageDialog msg (*this, _("Finalise Mix failed"), false, MESSAGE_ERROR);
		msg.set_secondary_text (f.error);
		msg.run ();
		return;
	}
	string text = string_compose (_("%1\nLoudness %2 LUFS, true peak %3 dBTP."), f.wav, fmt1 (f.lufs), fmt1 (f.true_peak));
	if (!f.mp3.empty ()) {
		text += "\n" + f.mp3;
	} else if (want_mp3) {
		text += "\n" + string (_("No MP3: FFmpeg was not found."));
	}
	ArdourMessageDialog msg (*this, _("The song is finalised"), false, MESSAGE_INFO, BUTTONS_NONE);
	msg.set_secondary_text (text);
	msg.add_button (_("Open the folder"), RESPONSE_ACCEPT);
	msg.add_button (Stock::OK, RESPONSE_OK);
	if (msg.run () == RESPONSE_ACCEPT) {
		PBD::open_folder (folder);
	}
}

void
FinalMixWindow::add_tracks_clicked ()
{
	if (!_model) {
		return;
	}
	Menu* m = manage (new Menu);
	for (auto const& r : _model->tracks ()) {
		if (_model->excluded (r)) {
			MenuItem* i = manage (new MenuItem (r->name ()));
			i->signal_activate ().connect ([this, r] () {
				if (_model) {
					_model->set_excluded (r, false);
				}
			});
			m->append (*i);
		}
	}
	m->show_all ();
	m->popup (1, gtk_get_current_event_time ());
}

void
FinalMixWindow::target_changed ()
{
	if (_ignore_changes || !_model) {
		return;
	}
	FinalMix::Target const t = (FinalMix::Target)std::max (0, _target.get_active_row_number ());
	_model->set_target (t, _custom.get_value ());
	_custom.set_visible (t == FinalMix::Custom);
	slow_update ();
}

void
FinalMixWindow::ceiling_changed ()
{
	if (_ignore_changes || !_model) {
		return;
	}
	_model->set_limiter_ceiling (_ceiling.get_value ());
}

void
FinalMixWindow::fast_update ()
{
	for (auto s : _strips) {
		s->fast_update ();
	}
}

static string
lufs_text (float v)
{
	return v <= -70 ? string ("--") : fmt1 (v);
}

void
FinalMixWindow::slow_update ()
{
	if (!_session || !_model) {
		return;
	}
	samplepos_t const pos = _session->audible_sample ();
	double const      sec = pos / (double)_session->sample_rate ();
	char              buf[32];
	snprintf (buf, sizeof (buf), "%d:%04.1f", (int)(sec / 60), std::fmod (sec, 60.));
	_position.set_text (buf);

	Session::MasterLoudness const l = _session->master_loudness ();
	_lufs_i.set_markup (string_compose ("<span size=\"x-large\"><b>%1</b></span> LUFS", lufs_text (l.integrated)));
	_lufs_m.set_text (string_compose (_("Now: %1 LUFS"), lufs_text (l.momentary)));
	_true_peak.set_text (string_compose (_("Peak: %1 dBTP"), l.true_peak <= -70 ? string ("--") : fmt1 (l.true_peak)));

	float const target = _model->target_lufs ();
	if (l.integrated <= -70) {
		_vs_target.set_text (_("Play the song to measure it."));
	} else {
		float const d = l.integrated - target;
		if (std::fabs (d) < 0.5) {
			_vs_target.set_text (_("On target."));
		} else if (d < 0) {
			_vs_target.set_text (string_compose (_("%1 LU below the target"), fmt1 (-d)));
		} else {
			_vs_target.set_text (string_compose (_("%1 LU above the target"), fmt1 (d)));
		}
	}
}

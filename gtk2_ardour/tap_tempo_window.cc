/*
 * MusicDAWM WP4: Tap Tempo Map window (see tap_tempo_window.h).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <ytkmm/table.h>

#include "pbd/compose.h"

#include "ardour/audio_track.h"
#include "ardour/session.h"
#include "ardour/tap_tempo.h"

#include "gtkmm2ext/utils.h"

#include "gui_thread.h"

#include "ardour_message.h"
#include "mouse_cursors.h"
#include "public_editor.h"
#include "tap_tempo_window.h"
#include "tempo_map_change.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using namespace Gtk;
using std::string;
using std::vector;

TapTempoWindow* TapTempoWindow::_instance = 0;

TapTempoWindow::TapTempoWindow (PublicEditor& e)
	: ArdourWindow (_("Tap Tempo Map"))
	, _editor (e)
	, _on_beats (_("every beat"))
	, _on_bars (_("every bar start"))
	, _divisions (1.0, 0)
	, _smooth (_("Smooth my timing"))
	, _tap (_("Tap (T)"))
	, _clear (_("Clear taps"))
	, _apply (_("Apply to the tempo map"))
{
	RadioButton::Group g = _on_beats.get_group ();
	_on_bars.set_group (g);

	_divisions.set_range (1, 16);
	_divisions.set_increments (1, 1);
	_divisions.set_value (4);
	for (char const* nv : { "2", "4", "8", "16" }) {
		_note_value.append_text (nv);
	}
	_note_value.set_active_text ("4");
	_smooth.set_active (true);

	Label* help = manage (new Label (
	    _("Play the song and press T (or click Tap) on the beat. Each time the song plays is a take; "
	      "tapping a passage again replaces it. The first tap is bar 1. Space starts and stops playback.")));
	help->set_line_wrap (true);
	help->set_size_request (420, -1);
	help->set_alignment (0, 0.5);

	Table* t = manage (new Table (4, 3));
	t->set_spacings (6);
	int row = 0;
	Label* l;
	l = manage (new Label (_("Tap on:"), 1.0, 0.5));
	t->attach (*l, 0, 1, row, row + 1, FILL, SHRINK);
	t->attach (_on_beats, 1, 2, row, row + 1, FILL, SHRINK);
	t->attach (_on_bars, 2, 3, row, row + 1, FILL, SHRINK);
	++row;
	l = manage (new Label (_("Time signature:"), 1.0, 0.5));
	t->attach (*l, 0, 1, row, row + 1, FILL, SHRINK);
	t->attach (_divisions, 1, 2, row, row + 1, FILL, SHRINK);
	t->attach (_note_value, 2, 3, row, row + 1, FILL, SHRINK);
	++row;
	l = manage (new Label (_("Snap taps to the hits of:"), 1.0, 0.5));
	t->attach (*l, 0, 1, row, row + 1, FILL, SHRINK);
	t->attach (_snap_track, 1, 3, row, row + 1, FILL, SHRINK);
	++row;
	t->attach (_smooth, 1, 3, row, row + 1, FILL, SHRINK);

	_tap.set_size_request (-1, 60);
	_status.set_alignment (0, 0.5);

	HBox* buttons = manage (new HBox (false, 6));
	buttons->pack_start (_clear, false, false);
	buttons->pack_end (_apply, false, false);

	VBox* vb = manage (new VBox (false, 8));
	vb->set_border_width (10);
	vb->pack_start (*help, false, false);
	vb->pack_start (*t, false, false);
	vb->pack_start (_tap, false, false);
	vb->pack_start (_status, false, false);
	vb->pack_start (*buttons, false, false);
	add (*vb);

	_tap.signal_clicked ().connect (sigc::mem_fun (*this, &TapTempoWindow::tap));
	_clear.signal_clicked ().connect (sigc::mem_fun (*this, &TapTempoWindow::clear));
	_apply.signal_clicked ().connect (sigc::mem_fun (*this, &TapTempoWindow::apply));

	update_status ();
	show_all_children ();
	_instance = this;
}

TapTempoWindow::~TapTempoWindow ()
{
	if (_instance == this) {
		_instance = 0;
	}
}

void
TapTempoWindow::set_session (Session* s)
{
	ArdourWindow::set_session (s);
	_session_connections.drop_connections ();
	_taps.clear ();
	_take.clear ();
	if (s) {
		s->TransportStateChange.connect (_session_connections, invalidator (*this), std::bind (&TapTempoWindow::transport_state_changed, this), gui_context ());
	}
	fill_tracks ();
	update_status ();
}

void
TapTempoWindow::fill_tracks ()
{
	_snap_track.clear_items ();
	_snap_track.append_text (_("nothing (use my taps)"));
	if (_session) {
		for (auto const& r : *_session->get_routes ()) {
			if (std::dynamic_pointer_cast<AudioTrack> (r)) {
				_snap_track.append_text (r->name ());
			}
		}
	}
	_snap_track.set_active (0);
}

bool
TapTempoWindow::key_press (GdkEventKey* ev)
{
	TapTempoWindow* w = _instance;
	if (!w || !w->get_visible () || !w->get_window () || ev->type != GDK_KEY_PRESS) {
		return false;
	}
	/* only key presses for this window; Space and the other shortcuts
	 * keep working as in the editor */
	if (!ev->window || gdk_window_get_toplevel (ev->window) != w->get_window ()->gobj ()) {
		return false;
	}
	if ((ev->keyval == GDK_t || ev->keyval == GDK_T) && !(ev->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK))) {
		w->tap ();
		return true;
	}
	return false;
}

void
TapTempoWindow::tap ()
{
	if (!_session || !_session->transport_rolling ()) {
		_status.set_text (_("Start playback first (Space), then tap."));
		return;
	}
	/* what is heard now: the playhead minus the output latency */
	_take.push_back (_session->audible_sample ());
	update_status ();
}

void
TapTempoWindow::transport_state_changed ()
{
	if (_session && !_session->transport_rolling ()) {
		end_take ();
	}
}

void
TapTempoWindow::end_take ()
{
	if (_take.empty ()) {
		return;
	}
	_taps = TapTempo::merge (_taps, _take);
	_take.clear ();
	update_status ();
}

vector<samplepos_t>
TapTempoWindow::all_taps () const
{
	return TapTempo::merge (_taps, _take);
}

void
TapTempoWindow::clear ()
{
	_taps.clear ();
	_take.clear ();
	update_status ();
}

void
TapTempoWindow::update_status ()
{
	vector<samplepos_t> const t = all_taps ();
	if (t.size () < 2 || !_session) {
		_status.set_text (string_compose (_("%1 taps"), t.size ()));
		_apply.set_sensitive (false);
		return;
	}
	TapTempo::Options opt;
	opt.unit = _on_bars.get_active () ? TapTempo::Bars : TapTempo::Beats;
	opt.divisions_per_bar = (int)_divisions.get_value ();
	double const bpm = TapTempo::tempo_for (t.back () - t[t.size () - 2], _session->sample_rate (), opt);
	_status.set_text (string_compose (_("%1 taps, now about %2 per minute"), t.size (), (int)std::lround (bpm)));
	_apply.set_sensitive (true);
}

void
TapTempoWindow::apply ()
{
	end_take ();
	if (!_session || _taps.size () < 2) {
		return;
	}

	TapTempo::Options opt;
	opt.unit              = _on_bars.get_active () ? TapTempo::Bars : TapTempo::Beats;
	opt.divisions_per_bar = (int)_divisions.get_value ();
	opt.note_value        = std::atoi (_note_value.get_active_text ().c_str ());
	opt.smooth            = _smooth.get_active ();

	TapTempo::Report    rep;
	vector<samplepos_t> taps = TapTempo::clean (_taps, opt, rep);

	if (_snap_track.get_active_row_number () > 0) {
		std::shared_ptr<Track> track = std::dynamic_pointer_cast<Track> (_session->route_by_name (_snap_track.get_active_text ()));
		vector<samplepos_t>    onsets;
		{
			PublicEditor::CursorRAII cr (_editor, _editor.cursors ()->wait);
			gdk_flush ();
			onsets = TapTempo::track_onsets (*_session, track);
		}
		/* a tap moves to the nearest hit within a quarter of the average
		 * tap interval, at most 60 ms */
		samplecnt_t const max_d = std::min<samplecnt_t> (_session->sample_rate () * 0.06,
		                                                 (taps.back () - taps.front ()) / (4 * (samplecnt_t)taps.size ()));
		taps = TapTempo::snap (taps, onsets, max_d, rep);
	}

	{
		TempoMapChange tmc (_editor, _("tap tempo map"));
		TapTempo::apply (tmc.map (), taps, _session->sample_rate (), opt);
	}

	string msg = string_compose (_("Tempo map made from %1 taps."), rep.taps);
	if (rep.doubles_removed) {
		msg += string_compose (_(" %1 double taps ignored."), rep.doubles_removed);
	}
	if (rep.missed_filled) {
		msg += string_compose (_(" %1 missed taps filled in."), rep.missed_filled);
	}
	if (rep.snapped) {
		msg += string_compose (_(" %1 taps moved onto hits."), rep.snapped);
	}
	msg += _(" Edit > Undo goes back to the previous map.");
	_status.set_text (msg);
}

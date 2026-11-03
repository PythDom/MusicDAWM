/*
 * MusicDAWM WP4: Tap Tempo Map window (see tap_tempo_window.h).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <set>

#include <ytkmm/separator.h>
#include <ytkmm/table.h>

#include "pbd/compose.h"

#include "ardour/audio_track.h"
#include "ardour/playlist.h"
#include "ardour/session.h"
#include "ardour/tap_tempo.h"

#include "temporal/tempo.h"

#include "gtkmm2ext/utils.h"

#include "widgets/tooltips.h"

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
	, _extend (_("Extend the grid to the whole song"))
	, _keep (_("Keep the tempo map outside the tapped passage"))
	, _tap (_("Tap (T)"))
	, _clear (_("Clear taps"))
	, _apply (_("Apply to the tempo map"))
	, _detect (_("Detect beats and bars"))
	, _earlier (_("Bar 1 one beat earlier"))
	, _later (_("Bar 1 one beat later"))
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
	{
		RadioButton::Group eg = _extend.get_group ();
		_keep.set_group (eg);
		_extend.set_active (true);
	}

	Label* help = manage (new Label (
	    _("Play the song and press T (or click Tap) on the beat. Each time the song plays is a take; "
	      "tapping a passage again replaces it. Start tapping on a downbeat: the first tap starts a bar. You can tap only part of the song. Space starts and stops playback.")));
	help->set_line_wrap (true);
	help->set_size_request (420, -1);
	help->set_alignment (0, 0.5);

	Table* t = manage (new Table (6, 3));
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
	++row;
	l = manage (new Label (_("Outside the taps:"), 1.0, 0.5));
	t->attach (*l, 0, 1, row, row + 1, FILL, SHRINK);
	t->attach (_extend, 1, 3, row, row + 1, FILL, SHRINK);
	++row;
	t->attach (_keep, 1, 3, row, row + 1, FILL, SHRINK);
	ArdourWidgets::set_tooltip (_extend, _("Before and after the taps, the grid follows the beat of the track chosen in \"Snap taps to the hits of\", or keeps your tapped tempo"));
	ArdourWidgets::set_tooltip (_keep, _("Only the tapped passage changes; the tempo map before and after it stays in place"));

	_tap.set_size_request (-1, 60);
	_status.set_alignment (0, 0.5);
	_status.set_line_wrap (true);
	_status.set_size_request (420, -1);

	HBox* buttons = manage (new HBox (false, 6));
	buttons->pack_start (_clear, false, false);
	buttons->pack_end (_apply, false, false);

	/* MusicDAWM: detection */
	Label* dhelp = manage (new Label (
	    _("Or let MusicDAWM find the beats and bars: tick the tracks to listen to (the drums, or several tracks together) and click Detect. "
	      "If you tapped a few beats first, your taps choose between half and double tempo, and your first tap marks a bar start.")));
	dhelp->set_line_wrap (true);
	dhelp->set_size_request (420, -1);
	dhelp->set_alignment (0, 0.5);
	Label* listen = manage (new Label (_("Listen to:"), 0.0, 0.5));
	HBox*  dbuttons = manage (new HBox (false, 6));
	dbuttons->pack_start (_detect, false, false);
	dbuttons->pack_start (_earlier, false, false);
	dbuttons->pack_start (_later, false, false);
	_earlier.set_sensitive (false);
	_later.set_sensitive (false);
	ArdourWidgets::set_tooltip (_detect, _("Find the tempo, every beat and the bar starts in the ticked tracks, and make the tempo map from them (Edit > Undo goes back)"));
	ArdourWidgets::set_tooltip (_earlier, _("The detected bar starts are one beat late: move them one beat earlier"));
	ArdourWidgets::set_tooltip (_later, _("The detected bar starts are one beat early: move them one beat later"));
	VBox* dvb = manage (new VBox (false, 6));
	dvb->pack_start (*dhelp, false, false);
	dvb->pack_start (*listen, false, false);
	dvb->pack_start (_track_box, false, false);
	dvb->pack_start (*dbuttons, false, false);

	VBox* vb = manage (new VBox (false, 8));
	vb->set_border_width (10);
	vb->pack_start (*dvb, false, false);
	vb->pack_start (*manage (new HSeparator ()), false, false);
	vb->pack_start (*help, false, false);
	vb->pack_start (*t, false, false);
	vb->pack_start (_tap, false, false);
	vb->pack_start (_status, false, false);
	vb->pack_start (*buttons, false, false);
	add (*vb);

	_tap.signal_clicked ().connect (sigc::mem_fun (*this, &TapTempoWindow::tap));
	_clear.signal_clicked ().connect (sigc::mem_fun (*this, &TapTempoWindow::clear));
	_apply.signal_clicked ().connect (sigc::mem_fun (*this, &TapTempoWindow::apply));
	_detect.signal_clicked ().connect (sigc::mem_fun (*this, &TapTempoWindow::detect));
	_earlier.signal_clicked ().connect (sigc::bind (sigc::mem_fun (*this, &TapTempoWindow::shift_bar), -1));
	_later.signal_clicked ().connect (sigc::bind (sigc::mem_fun (*this, &TapTempoWindow::shift_bar), 1));

	update_status ();
	show_all_children ();
	_instance = this;
}

TapTempoWindow::~TapTempoWindow ()
{
	for (auto const& tc : _track_checks) {
		delete tc.first;
	}
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
		s->RouteAdded.connect (_session_connections, invalidator (*this), std::bind (&TapTempoWindow::fill_tracks, this), gui_context ());
	}
	_detected = BeatDetect::Result ();
	_earlier.set_sensitive (false);
	_later.set_sensitive (false);
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

	/* the tracks to listen to: ticked as before; the first time, the drums */
	std::set<string> ticked;
	for (auto const& tc : _track_checks) {
		if (tc.first->get_active ()) {
			ticked.insert (tc.second);
		}
		_track_box.remove (*tc.first);
		delete tc.first;
	}
	bool const first_time = _track_checks.empty ();
	_track_checks.clear ();
	if (!_session) {
		return;
	}
	for (auto const& r : *_session->get_routes ()) {
		if (!std::dynamic_pointer_cast<AudioTrack> (r)) {
			continue;
		}
		CheckButton* cb = new CheckButton (r->name ());
		gchar*       l  = g_utf8_strdown (r->name ().c_str (), -1);
		string const lower (l);
		g_free (l);
		bool const drums = lower.find ("drum") != string::npos || lower.find ("batter") != string::npos || lower.find ("perc") != string::npos
		                   || lower.find ("kick") != string::npos || lower.find ("beat") != string::npos;
		cb->set_active (first_time ? drums : ticked.count (r->name ()) > 0);
		_track_box.pack_start (*cb, false, false);
		cb->show ();
		_track_checks.push_back (std::make_pair (cb, r->name ()));
	}
	if (first_time) {
		bool any = false;
		for (auto const& tc : _track_checks) {
			any |= tc.first->get_active ();
		}
		/* no drum track: all of them together */
		for (auto const& tc : _track_checks) {
			if (!any) {
				tc.first->set_active (true);
			}
		}
	}
}

void
TapTempoWindow::detect ()
{
	if (!_session) {
		return;
	}
	end_take ();
	vector<std::shared_ptr<Track>> tracks;
	for (auto const& tc : _track_checks) {
		if (tc.first->get_active ()) {
			if (std::shared_ptr<Track> t = std::dynamic_pointer_cast<Track> (_session->route_by_name (tc.second))) {
				tracks.push_back (t);
			}
		}
	}
	if (tracks.empty ()) {
		_status.set_text (_("Tick at least one track to listen to."));
		return;
	}

	/* the whole song: the session range, or the tracks' regions */
	samplepos_t start = _session->current_start_sample ();
	samplepos_t end   = _session->current_end_sample ();
	if (end <= start) {
		start = max_samplepos;
		end   = 0;
		for (auto const& t : tracks) {
			std::shared_ptr<RegionList> const rl = t->playlist ()->region_list ();
			for (auto const& r : *rl) {
				start = std::min (start, r->position_sample ());
				end   = std::max (end, r->position_sample () + r->length_samples ());
			}
		}
	}
	if (end <= start) {
		_status.set_text (_("The ticked tracks have no audio."));
		return;
	}

	TapTempo::Options topt;
	topt.unit              = _on_bars.get_active () ? TapTempo::Bars : TapTempo::Beats;
	topt.divisions_per_bar = (int)_divisions.get_value ();

	BeatDetect::Options opt;
	opt.beats_per_bar = topt.divisions_per_bar;
	/* the user's taps, if any: their tempo chooses the octave */
	vector<samplepos_t> const taps = _taps;
	if (taps.size () >= 3) {
		vector<samplecnt_t> iv;
		for (size_t i = 1; i < taps.size (); ++i) {
			iv.push_back (taps[i] - taps[i - 1]);
		}
		std::sort (iv.begin (), iv.end ());
		opt.prefer_bpm = TapTempo::tempo_for (iv[iv.size () / 2], _session->sample_rate (), topt); /* beats per minute */
	}

	_status.set_text (_("Listening to the tracks..."));
	{
		PublicEditor::CursorRAII cr (_editor, _editor.cursors ()->wait);
		while (gtk_events_pending ()) {
			gtk_main_iteration ();
		}
		_detected = BeatDetect::analyse_tracks (*_session, tracks, start, end, opt);
	}
	if (!_detected.ok) {
		_earlier.set_sensitive (false);
		_later.set_sensitive (false);
		_status.set_text (_("No steady beat found in the ticked tracks (they need at least 8 seconds of music with a pulse). Tap the beat instead."));
		return;
	}

	string note;
	if (!taps.empty ()) {
		/* the first tap marks a bar start: the beat nearest to it */
		size_t near = 0;
		for (size_t i = 0; i < _detected.beats.size (); ++i) {
			if (std::llabs (_detected.beats[i] - taps.front ()) < std::llabs (_detected.beats[near] - taps.front ())) {
				near = i;
			}
		}
		_detected.downbeat = (int)(near % opt.beats_per_bar);
		note               = _(" Bar starts taken from your first tap.");
	} else if (!_detected.downbeat_sure) {
		note = _(" The bar starts were hard to tell: check that bar 1 starts where it should, and use \"Bar 1 one beat earlier/later\" if not.");
	}
	_earlier.set_sensitive (true);
	_later.set_sensitive (true);
	apply_detected (note);
}

void
TapTempoWindow::apply_detected (string const& note)
{
	if (!_session || !_detected.ok) {
		return;
	}
	TapTempo::Options opt;
	opt.unit              = TapTempo::Beats;
	opt.divisions_per_bar = (int)_divisions.get_value ();
	opt.note_value        = std::atoi (_note_value.get_active_text ().c_str ());
	int const bpb         = std::max (1, opt.divisions_per_bar);
	int const db          = ((_detected.downbeat % bpb) + bpb) % bpb;
	if ((size_t)db + 2 > _detected.beats.size ()) {
		return;
	}
	vector<samplepos_t> const beats (_detected.beats.begin () + db, _detected.beats.end ());
	{
		TempoMapChange tmc (_editor, _("detect tempo map"));
		TapTempo::apply (tmc.map (), beats, _session->sample_rate (), opt);
	}

	string msg = string_compose (_("About %1 per minute, %2 beats found (pulse: %3)."), (int)std::lround (_detected.bpm), _detected.beats.size (),
	                             _detected.confidence > 0.6 ? _("clear") : _detected.confidence > 0.3 ? _("fair") : _("weak"));
	if (_detected.other_bpm > 0) {
		msg += string_compose (_(" If the song is really at %1, tap a few beats at that speed and detect again."), (int)std::lround (_detected.other_bpm));
	}
	if (!_detected.unsure.empty ()) {
		Temporal::TempoMap::SharedPtr tm (Temporal::TempoMap::use ());
		string                        where;
		for (auto const& u : _detected.unsure) {
			int const b0 = tm->bbt_at (timepos_t (u.first)).bars;
			int const b1 = tm->bbt_at (timepos_t (u.second)).bars;
			where += string_compose (where.empty () ? "%1-%2" : ", %1-%2", b0, b1);
		}
		msg += string_compose (_(" No clear beat in bars %1: check the grid there (tap those bars and Apply to fix them)."), where);
	}
	msg += note;
	msg += _(" Edit > Undo goes back to the previous map.");
	_status.set_text (msg);
}

void
TapTempoWindow::shift_bar (int beats)
{
	if (!_detected.ok) {
		return;
	}
	_detected.downbeat += beats;
	apply_detected (beats < 0 ? _(" Bar starts moved one beat earlier.") : _(" Bar starts moved one beat later."));
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

	vector<samplepos_t> onsets;
	if (_snap_track.get_active_row_number () > 0) {
		std::shared_ptr<Track> track = std::dynamic_pointer_cast<Track> (_session->route_by_name (_snap_track.get_active_text ()));
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

	opt.keep_outside = _keep.get_active ();
	if (!opt.keep_outside) {
		/* the whole song: the session range, or the taps if there is none */
		samplepos_t const start = _session->current_start_sample ();
		samplepos_t const end   = std::max (_session->current_end_sample (), taps.back ());
		taps                    = TapTempo::extend (taps, opt, onsets, std::min (start, taps.front ()), end, _session->sample_rate (), rep);
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
	if (rep.extended_before || rep.extended_after) {
		msg += string_compose (_(" Grid extended by %1 %2 before and %3 after the taps"), rep.extended_before, opt.unit == TapTempo::Bars ? _("bars") : _("beats"), rep.extended_after);
		msg += onsets.empty () ? string (_(" at your tapped tempo.")) : string_compose (_(", %1 of them on the track's hits."), rep.followed);
	}
	if (!rep.unsure.empty ()) {
		Temporal::TempoMap::SharedPtr tm (Temporal::TempoMap::use ());
		string              where;
		for (auto const& u : rep.unsure) {
			int const b0 = tm->bbt_at (timepos_t (u.first)).bars;
			int const b1 = tm->bbt_at (timepos_t (u.second)).bars;
			where += string_compose (where.empty () ? "%1-%2" : ", %1-%2", b0, b1);
		}
		msg += string_compose (_(" No clear hits in bars %1: check the grid there, or play and tap those bars again."), where);
	}
	msg += _(" Edit > Undo goes back to the previous map.");
	_status.set_text (msg);
}

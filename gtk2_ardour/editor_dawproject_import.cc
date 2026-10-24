/*
 * MusicDAWM: Session > Import DAWproject / Waveform project / Cubase project,
 * and Convert Waveform project to DAWproject.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <ytkmm/filechooserdialog.h>
#include <ytkmm/stock.h>

#include <glibmm/fileutils.h>

#include "pbd/basename.h"
#include "pbd/compose.h"

#include "ardour/dawproject_import.h"
#include "ardour/waveform_import.h"
#include "ardour/session.h"

#include "ardour_message.h"
#include "editor.h"
#include "mouse_cursors.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using namespace Gtk;

namespace {

/* ask for a file; "" if cancelled */
std::string
choose_file (std::string const& title, std::string const& filter_name, std::vector<std::string> const& patterns, bool save = false, std::string const& suggest = std::string ())
{
	FileChooserDialog chooser (title, save ? FILE_CHOOSER_ACTION_SAVE : FILE_CHOOSER_ACTION_OPEN);
	chooser.add_button (Stock::CANCEL, RESPONSE_CANCEL);
	chooser.add_button (save ? _("Save") : _("Import"), RESPONSE_ACCEPT);
	FileFilter filter;
	filter.set_name (filter_name);
	for (auto const& p : patterns) {
		filter.add_pattern (p);
	}
	chooser.add_filter (filter);
	chooser.set_default_response (RESPONSE_ACCEPT);
	if (save) {
		chooser.set_do_overwrite_confirmation (true);
		chooser.set_current_name (suggest);
	}
	if (chooser.run () != RESPONSE_ACCEPT) {
		return std::string ();
	}
	std::string const path = chooser.get_filename ();
	if (path.empty () || Glib::file_test (path, Glib::FILE_TEST_IS_DIR)) {
		return std::string ();
	}
	return path;
}

void
show_result (DAWProjectImport::Result const& res)
{
	std::string summary;
	if (res.ok) {
		summary = string_compose (_("Imported from %1: %2 tracks, %3 busses, %4 clips, %5 markers."),
		                          res.application.empty () ? _("an unknown program") : res.application,
		                          res.tracks, res.busses, res.regions, res.markers);
	} else {
		summary = _("The file could not be imported.");
	}

	std::string details;
	for (auto const& n : res.notes) {
		details += "\xe2\x80\xa2 " + n + "\n";
	}
	if (details.empty ()) {
		details = _("Everything in the file was imported.");
	} else {
		details = _("Not imported or changed:\n") + details;
	}

	ArdourMessageDialog msg (summary, false, res.ok ? MESSAGE_INFO : MESSAGE_ERROR, BUTTONS_OK);
	msg.set_secondary_text (details);
	msg.run ();
}

} /* namespace */

void
Editor::external_dawproject_dialog ()
{
	if (!_session) {
		return;
	}
	std::string const path = choose_file (_("Import DAWproject"), _("DAWproject files (Cubase, Studio One, Bitwig, ...)"), { "*.dawproject", "*.DAWPROJECT" });
	if (path.empty ()) {
		return;
	}
	DAWProjectImport::Result res;
	{
		CursorRAII cr (*this, _cursors->wait);
		gdk_flush ();
		res = DAWProjectImport::import (*_session, path);
	}
	show_result (res);
	if (res.ok) {
		temporal_zoom_session ();
	}
}

void
Editor::external_waveform_dialog ()
{
	if (!_session) {
		return;
	}
	std::string const path = choose_file (_("Import Waveform project"), _("Waveform / Tracktion edits (.tracktionedit)"), { "*.tracktionedit", "*.TRACKTIONEDIT" });
	if (path.empty ()) {
		return;
	}
	DAWProjectImport::Result res;
	{
		CursorRAII cr (*this, _cursors->wait);
		gdk_flush ();
		res = WaveformImport::import (*_session, path);
	}
	show_result (res);
	if (res.ok) {
		temporal_zoom_session ();
	}
}

void
Editor::external_cubase_dialog ()
{
	ArdourMessageDialog msg (_("Import a Cubase project"), false, MESSAGE_INFO, BUTTONS_OK_CANCEL);
	msg.set_secondary_text (_("MusicDAWM cannot read Cubase's .cpr files directly (the format is not published).\n\n"
	                          "In Cubase 14 (version 14.0.20 or later), open the project and choose File \xe2\x86\x92 Export \xe2\x86\x92 DAWproject. "
	                          "Then press OK here and pick the .dawproject file."));
	if (msg.run () != RESPONSE_OK) {
		return;
	}
	msg.hide ();
	external_dawproject_dialog ();
}

void
Editor::convert_waveform_dialog ()
{
	std::string const edit = choose_file (_("Waveform project to convert"), _("Waveform / Tracktion edits (.tracktionedit)"), { "*.tracktionedit", "*.TRACKTIONEDIT" });
	if (edit.empty ()) {
		return;
	}
	std::string const out = choose_file (_("Save as DAWproject"), _("DAWproject files"), { "*.dawproject" }, true,
	                                     std::string (PBD::basename_nosuffix (edit)) + ".dawproject");
	if (out.empty ()) {
		return;
	}
	std::vector<std::string> notes;
	bool                     ok;
	{
		CursorRAII cr (*this, _cursors->wait);
		gdk_flush ();
		ok = WaveformImport::write_dawproject (edit, out, notes);
	}
	std::string details;
	for (auto const& n : notes) {
		details += "\xe2\x80\xa2 " + n + "\n";
	}
	ArdourMessageDialog msg (ok ? string_compose (_("Saved %1."), out) : _("The project could not be converted."), false, ok ? MESSAGE_INFO : MESSAGE_ERROR, BUTTONS_OK);
	if (!details.empty ()) {
		msg.set_secondary_text (details);
	}
	msg.run ();
}

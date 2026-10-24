/*
 * MusicDAWM: Session > Import DAWproject...
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <ytkmm/filechooserdialog.h>
#include <ytkmm/stock.h>

#include <glibmm/fileutils.h>

#include "pbd/compose.h"

#include "ardour/dawproject_import.h"
#include "ardour/session.h"

#include "ardour_message.h"
#include "editor.h"
#include "mouse_cursors.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using namespace Gtk;

void
Editor::external_dawproject_dialog ()
{
	if (!_session) {
		return;
	}

	FileChooserDialog chooser (_("Import DAWproject"), FILE_CHOOSER_ACTION_OPEN);
	chooser.add_button (Stock::CANCEL, RESPONSE_CANCEL);
	chooser.add_button (_("Import"), RESPONSE_ACCEPT);
	FileFilter filter;
	filter.set_name (_("DAWproject files (Cubase, Studio One, Bitwig, ...)"));
	filter.add_pattern ("*.dawproject");
	filter.add_pattern ("*.DAWPROJECT");
	chooser.add_filter (filter);
	chooser.set_default_response (RESPONSE_ACCEPT);

	if (chooser.run () != RESPONSE_ACCEPT) {
		return;
	}
	std::string const path = chooser.get_filename ();
	chooser.hide ();
	if (path.empty () || Glib::file_test (path, Glib::FILE_TEST_IS_DIR)) {
		return;
	}

	DAWProjectImport::Result res;
	{
		CursorRAII cr (*this, _cursors->wait);
		gdk_flush ();
		res = DAWProjectImport::import (*_session, path);
	}

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

	if (res.ok) {
		temporal_zoom_session ();
	}
}

/*
 * MusicDAWM: import a DAWproject file (.dawproject) into a session.
 *
 * Written from the open DAWproject 1.0 specification (Project.xsd and its
 * documentation, github.com/bitwig/dawproject, MIT licence); no code from the
 * reference implementation is used.
 *
 * What comes across: tempo and time signature (including changes), markers,
 * tracks (audio and MIDI, folder tracks flattened), effect and group
 * channels as busses, output routing, sends, volume, pan, mute, solo,
 * colours, audio clips (offsets, fades, loops, disabled clips), notes, and
 * volume/pan/mute automation. Plug-ins and anything else are listed in the
 * report instead.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <string>
#include <vector>

#include "ardour/libardour_visibility.h"

namespace ARDOUR {

class Session;

class LIBARDOUR_API DAWProjectImport
{
public:
	struct Result {
		bool                     ok;
		std::string              application;   /* exporting program, e.g. "Cubase 14.0.20" */
		int                      tracks;        /* audio and MIDI tracks created */
		int                      busses;        /* busses created */
		int                      regions;       /* regions created */
		int                      markers;
		std::vector<std::string> notes;         /* what did not come across, for the user */

		Result () : ok (false), tracks (0), busses (0), regions (0), markers (0) {}
	};

	/* Import `path` into the session, adding to what is there (the usual
	 * case is a new, empty session). One undo step is not provided: the
	 * import creates routes, which Ardour does not undo.
	 */
	static Result import (Session&, std::string const& path);
};

} /* namespace ARDOUR */

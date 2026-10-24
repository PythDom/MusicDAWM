/*
 * MusicDAWM: import a Waveform / Tracktion project (.tracktionedit, with the
 * .tracktion project index next to it), by converting it to DAWproject and
 * importing that (DAWProjectImport).
 *
 * Written from the file layout documented in IOdaw's feasibility study and its
 * prototype reader (prototype/tracktion_inspect.py), checked there on a real
 * Waveform 11 project. No Tracktion Engine code is used.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <string>
#include <vector>

#include "ardour/dawproject_import.h"
#include "ardour/libardour_visibility.h"

namespace ARDOUR {

class Session;

class LIBARDOUR_API WaveformImport
{
public:
	/* Convert `edit` to a DAWproject project.xml. Audio files are referenced
	 * by absolute path. Problems go to `notes`. Returns false if the edit
	 * cannot be read.
	 */
	static bool to_dawproject (std::string const& edit, std::string& project_xml, std::vector<std::string>& notes);

	/* Convert and write a .dawproject file with the audio files inside, for
	 * use in other programs. */
	static bool write_dawproject (std::string const& edit, std::string const& out, std::vector<std::string>& notes);

	/* Convert and import into the session. */
	static DAWProjectImport::Result import (Session&, std::string const& edit);
};

} /* namespace ARDOUR */

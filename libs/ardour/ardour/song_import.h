/*
 * MusicDAWM WP2: a new song from WAV files.
 *
 * The files (e.g. the takes of a Zoom R8 project) become one track each,
 * named after the file, each file at its recorded position: the BWF time
 * stamp (bext time reference) relative to the earliest file. Files are
 * copied into the session (converted to its sample rate if needed).
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
#include "ardour/types.h"

namespace ARDOUR {

class Session;

class LIBARDOUR_API SongImport
{
public:
	struct File {
		std::string path;
		bool        ok                 = false;
		uint32_t    channels           = 0;
		int         rate               = 0;
		samplecnt_t length             = 0; /* at the file's rate */
		bool        has_time_reference = false;
		int64_t     time_reference     = 0; /* samples at the file's rate */
	};

	struct Report {
		int                      tracks    = 0;
		int                      converted = 0; /* files at another sample rate */
		int                      no_stamp  = 0; /* files without a BWF time stamp (placed at the start) */
		std::vector<std::string> failed;        /* files that could not be read or imported */
	};

	static File        probe (std::string const& path);
	static std::string track_name (std::string const& path);
	/* the WAV files of a folder, in name order */
	static std::vector<std::string> wav_files_in (std::string const& folder);

	static Report add_files (Session&, std::vector<std::string> paths);
};

} // namespace ARDOUR

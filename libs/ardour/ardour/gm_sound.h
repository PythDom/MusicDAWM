/*
 * MusicDAWM: General MIDI sounds from the bundled soundfont.
 *
 * GeneralUser GS (S. Christian Collins, see THIRD_PARTY.md) is played by
 * Ardour's own ACE Fluid Synth plug-in. Drum Studio uses its drum kits and
 * Bass Studio its basses. The plug-in remembers the soundfont file but not
 * the chosen program, so the program is written as a program change at the
 * start of each groove (DrumStudio::groove_file).
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

#include "ardour/libardour_visibility.h"
#include "ardour/plugin.h"

namespace ARDOUR {

class Route;

class LIBARDOUR_API GMSound
{
public:
	struct Program {
		std::string name;
		int         bank;    /* 0 for the GM set; drum kits are chosen by program on channel 10 */
		int         program; /* 0 .. 127 */
	};

	/* the soundfont file, or empty if it is not installed */
	static std::string soundfont ();
	/* ACE Fluid Synth, or null */
	static std::shared_ptr<PluginInfo> synth ();
	static bool available () { return !soundfont ().empty () && synth (); }

	/* load the soundfont into a Fluid Synth instance, or into the Fluid
	 * Synth of a route (e.g. after the program folder moved); false if
	 * there is none */
	static bool load_soundfont (std::shared_ptr<Plugin>);
	static bool load_soundfont (std::shared_ptr<Route>);

	static std::vector<Program> const& drum_kits ();
	static std::vector<Program> const& basses ();
};

} /* namespace ARDOUR */

/*
 * MusicDAWM: General MIDI sounds from the bundled soundfont (see ardour/gm_sound.h).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include "pbd/file_utils.h"

#include "ardour/filesystem_paths.h"
#include "ardour/gm_sound.h"
#include "ardour/plugin_insert.h"
#include "ardour/plugin_manager.h"
#include "ardour/route.h"
#include "ardour/search_paths.h"
#include "ardour/uri_map.h"
#include "ardour/variant.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using std::string;
using std::vector;

static char const* const sf2_name     = "GeneralUser-GS.sf2";
static char const* const fluid_uri    = "urn:ardour:a-fluidsynth";
static char const* const fluid_sf2key = "urn:ardour:a-fluidsynth:sf2file";

string
GMSound::soundfont ()
{
	string             path;
	PBD::Searchpath    sp (ardour_data_search_path ());
	sp.add_subdirectory_to_paths (X_("soundfonts"));
	if (PBD::find_file (sp, sf2_name, path)) {
		return path;
	}
	/* a source tree (ardev, tests) */
	string const env = Glib::getenv (X_("MUSICDAWM_SOUNDFONT"));
	if (!env.empty () && Glib::file_test (env, Glib::FILE_TEST_EXISTS)) {
		return env;
	}
	return string ();
}

std::shared_ptr<PluginInfo>
GMSound::synth ()
{
#ifdef LV2_SUPPORT
	for (auto const& pi : PluginManager::instance ().lv2_plugin_info ()) {
		if (pi->unique_id == fluid_uri) {
			return pi;
		}
	}
#endif
	return std::shared_ptr<PluginInfo> ();
}

bool
GMSound::load_soundfont (std::shared_ptr<Plugin> p)
{
	string const sf2 = soundfont ();
	if (!p || sf2.empty () || p->unique_id () != fluid_uri) {
		return false;
	}
	p->set_property (URIMap::instance ().uri_to_id (fluid_sf2key), Variant (Variant::PATH, sf2));
	return true;
}

bool
GMSound::load_soundfont (std::shared_ptr<Route> r)
{
	if (!r) {
		return false;
	}
	bool done = false;
	for (uint32_t i = 0;; ++i) {
		std::shared_ptr<PluginInsert> pi = std::dynamic_pointer_cast<PluginInsert> (r->nth_plugin (i));
		if (!pi) {
			break;
		}
		done |= load_soundfont (pi->plugin ());
	}
	return done;
}

vector<GMSound::Program> const&
GMSound::drum_kits ()
{
	/* GeneralUser GS's drum kits (bank 128), the ones that suit songs */
	static vector<Program> const kits {
		{ "Standard", 0, 0 }, { "Room", 0, 8 }, { "Power", 0, 16 }, { "Electronic", 0, 24 },
		{ "808/909", 0, 25 }, { "Dance", 0, 26 }, { "Jazz", 0, 32 }, { "Brush", 0, 40 },
	};
	return kits;
}

vector<GMSound::Program> const&
GMSound::basses ()
{
	static vector<Program> const b {
		{ "Acoustic Bass", 0, 32 }, { "Finger Bass", 0, 33 }, { "Pick Bass", 0, 34 }, { "Fretless Bass", 0, 35 },
		{ "Slap Bass 1", 0, 36 }, { "Slap Bass 2", 0, 37 }, { "Synth Bass 1", 0, 38 }, { "Synth Bass 2", 0, 39 },
		{ "Acid Bass", 8, 38 }, { "Techno Bass", 11, 38 },
	};
	return b;
}

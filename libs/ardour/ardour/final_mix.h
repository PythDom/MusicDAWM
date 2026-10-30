/*
 * MusicDAWM WP7: the Final Mix model.
 *
 * The Final Mix window shows a simple, fixed set of tools per track. They
 * are ordinary Ardour processors, recognised by their names, so the
 * classic mixer shows and can edit them too, and sessions open in stock
 * Ardour:
 *   "Final Mix EQ"     x42-fil4: high-pass and a high shelf ("Brightness")
 *   "Final Mix Punch"  ACE Compressor (amount 0..1)
 *   a send to the "Final Mix Space" bus (ACE Reverb; amount 0..1)
 *   "Final Mix Limiter" on the master bus: x42-dpl, last
 * The simple settings (role, amounts) are kept in the route's extra XML,
 * the window-wide ones (FX on, loudness target) in the session's.
 *
 * Removing a track from the mix (the window's minus button) mutes it and
 * remembers its earlier mute state; bringing it back restores that. Both
 * are undoable (ExcludeCommand).
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

#include "pbd/command.h"
#include "pbd/id.h"
#include "pbd/signals.h"

#include "ardour/libardour_visibility.h"
#include "ardour/types.h"

namespace ARDOUR {

class PluginInsert;
class Route;
class Session;

class LIBARDOUR_API FinalMix
{
public:
	enum Role {
		Drums = 0,
		Bass,
		Guitars,
		Keys,
		Vocals,
		BackingVocals,
		Other,
		NumRoles
	};

	struct Settings {
		Role  role          = Other;
		bool  high_pass     = false;
		float hp_freq       = 80;  /* Hz */
		float brightness_db = 0;   /* high shelf at 6 kHz, -6..+6 dB */
		float punch         = 0;   /* 0..1 */
		float space         = 0;   /* 0..1 */

		bool neutral () const { return !high_pass && brightness_db == 0 && punch == 0 && space == 0; }
	};

	enum Target {
		Streaming = 0, /* -14 LUFS */
		Loud,          /* -9 LUFS */
		Custom
	};

	FinalMix (Session&);

	static std::string role_name (Role);
	static Role        guess_role (std::string const& track_name);
	static float       default_hp_freq (Role);

	/* the tracks the window shows (all tracks, in order), and the ones removed */
	std::vector<std::shared_ptr<Route>> tracks () const;
	bool excluded (std::shared_ptr<Route>) const;
	void set_excluded (std::shared_ptr<Route>, bool); /* one undo step */

	Settings settings (std::shared_ptr<Route>) const;
	/* creates, sets and (de)activates the track's Final Mix processors */
	void set_settings (std::shared_ptr<Route>, Settings const&);
	/* settings neutral, fader 0 dB, pan centre */
	void reset (std::shared_ptr<Route>);

	/* all Final Mix processors on or off (the settings are kept) */
	bool fx_enabled () const;
	void set_fx_enabled (bool);

	Target target () const;
	float  target_lufs () const;
	void   set_target (Target, float custom_lufs = -14);

	/* the master limiter (made on first use) */
	float limiter_ceiling () const; /* dBTP */
	void  set_limiter_ceiling (float);

	std::shared_ptr<Route> space_bus () const;

	/* a track was removed from or brought back into the mix (also by undo) */
	static PBD::Signal<void()> ExclusionChanged;

	static char const* const eq_name;
	static char const* const punch_name;
	static char const* const limiter_name;
	static char const* const space_bus_name;

	class LIBARDOUR_API ExcludeCommand : public PBD::Command
	{
	public:
		ExcludeCommand (Session&, std::shared_ptr<Route>, bool exclude);
		ExcludeCommand (Session&, XMLNode const&);
		void     operator() ();
		void     undo ();
		XMLNode& get_state () const;

	private:
		Session& _session;
		PBD::ID  _route;
		bool     _exclude;
		void     apply (bool) const;
	};

private:
	Session& _session;

	std::shared_ptr<PluginInsert> find_plugin (std::shared_ptr<Route>, char const* name) const;
	std::shared_ptr<PluginInsert> make_plugin (std::shared_ptr<Route>, char const* name, std::string const& uri, bool pre_fader);
	std::shared_ptr<Route>        make_space_bus ();
	std::shared_ptr<PluginInsert> limiter (bool create);
	XMLNode*                      session_node () const;

	static void store_excluded (Session&, std::shared_ptr<Route>, bool);
};

} // namespace ARDOUR

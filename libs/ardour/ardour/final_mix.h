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

#include <functional>
#include <map>
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

	enum Genre {
		Pop = 0,
		Rock,
		Acoustic,
		Electronic,
		HipHop,
		Metal,
		Jazz,
		NumGenres
	};

	enum Target {
		Streaming = 0, /* -14 LUFS */
		Loud,          /* -9 LUFS */
		Custom
	};

	FinalMix (Session&);

	static std::string genre_name (Genre);
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

	/* what the window can undo in one step: the faders, pans and settings
	 * of the tracks in the mix */
	struct TrackState {
		PBD::ID  route;
		float    gain;
		float    pan; /* azimuth 0 (left) .. 1 (right); < 0: no panner */
		Settings settings;
	};
	typedef std::vector<TrackState> Snapshot;
	Snapshot snapshot () const;
	void     restore (Snapshot const&);

	/* Starting mix: balance (from each track's measured loudness), pan and
	 * effects from rules per genre and role; one undo step. Returns the
	 * number of tracks set. */
	Genre genre () const;
	int   starting_mix (Genre);
	/* Randomise FX: new effect amounts around the genre's (faders and pans
	 * stay); one undo step */
	void randomise (uint32_t seed);
	/* every track in the mix reset; one undo step */
	void reset_all ();

	/* the integrated loudness (LUFS) of a track's audio, with region gain
	 * and Region FX, before its plug-ins and fader, as it would sound at
	 * the centre of a stereo mix; <= -200: silent or no audio */
	float track_loudness (std::shared_ptr<Route>) const;

	/* ---- listening, analysis, reference, finalising (M5, R1) ---- */

	enum { NumBands = 6 };
	static std::string band_name (int);

	/* what the analysis of a song or a track says */
	struct Analysis {
		bool   valid         = false;
		float  lufs          = -200; /* integrated */
		float  true_peak     = -200; /* dBTP */
		float  band_db[NumBands] = { 0 }; /* energy of each band relative to the whole, dB */
		float  width_db      = -200; /* side relative to mid, dB; -200: mono */
		double seconds       = 0;
	};
	static Analysis analyse_file (std::string const& path);
	Analysis        analyse_track (std::shared_ptr<Route>) const;

	/* how the mix sounds now: exports the master to a temporary file
	 * (calling idle () while the export runs) and analyses it */
	Analysis analyse_mix (std::function<void()> idle);
	Analysis last_mix_analysis () const;

	/* Before: the tracks as recorded (faders 0 dB, pan centre, Final Mix
	 * effects off) at the loudness of the mix; After: the mix; Ref: the
	 * reference track alone, at the loudness of the mix. Before keeps the
	 * mix in the session file and comes back to it when the session or
	 * the window is opened again. */
	enum Listen { After = 0, Before, Ref };
	Listen listen () const;
	bool   set_listen (Listen);

	/* the reference song: imported as a track that plays straight to the
	 * outputs (not through the master bus), muted except in Ref */
	std::shared_ptr<Route> reference_track () const;
	bool                   is_reference (std::shared_ptr<Route>) const;
	int                    set_reference (std::string const& path); /* 0: done */
	Analysis               reference_analysis () const;

	/* R1: rule-based matching of the mix to the reference */
	struct Match {
		bool        valid = false;
		float       target_lufs = -14;
		float       eq_db[NumBands] = { 0 }; /* master EQ per band */
		std::string report;                  /* what was done and what to try */
	};
	Match compute_match (Analysis const& mix, Analysis const& ref) const;
	void  apply_match (Match const&);
	void  clear_match ();
	bool  matched () const;

	/* Finalise Mix: the song to <folder>/<name>.wav (24 bit) and, when
	 * FFmpeg is installed, <name>.mp3, normalised to the loudness target
	 * with a true-peak limiter at the ceiling */
	struct Finalised {
		bool        ok = false;
		std::string wav, mp3, error;
		float       lufs = -200, true_peak = -200;
	};
	static bool mp3_available ();
	Finalised   finalise (std::string const& folder, std::string const& name, bool mp3, std::function<void()> idle);

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
	/* faders, pans or settings changed together (Starting mix, undo ...) */
	static PBD::Signal<void()> SettingsChanged;

	static char const* const eq_name;
	static char const* const punch_name;
	static char const* const limiter_name;
	static char const* const space_bus_name;
	static char const* const match_eq_name;

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

	class LIBARDOUR_API SnapshotCommand : public PBD::Command
	{
	public:
		SnapshotCommand (FinalMix&, std::string const& name, Snapshot const& before, Snapshot const& after);
		SnapshotCommand (Session&, XMLNode const&);
		~SnapshotCommand ();
		void     operator() ();
		void     undo ();
		XMLNode& get_state () const;

	private:
		FinalMix* _fm;
		bool      _own_fm;
		Snapshot  _before;
		Snapshot  _after;
	};

private:
	Session& _session;

	/* track loudness, until the track's regions change */
	mutable std::map<PBD::ID, std::pair<std::string, float>> _loudness_cache;
	std::string region_signature (std::shared_ptr<Route>) const;
	Analysis    analyse_track (std::shared_ptr<Route>, bool spectrum) const;

	void commit_snapshot (std::string const& name, Snapshot const& before);
	int  export_master (std::string const& folder, std::string const& name, bool wav, bool mp3, bool normalise, std::function<void()> idle);
	float estimate_mix_lufs (bool before) const;
	static XMLNode& snapshot_state (Snapshot const&, XMLNode&);
	static Snapshot snapshot_from (XMLNode const&);

	std::shared_ptr<PluginInsert> find_plugin (std::shared_ptr<Route>, char const* name) const;
	std::shared_ptr<PluginInsert> make_plugin (std::shared_ptr<Route>, char const* name, std::string const& uri, bool pre_fader);
	std::shared_ptr<Route>        make_space_bus ();
	std::shared_ptr<PluginInsert> limiter (bool create);
	XMLNode*                      session_node () const;

	static void store_excluded (Session&, std::shared_ptr<Route>, bool);
};

} // namespace ARDOUR

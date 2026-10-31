/*
 * MusicDAWM WP4: Tap Tempo Map.
 *
 * The user taps along with a recording played without a click, on beats or
 * on bar starts. The taps are cleaned (double taps dropped, missed taps
 * filled, timing jitter smoothed), optionally moved to the nearest onset of
 * a track, and turned into a tempo map whose grid follows the performance:
 * one tempo point per tap, bar 1 on the first tap.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <string>
#include <vector>

#include "temporal/tempo.h"

#include "ardour/libardour_visibility.h"
#include "ardour/types.h"

namespace ARDOUR {

class Session;
class Track;

class LIBARDOUR_API TapTempo
{
public:
	enum Unit {
		Beats, /* a tap on every beat */
		Bars,  /* a tap on every bar start */
	};

	struct Options {
		Options () : unit (Beats), divisions_per_bar (4), note_value (4), smooth (true), keep_outside (false) {}
		Unit unit;
		int  divisions_per_bar; /* the meter, e.g. 4/4 or 6/8 */
		int  note_value;
		bool smooth;            /* smooth timing jitter */
		bool keep_outside;      /* apply (): keep the map before and after the taps */
	};

	struct Report {
		Report () : taps (0), doubles_removed (0), missed_filled (0), snapped (0), extended_before (0), extended_after (0), followed (0) {}
		int taps;            /* taps used for the map */
		int doubles_removed;
		int missed_filled;
		int snapped;         /* taps moved to an onset */
		int extended_before; /* extend (): beats (or bars) added before the taps */
		int extended_after;  /* ... and after them */
		int followed;        /* ... of which locked onto a hit of the track */
		/* extend (): stretches where no hit was found for 2 or more beats
		 * in a row (a break, a quiet passage): the grid keeps the tempo
		 * there, to be checked or re-tapped */
		std::vector<std::pair<samplepos_t, samplepos_t>> unsure;
	};

	/* Merge a new take into the taps so far: the new taps replace the old
	 * ones between their first and last tap (re-tapping a passage). */
	static std::vector<samplepos_t> merge (std::vector<samplepos_t> const& old_taps, std::vector<samplepos_t> const& take);

	/* Drop double taps, fill missed taps, smooth jitter. */
	static std::vector<samplepos_t> clean (std::vector<samplepos_t> taps, Options const&, Report&);

	/* Move each tap to the nearest onset within max_distance (both sorted). */
	static std::vector<samplepos_t> snap (std::vector<samplepos_t> const& taps, std::vector<samplepos_t> const& onsets, samplecnt_t max_distance, Report&);

	/* Onsets in a block of audio (sample offsets). */
	static std::vector<samplepos_t> detect_onsets (Sample const*, samplecnt_t, samplecnt_t sample_rate);

	/* Onsets (session positions) of the audio regions of a track, for snap(). */
	static std::vector<samplepos_t> track_onsets (Session&, std::shared_ptr<Track>);

	/* Extend tapped (cleaned) taps over [start, end), the whole song. With
	 * onsets (the hits of a track), the beat is followed: each next beat is
	 * predicted from the current tempo and moves onto the hit found near
	 * it, and the tempo follows the drift. Without onsets, or where there
	 * is no hit, the tempo of the nearest 16 tapped intervals continues.
	 * Before the taps, the first tap stays the start of a bar. */
	static std::vector<samplepos_t> extend (std::vector<samplepos_t> const& taps, Options const&, std::vector<samplepos_t> const& onsets,
	                                        samplepos_t start, samplepos_t end, samplecnt_t sample_rate, Report&);

	/* Replace the map with one that follows the taps (at least 2): bar 1
	 * starts on the first tap. With Options::keep_outside, only the tapped
	 * passage changes: the map before and after it stays where it was in
	 * time, and the bar numbers carry on. */
	static bool apply (Temporal::TempoMap& map, std::vector<samplepos_t> const& taps, samplecnt_t sample_rate, Options const&);

	/* Tempo (in the meter's note value, per minute) for this tap interval. */
	static double tempo_for (samplecnt_t interval, samplecnt_t sample_rate, Options const&);
};

} /* namespace ARDOUR */

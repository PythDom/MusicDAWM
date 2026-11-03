/*
 * MusicDAWM: beat and bar detection for the Tap Tempo Map.
 *
 * Ported from RunBPM (src/core/bpm.ts and beats.ts, by the same author, put
 * under the GPL for MusicDAWM, D-052):
 *  1. onset strength: spectral flux of the signal brought down to ~11 kHz;
 *  2. tempo: autocorrelation of the onset curve scored by a comb of 4, then
 *     16 beats; the octave closest to the user's taps (or to a typical song
 *     tempo) is kept;
 *  3. beats: dynamic programming (D. Ellis, "Beat Tracking by Dynamic
 *     Programming", 2007): the sequence of beats through the strongest
 *     onsets with intervals close to the period, so it follows a song played
 *     without a click.
 * Added for MusicDAWM:
 *  4. the first beat of the bar: of the possible positions in the bar, the
 *     one whose beats carry the most bass accent and the most change of
 *     sound (chords and parts change on downbeats);
 *  5. unsure passages: two or more beats in a row with no clear onset.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "ardour/libardour_visibility.h"
#include "ardour/types.h"

namespace ARDOUR {

class Session;
class Track;

class LIBARDOUR_API BeatDetect
{
public:
	struct Result {
		bool   ok         = false;
		double bpm        = 0; /* average tempo of the tracked beats */
		double other_bpm  = 0; /* the other plausible octave (half or double), 0 if none */
		double confidence = 0; /* 0 (no pulse) .. 1 (very clear pulse) */
		/* the beats (session positions), and which of them is the first
		 * downbeat (bar start): beats[downbeat], beats[downbeat + n], ... */
		std::vector<samplepos_t> beats;
		int                      downbeat = 0;
		bool                     downbeat_sure = false; /* clearly more accented than the other positions */
		/* passages without a clear onset (two or more beats in a row) */
		std::vector<std::pair<samplepos_t, samplepos_t>> unsure;
	};

	struct Options {
		int    beats_per_bar = 4;
		double prefer_bpm    = 0;  /* e.g. from the user's taps; 0: a typical song tempo */
		double min_bpm       = 50;
		double max_bpm       = 220;
	};

	/* Analyse a mono signal; positions are sample offsets plus `origin`. */
	static Result analyse (float const* mono, samplecnt_t n, samplecnt_t sample_rate, Options const&, samplepos_t origin = 0);

	/* Analyse the mix of these tracks' audio over [start, end). */
	static Result analyse_tracks (Session&, std::vector<std::shared_ptr<Track>> const&, samplepos_t start, samplepos_t end, Options const&);

	/* exposed for tests */
	struct Envelope {
		std::vector<double> onset;  /* spectral flux, detrended */
		std::vector<double> bass;   /* flux below 200 Hz */
		std::vector<std::vector<float>> bands; /* log energy per frame in 12 bands */
		double fps        = 0; /* frames per second */
		double frame0     = 0; /* time (s) of frame 0's onset */
	};
	static Envelope envelope (float const* mono, samplecnt_t n, samplecnt_t sample_rate);
	static double   tempo (Envelope const&, Options const&, double& other_bpm, double& confidence);
	static std::vector<int> track (Envelope const&, double bpm);
};

} /* namespace ARDOUR */

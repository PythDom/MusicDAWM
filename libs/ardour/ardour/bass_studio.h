/*
 * MusicDAWM Bass Studio: a bass line from a typed chord chart.
 *
 * The user types the chords, one bar per token ("C G Am F", "|" and new
 * lines are ignored, "C,G" is two chords in one bar, "%" repeats the bar
 * before, "N.C." or "x" is a bar without bass, "[Verse]" is a comment),
 * picks a style, a complexity (1 = simple .. 4 = busy), a feel and a bass
 * sound. The bass line is generated as an ordinary, editable MIDI region on
 * a bass track (a General MIDI bass of the bundled soundfont, played by
 * ACE Fluid Synth). Optionally it follows the Drum Studio track: the
 * complexity of each bar from the drum part played there, and, in the
 * "Follow the kick" style, a note on every kick drum hit.
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

#include "ardour/drum_studio.h"
#include "ardour/libardour_visibility.h"

namespace ARDOUR {

class MidiTrack;
class Region;
class Route;
class Session;

class LIBARDOUR_API BassStudio
{
public:
	struct Chord {
		int         root    = 0;  /* pitch class, C = 0 */
		int         bass    = 0;  /* pitch class of the bass note (slash chords), else the root */
		int         third   = 4;  /* semitones above the root: 3 minor, 4 major, 2 sus2, 5 sus4 */
		int         fifth   = 7;  /* 6 diminished, 8 augmented */
		int         sixth   = 9;
		int         seventh = 10; /* 10 minor 7th (also the default for walking), 11 major 7th, 9 diminished 7th */
		std::string text;
	};

	struct Bar {
		std::vector<Chord> chords; /* sharing the bar evenly; empty: no bass */
	};

	struct Style {
		std::string key;
		std::string name;
	};

	struct Options {
		std::string      style      = "rock8";
		int              complexity = 2;
		DrumStudio::Feel feel       = DrumStudio::FeelNormal;
		int              program    = 33; /* General MIDI: Finger Bass */
		int              bank       = 0;
		bool             follow_drums = true; /* complexity of each bar from the drum part played there */
	};

	struct Note {
		double start; /* beats (quarter notes) from the start of the chart */
		double length;
		int    pitch;
		int    velocity;
	};

	static std::vector<Style> const& styles ();
	static Style const*              style (std::string const& key);

	/* parse a chord chart; false with a message naming the token on error */
	static bool parse_chart (std::string const& text, std::vector<Bar>& bars, std::string& error);
	static bool parse_chord (std::string const& token, Chord&);

	/* The notes of the bass line. beats_per_bar in quarter notes;
	 * complexity_per_bar (optional) overrides the complexity bar by bar
	 * (0: the option's); kicks_per_bar (for "Follow the kick") are kick
	 * times in beats within each bar. */
	static std::vector<Note> generate (std::vector<Bar> const&, Options const&, double beats_per_bar,
	                                   std::vector<int> const& complexity_per_bar = std::vector<int> (),
	                                   std::vector<std::vector<double>> const& kicks_per_bar = std::vector<std::vector<double>> ());

	/* write the notes as a Standard MIDI File (one track, channel 1, with
	 * the program change first) */
	static bool write_midi (std::string const& path, std::vector<Note> const&, double length_beats, int program, int bank);

	/* ---- in a session ---- */

	static bool                       is_bass_track (std::shared_ptr<Route>);
	static std::shared_ptr<MidiTrack> find_bass_track (Session&);
	static std::shared_ptr<MidiTrack> create_bass_track (Session&);

	/* The drum track's part and kicks, bar by bar from start_bar (1-based). */
	static void drum_parts (Session&, int start_bar, int n_bars, std::vector<int>& complexity_per_bar,
	                        std::vector<std::vector<double>>& kicks_per_bar);

	/* Make (or remake) the bass line: the bass track is created if needed,
	 * its earlier generated bass line replaced (one undo step). Returns
	 * the number of notes, or -1 with a message. The chart and options are
	 * saved with the session. */
	static int make_bass_line (Session&, std::string const& chart, int start_bar, Options const&, std::string& error);

	/* a region of the first bars of the line, for previewing it */
	static std::shared_ptr<Region> preview_region (Session&, std::string const& chart, int start_bar, Options const&, int bars, std::string& error);

	/* what was saved with the session (empty chart if nothing) */
	static void saved (Session&, std::string& chart, int& start_bar, Options&);
};

} /* namespace ARDOUR */

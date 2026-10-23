/*
 * MusicDAWM Drum Studio: style-based drum grooves on a MIDI track.
 *
 * A drum track is a MIDI track whose trigger box holds one groove per
 * (song part, complexity) cell: 7 parts x 4 complexities = 28 slots. Cells
 * are switched live as cues ("perform mode"); Rec Cues records the switches
 * as cue markers, which replay identically and can be dragged. "Flatten"
 * turns the performance into ordinary MIDI regions.
 *
 * The grooves come from share/drumstudio/grooves (see
 * tools/drumstudio/make_grooves.py). They are plain General MIDI drum notes,
 * so any drum instrument works; the bundled AVL kit is the default.
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

#include "temporal/timeline.h"

#include "ardour/libardour_visibility.h"
#include "ardour/plugin.h"

namespace ARDOUR {

class MidiTrack;
class Route;
class Session;

class LIBARDOUR_API DrumStudio
{
public:
	struct Groove {
		std::string part;       /* part key, e.g. "verse" */
		int         complexity; /* 1 .. 4 */
		int         bars;
		std::string path;       /* absolute path of the .mid file */
	};

	struct Style {
		std::string key;  /* e.g. "rock" */
		std::string name; /* e.g. "Rock" */
		int         meter_num;
		int         meter_den;
		double      tempo; /* suggested tempo, BPM */
		std::vector<Groove> grooves;

		Groove const* groove (std::string const& part, int complexity) const;
	};

	struct Part {
		std::string key;  /* "intro", "verse", ... */
		std::string name; /* "Intro", "Verse", ... */
	};

	enum Quantize {
		QuantizeBeat,
		QuantizeBar,
		QuantizeTwoBars
	};

	static const int complexities = 4;

	/* the groove library, loaded from the data search path on first use */
	static std::vector<Part> const&  parts ();
	static std::vector<Style> const& styles ();
	static Style const*              style (std::string const& key);
	static int                       part_index (std::string const& key); /* -1 if unknown */

	/* Fill and ending play once; the others loop. */
	static bool part_loops (int part_index);
	static bool part_is_fill (int part_index);

	/* trigger slot (= cue number) of a cell; part_index 0..6, complexity 1..4 */
	static int  slot (int part_index, int complexity);
	static int  slot_count ();
	static void cell_of_slot (int slot, int& part_index, int& complexity);

	/* the bundled AVL kits (x42-avldrums), if installed; first is the default */
	static std::vector<std::shared_ptr<PluginInfo>> bundled_kits ();

	/* Create a MIDI track with the given instrument (or the default kit, or no
	 * instrument if none is installed) and the style's grooves in its slots.
	 * The track is hidden from the Cue page; Drum Studio is its UI.
	 */
	static std::shared_ptr<MidiTrack> create_drum_track (Session&, std::string const& style_key,
	                                                     std::shared_ptr<PluginInfo> instrument = std::shared_ptr<PluginInfo> ());

	/* Replace the grooves of an existing drum track with another style's. */
	static int load_style (std::shared_ptr<MidiTrack>, std::string const& style_key);

	static bool        is_drum_track (std::shared_ptr<Route>);
	static std::string style_of (std::shared_ptr<Route>);
	static std::shared_ptr<MidiTrack> find_drum_track (Session&);

	/* The next switching point after `pos` for the given quantization. Bars
	 * are counted from bar 1, so two-bar phrases start on odd bars.
	 */
	static Temporal::timepos_t next_boundary (Temporal::timepos_t const& pos, Quantize);

	/* Turn the cue markers in [start, end) into MIDI regions on the drum
	 * track, then isolate its slots from cues so nothing plays twice.
	 * Each region gets its own MIDI data, so notes can be edited one region
	 * at a time. Returns the number of regions created, or -1 on error.
	 */
	static int flatten (Session&, std::shared_ptr<MidiTrack>,
	                    Temporal::timepos_t const& start, Temporal::timepos_t const& end);

	/* Undo the isolation set by flatten (the regions stay). */
	static void follow_cues (std::shared_ptr<MidiTrack>, bool yn);

private:
	static void load_library ();
	static void play_timeline_too (std::shared_ptr<MidiTrack>);
};

/* Perform mode: live switching on the drum track, quantized, with fills that
 * return to the previous part. Lives in the GUI thread: call tick() often
 * (every 20-50 ms) while the transport rolls.
 *
 * Each switch is sent as a cue (Session::trigger_cue_row) in the last beat
 * before its boundary; the slots are quantized to one beat, so the new groove
 * starts exactly on the boundary. With Rec Cues armed, Ardour records the
 * switches as cue markers on the beat (cue-record-snap-to-beat).
 */
class LIBARDOUR_API DrumPerformer
{
public:
	DrumPerformer (Session&);

	void                 set_quantize (DrumStudio::Quantize q) { _quantize = q; }
	DrumStudio::Quantize quantize () const { return _quantize; }

	/* user asked for this cell; it starts at the next boundary */
	void request (int part_index, int complexity);
	/* call periodically from the GUI thread */
	void tick ();
	/* forget pending switches (e.g. on stop or locate) */
	void reset ();

	int playing_slot () const { return _playing; } /* -1 if none */
	int pending_slot () const { return _pending; } /* -1 if none */
	Temporal::timepos_t pending_at () const { return _pending_at; }

	/* arm Rec Cues and make it snap recorded switches to the beat */
	void set_recording (bool);
	bool recording () const;

private:
	Session&             _session;
	DrumStudio::Quantize _quantize;
	int                  _playing;
	int                  _pending;
	bool                 _sent;
	Temporal::timepos_t  _pending_at;
	int                  _before_fill; /* slot to return to after a fill */
	Temporal::timepos_t  _return_at;   /* when the running fill ends */

	void schedule (int slot, Temporal::timepos_t const& at);
};

} /* namespace ARDOUR */

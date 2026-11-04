/*
 * MusicDAWM Bass Studio (see ardour/bass_studio.h).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>

#include <glib/gstdio.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include "pbd/compose.h"
#include "pbd/error.h"
#include "pbd/stateful_diff_command.h"
#include "pbd/xml++.h"

#include "temporal/tempo.h"

#include "ardour/bass_studio.h"
#include "ardour/chan_count.h"
#include "ardour/dB.h"
#include "ardour/filesystem_paths.h"
#include "ardour/gain_control.h"
#include "ardour/gm_sound.h"
#include "ardour/location.h"
#include "ardour/midi_model.h"
#include "ardour/midi_region.h"
#include "ardour/midi_track.h"
#include "ardour/playlist.h"
#include "ardour/region_factory.h"
#include "ardour/session.h"
#include "ardour/smf_source.h"
#include "ardour/source_factory.h"
#include "ardour/triggerbox.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using namespace Temporal;
using std::string;
using std::vector;

static char const* const bass_node  = X_("BassStudio");
static char const* const line_name  = X_("Bass line");

/* ---- styles: one bar of 4 beats per complexity, "pos:length:degree" ----
 * degrees: R root (or the slash bass), 3, 5, 6, 7, 8 (octave), A (approach:
 * a semitone below the next chord's root, or above). Shorter bars (3/4)
 * use the events that fit. */

namespace {

struct StyleDef {
	char const* key;
	char const* name;
	char const* bar[4];
};

StyleDef const style_defs[] = {
	{ "root", N_("Root notes"),
	  { "0:4:R", "0:2:R 2:2:R", "0:2:R 2:2:5", "0:2:R 2:1.5:5 3.5:.5:A" } },
	{ "rock8", N_("Rock / pop eighths"),
	  { "0:1:R 1:1:R 2:1:R 3:1:R",
	    "0:.5:R .5:.5:R 1:.5:R 1.5:.5:R 2:.5:R 2.5:.5:R 3:.5:R 3.5:.5:R",
	    "0:.5:R .5:.5:R 1:.5:R 1.5:.5:R 2:.5:R 2.5:.5:R 3:.5:5 3.5:.5:8",
	    "0:.5:R .5:.5:R 1:.5:R 1.5:.5:8 2:.5:R 2.5:.5:R 3:.5:5 3.5:.5:A" } },
	{ "twofeel", N_("Country / two-feel"),
	  { "0:2:R 2:2:5", "0:1.5:R 1.5:.5:R 2:2:5", "0:1:R 1:1:3 2:1:5 3:1:3", "0:1:R 1:1:3 2:1:5 3:.5:6 3.5:.5:A" } },
	{ "octave", N_("Disco octaves"),
	  { "0:1:R 1:1:8 2:1:R 3:1:8",
	    "0:.5:R .5:.5:8 1:.5:R 1.5:.5:8 2:.5:R 2.5:.5:8 3:.5:R 3.5:.5:8",
	    "0:.5:R .5:.5:8 1:.5:R 1.5:.5:8 2:.5:R 2.5:.5:8 3:.5:R 3.5:.5:5",
	    "0:.5:R .5:.5:8 1:.5:R 1.5:.5:8 2:.5:R 2.5:.5:8 3:.5:5 3.5:.5:A" } },
	{ "walking", N_("Jazz walking"),
	  { "0:2:R 2:2:5", "0:1:R 1:1:5 2:1:R 3:1:5", "0:1:R 1:1:3 2:1:5 3:1:A", "0:1:R 1:1:3 2:1:5 3:.667:6 3.667:.333:A" } },
	{ "reggae", N_("Reggae one-drop"),
	  { "0:1.5:R 2:1:R", "0:1:R 1.5:.5:R 2:1:5", "0:.75:R 1:.5:8 1.5:.5:R 2:1:5 3.5:.5:A", "0:.5:R .5:.5:R 1.5:.5:8 2:.5:5 2.5:.5:R 3.5:.5:A" } },
	{ "funk", N_("Funk sixteenths"),
	  { "0:.75:R 1.5:.5:R 2.5:.5:R",
	    "0:.5:R .75:.25:R 1.5:.25:8 2:.5:R 3:.25:5 3.5:.5:R",
	    "0:.25:R .5:.25:8 .75:.25:R 1.5:.25:8 2:.25:R 2.5:.25:5 3:.25:R 3.5:.25:7 3.75:.25:8",
	    "0:.25:R .5:.25:8 .75:.25:R 1.5:.25:8 2:.25:R 2.5:.25:5 3:.25:R 3.5:.25:7 3.75:.25:A" } },
	{ "ballad", N_("Ballad"),
	  { "0:4:R", "0:3:R 3:1:5", "0:2:R 2:1:5 3:1:8", "0:1.5:R 1.5:.5:5 2:1:8 3:1:A" } },
	{ "shuffle", N_("Blues shuffle"),
	  { "0:1:R 1:1:R 2:1:R 3:1:R", "0:1:R 1:1:3 2:1:5 3:1:6",
	    "0:.667:R .667:.333:R 1:.667:3 1.667:.333:3 2:.667:5 2.667:.333:5 3:.667:6 3.667:.333:6",
	    "0:.667:R .667:.333:3 1:.667:5 1.667:.333:6 2:.667:7 2.667:.333:6 3:.667:5 3.667:.333:3" } },
	{ "bossa", N_("Bossa nova"),
	  { "0:2:R 2:2:5", "0:1.5:R 1.5:.5:5 2:1.5:5 3.5:.5:R", "0:1.5:R 1.5:.5:5 2:1.5:R 3.5:.5:5", "0:1.5:R 1.5:.5:5 2:1:8 3:.5:5 3.5:.5:A" } },
	{ "kick", N_("Follow the kick drum"),
	  { "0:4:R", "0:4:R", "0:4:R", "0:4:R" } }, /* the kicks of the drum track; this pattern without one */
};

struct Event {
	double pos, len;
	char   degree;
};

vector<Event>
parse_bar (char const* s)
{
	vector<Event>      v;
	std::istringstream in (s);
	string             tok;
	while (in >> tok) {
		double p = 0, l = 0;
		char   d = 'R';
		if (sscanf (tok.c_str (), "%lf:%lf:%c", &p, &l, &d) == 3) {
			v.push_back (Event { p, l, d });
		}
	}
	return v;
}

StyleDef const*
style_def (string const& key)
{
	for (auto const& s : style_defs) {
		if (key == s.key) {
			return &s;
		}
	}
	return 0;
}

int
pitch_class (char letter, char accidental)
{
	static int const pcs[] = { 9, 11, 0, 2, 4, 5, 7 }; /* A B C D E F G */
	int pc = pcs[toupper (letter) - 'A'];
	if (accidental == '#') {
		pc += 1;
	} else if (accidental == 'b') {
		pc += 11;
	}
	return pc % 12;
}

/* the bass register: the chord's bass note between E1 (28) and D#2 (39) */
int
low_note (int pc)
{
	return 28 + ((pc - 4 + 12) % 12);
}

} // namespace

vector<BassStudio::Style> const&
BassStudio::styles ()
{
	static vector<Style> s;
	if (s.empty ()) {
		for (auto const& d : style_defs) {
			s.push_back (Style { d.key, _(d.name) });
		}
	}
	return s;
}

BassStudio::Style const*
BassStudio::style (string const& key)
{
	for (auto const& s : styles ()) {
		if (s.key == key) {
			return &s;
		}
	}
	return 0;
}

bool
BassStudio::parse_chord (string const& token, Chord& c)
{
	if (token.empty () || toupper (token[0]) < 'A' || toupper (token[0]) > 'G') {
		return false;
	}
	size_t i   = 1;
	char   acc = 0;
	if (i < token.size () && (token[i] == '#' || token[i] == 'b')) {
		acc = token[i++];
	}
	c      = Chord ();
	c.text = token;
	c.root = pitch_class (token[0], acc);
	c.bass = c.root;

	string q = token.substr (i);
	size_t const slash = q.find ('/');
	if (slash != string::npos) {
		string const b = q.substr (slash + 1);
		q              = q.substr (0, slash);
		if (b.empty () || toupper (b[0]) < 'A' || toupper (b[0]) > 'G' || b.size () > 2 || (b.size () == 2 && b[1] != '#' && b[1] != 'b')) {
			return false;
		}
		c.bass = pitch_class (b[0], b.size () == 2 ? b[1] : 0);
	}

	/* the quality, longest names first */
	struct Q {
		char const* name;
		int         third, fifth, seventh;
	};
	static Q const qs[] = {
		{ "m7b5", 3, 6, 10 }, { "maj7", 4, 7, 11 }, { "maj9", 4, 7, 11 }, { "min7", 3, 7, 10 }, { "dim7", 3, 6, 9 }, { "sus2", 2, 7, 10 }, { "sus4", 5, 7, 10 },
		{ "add9", 4, 7, 10 }, { "min", 3, 7, 10 }, { "maj", 4, 7, 11 }, { "dim", 3, 6, 9 }, { "aug", 4, 8, 10 }, { "sus", 5, 7, 10 }, { "m7", 3, 7, 10 },
		{ "m6", 3, 7, 10 }, { "m9", 3, 7, 10 }, { "M7", 4, 7, 11 }, { "mM7", 3, 7, 11 }, { "-7", 3, 7, 10 }, { "ø", 3, 6, 10 }, { "°", 3, 6, 9 },
		{ "m", 3, 7, 10 }, { "-", 3, 7, 10 }, { "+", 4, 8, 10 }, { "o", 3, 6, 9 },
	};
	for (auto const& x : qs) {
		size_t const n = strlen (x.name);
		if (q.compare (0, n, x.name) == 0) {
			c.third   = x.third;
			c.fifth   = x.fifth;
			c.seventh = x.seventh;
			q         = q.substr (n);
			break;
		}
	}
	/* extensions that do not change the bass line */
	static char const* const ext[] = { "7", "9", "11", "13", "6", "add9", "add2", "b9", "#9", "#11", "b13", "5", "(", ")" };
	while (!q.empty ()) {
		bool found = false;
		for (auto const* e : ext) {
			size_t const n = strlen (e);
			if (q.compare (0, n, e) == 0) {
				q     = q.substr (n);
				found = true;
				break;
			}
		}
		if (!found) {
			return false;
		}
	}
	return true;
}

bool
BassStudio::parse_chart (string const& text, vector<Bar>& bars, string& error)
{
	bars.clear ();
	string t = text;
	for (char& ch : t) {
		if (ch == '|' || ch == '\n' || ch == '\r' || ch == '\t') {
			ch = ' ';
		}
	}
	/* comments: [Verse] */
	string clean;
	bool   in = false;
	for (char ch : t) {
		if (ch == '[') {
			in = true;
		} else if (ch == ']') {
			in = false;
			clean += ' ';
		} else if (!in) {
			clean += ch;
		}
	}
	std::istringstream ss (clean);
	string             tok;
	while (ss >> tok) {
		Bar b;
		if (tok == "%") {
			if (bars.empty ()) {
				error = _("\"%\" repeats the bar before, but there is none");
				return false;
			}
			bars.push_back (bars.back ());
			continue;
		}
		if (tok == "N.C." || tok == "NC" || tok == "x" || tok == "X") {
			bars.push_back (b);
			continue;
		}
		size_t start = 0;
		while (start <= tok.size ()) {
			size_t const comma = tok.find (',', start);
			string const one   = tok.substr (start, comma == string::npos ? string::npos : comma - start);
			Chord        c;
			if (!parse_chord (one, c)) {
				error = string_compose (_("\"%1\" is not a chord (bar %2)"), one.empty () ? tok : one, bars.size () + 1);
				return false;
			}
			b.chords.push_back (c);
			if (comma == string::npos) {
				break;
			}
			start = comma + 1;
		}
		bars.push_back (b);
	}
	if (bars.empty ()) {
		error = _("Type the chords first, one bar per chord, for example: C G Am F");
		return false;
	}
	return true;
}

vector<BassStudio::Note>
BassStudio::generate (vector<Bar> const& bars, Options const& opt, double bpb, vector<int> const& complexity_per_bar, vector<vector<double>> const& kicks_per_bar)
{
	vector<Note>    notes;
	StyleDef const* sd = style_def (opt.style);
	if (!sd) {
		sd = style_def ("rock8");
	}
	double const total = bars.size () * bpb;
	double const scale = opt.feel == DrumStudio::FeelHalf ? 2 : opt.feel == DrumStudio::FeelDouble ? 0.5 : 1;

	/* the chord at a time, and when the next chord starts */
	auto chord_at = [&] (double t, Chord const*& c, double& next_change) {
		c           = 0;
		next_change = total;
		int const b = std::min ((int)(t / bpb), (int)bars.size () - 1);
		if (b < 0 || bars[b].chords.empty ()) {
			next_change = (b + 1) * bpb;
			return;
		}
		double const sub = bpb / bars[b].chords.size ();
		int const    k   = std::min ((int)((t - b * bpb) / sub), (int)bars[b].chords.size () - 1);
		c                = &bars[b].chords[k];
		next_change      = b * bpb + (k + 1) * sub;
	};
	auto next_root = [&] (double after) -> int {
		for (double t = after; t < total; t += 0.25) {
			Chord const* c;
			double       nc;
			chord_at (t, c, nc);
			if (c) {
				return c->bass;
			}
		}
		return -1;
	};
	auto pitch = [&] (Chord const& c, char d, double t, double next) -> int {
		int const r = low_note (c.bass);
		int const k = low_note (c.root);
		switch (d) {
			case '3': return k + c.third;
			case '5': return k + c.fifth;
			case '6': return k + c.sixth;
			case '7': return k + c.seventh;
			case '8': return r + 12;
			case 'A': {
				int const n = next_root (next);
				if (n < 0) {
					return k + c.fifth;
				}
				int const target = low_note (n);
				/* from below, unless that is the note we are on */
				return (target - 1 != r) ? target - 1 : target + 1;
			}
			default: return r;
		}
	};
	auto velocity = [] (double pos) {
		if (pos < 1e-6) {
			return 100;
		}
		double const frac = pos - floor (pos);
		return frac < 1e-6 ? 90 : (fmod (pos * 2, 1) < 1e-6 ? 82 : 74);
	};

	for (size_t b = 0; b < bars.size (); ++b) {
		double const bar0 = b * bpb;
		int          cx   = opt.complexity;
		if (b < complexity_per_bar.size () && complexity_per_bar[b] > 0) {
			cx = complexity_per_bar[b];
		}
		cx = std::max (1, std::min (4, cx));

		/* "Follow the kick": a root on every kick of the bar */
		if (string (sd->key) == "kick" && b < kicks_per_bar.size () && !kicks_per_bar[b].empty ()) {
			vector<double> const& k = kicks_per_bar[b];
			for (size_t i = 0; i < k.size (); ++i) {
				double const t = bar0 + k[i];
				Chord const* c;
				double       nc;
				chord_at (t, c, nc);
				if (!c) {
					continue;
				}
				char d = 'R';
				if (cx >= 2 && i % 2 == 1 && fmod (k[i], 1) > 1e-6) {
					d = '8'; /* off-beat kicks an octave up */
				}
				if (cx >= 3 && i + 1 == k.size () && k.size () > 2) {
					d = '5';
				}
				if (cx >= 4 && i + 1 == k.size () && k.size () > 1) {
					d = 'A';
				}
				double const end = i + 1 < k.size () ? bar0 + k[i + 1] : bar0 + bpb;
				notes.push_back (Note { t, end - t, pitch (*c, d, t, nc), velocity (k[i]) });
			}
			continue;
		}

		/* the style's pattern, in cycles of the bar (two bars in half-time,
		 * half a bar in double-time) */
		vector<Event> const ev    = parse_bar (sd->bar[cx - 1]);
		double const        cycle = 4 * scale;
		/* half-time: one cycle covers two bars, so it starts on even bars only */
		if (scale > 1 && (b % 2) == 1) {
			continue;
		}
		for (double c0 = bar0; c0 < bar0 + (scale > 1 ? 2 * bpb : bpb) - 1e-9; c0 += cycle) {
			for (auto const& e : ev) {
				double const t = c0 + e.pos * scale;
				if (e.pos >= bpb - 1e-9 || t >= total - 1e-9 || t >= c0 + bpb * scale - 1e-9) {
					continue;
				}
				Chord const* c;
				double       nc;
				chord_at (t, c, nc);
				if (!c) {
					continue;
				}
				notes.push_back (Note { t, e.len * scale, pitch (*c, e.degree, t, nc), velocity (e.pos) });
			}
		}
	}

	/* legato but separated: a note ends a little before the next one, and
	 * at a bar without bass */
	std::sort (notes.begin (), notes.end (), [] (Note const& a, Note const& b) { return a.start < b.start; });
	for (size_t i = 0; i < notes.size (); ++i) {
		double end = notes[i].start + notes[i].length;
		if (i + 1 < notes.size ()) {
			end = std::min (end, notes[i + 1].start);
		}
		end = std::min (end, total);
		Chord const* c;
		double       nc;
		chord_at (notes[i].start, c, nc);
		/* do not hold over a bar without bass */
		int const nb = (int)(nc / bpb);
		if (nb < (int)bars.size () && bars[nb].chords.empty ()) {
			end = std::min (end, nc);
		}
		notes[i].length = std::max (0.05, (end - notes[i].start) * 0.92);
	}
	return notes;
}

bool
BassStudio::write_midi (string const& path, vector<Note> const& notes, double length_beats, int program, int bank)
{
	int const PPQ = 480;
	struct E {
		long    at;
		int     order;
		uint8_t b[3];
		int     n;
	};
	vector<E> ev;
	int       o = 0;
	if (bank > 0) {
		ev.push_back (E { 0, o++, { 0xb0, 0x00, (uint8_t)(bank & 0x7f) }, 3 });
		ev.push_back (E { 0, o++, { 0xb0, 0x20, 0x00 }, 3 });
	}
	if (program >= 0 && program < 128) {
		ev.push_back (E { 0, o++, { 0xc0, (uint8_t)program, 0 }, 2 });
	}
	for (auto const& n : notes) {
		long const a = std::lround (n.start * PPQ);
		long const z = std::max (a + 1, std::lround ((n.start + n.length) * PPQ));
		ev.push_back (E { a, o++, { 0x90, (uint8_t)std::max (0, std::min (127, n.pitch)), (uint8_t)std::max (1, std::min (127, n.velocity)) }, 3 });
		ev.push_back (E { z, o++, { 0x80, (uint8_t)std::max (0, std::min (127, n.pitch)), 0 }, 3 });
	}
	std::stable_sort (ev.begin (), ev.end (), [] (E const& x, E const& y) {
		if (x.at != y.at) {
			return x.at < y.at;
		}
		bool const xo = (x.b[0] & 0xf0) == 0x80, yo = (y.b[0] & 0xf0) == 0x80;
		if (xo != yo) {
			return xo; /* note-offs first */
		}
		return x.order < y.order;
	});
	vector<uint8_t> trk;
	auto varlen = [&trk] (uint32_t v) {
		uint8_t buf[5];
		int     n = 0;
		buf[n++]  = v & 0x7f;
		while (v >>= 7) {
			buf[n++] = 0x80 | (v & 0x7f);
		}
		while (n--) {
			trk.push_back (buf[n]);
		}
	};
	long last = 0;
	for (auto const& e : ev) {
		varlen ((uint32_t)(e.at - last));
		last = e.at;
		trk.insert (trk.end (), e.b, e.b + e.n);
	}
	long const eot = std::max (last, std::lround (length_beats * PPQ));
	varlen ((uint32_t)(eot - last));
	trk.insert (trk.end (), { 0xff, 0x2f, 0x00 });

	vector<uint8_t> out = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, (uint8_t)(PPQ >> 8), (uint8_t)(PPQ & 0xff), 'M', 'T', 'r', 'k' };
	uint32_t const  n   = trk.size ();
	out.insert (out.end (), { (uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n });
	out.insert (out.end (), trk.begin (), trk.end ());

	FILE* f = g_fopen (path.c_str (), "wb");
	if (!f) {
		return false;
	}
	bool const ok = fwrite (out.data (), 1, out.size (), f) == out.size ();
	fclose (f);
	return ok;
}

/* ---- in a session ---- */

bool
BassStudio::is_bass_track (std::shared_ptr<Route> r)
{
	return r && std::dynamic_pointer_cast<MidiTrack> (r) && r->extra_xml (bass_node);
}

std::shared_ptr<MidiTrack>
BassStudio::find_bass_track (Session& s)
{
	std::shared_ptr<RouteList const> rl (s.get_routes ());
	for (auto const& r : *rl) {
		if (is_bass_track (r)) {
			return std::dynamic_pointer_cast<MidiTrack> (r);
		}
	}
	return std::shared_ptr<MidiTrack> ();
}

std::shared_ptr<MidiTrack>
BassStudio::create_bass_track (Session& s)
{
	std::shared_ptr<PluginInfo> synth = GMSound::synth ();
	std::list<std::shared_ptr<MidiTrack>> tl = s.new_midi_track (
	    ChanCount (DataType::MIDI, 1), ChanCount (DataType::MIDI, 1), false,
	    synth, 0, std::shared_ptr<RouteGroup> (), 1, _("Bass"),
	    PresentationInfo::max_order, Normal, true);
	if (tl.empty ()) {
		return std::shared_ptr<MidiTrack> ();
	}
	std::shared_ptr<MidiTrack> t = tl.front ();
	GMSound::load_soundfont (t);
	t->gain_control ()->set_value (dB_to_coefficient (-3.f), PBD::Controllable::NoGroup);
	t->extra_xml (bass_node, true)->set_property (X_("version"), 1);
	return t;
}

void
BassStudio::drum_parts (Session& s, int start_bar, int n_bars, vector<int>& cx, vector<vector<double>>& kicks)
{
	cx.assign (n_bars, 0);
	kicks.assign (n_bars, vector<double> ());
	std::shared_ptr<MidiTrack> drums = DrumStudio::find_drum_track (s);
	if (!drums || n_bars <= 0) {
		return;
	}
	TempoMap::SharedPtr tmap (TempoMap::use ());
	auto bar_start = [&] (int b) { return tmap->quarters_at (BBT_Argument (BBT_Time (start_bar + b, 1, 0))); };
	Beats const chart0 = bar_start (0);

	/* the drum cells, from the cue markers (in time order) */
	vector<std::pair<Beats, int>> cues;
	for (auto const& l : s.locations ()->list ()) {
		if (l->is_cue_marker () && l->cue_id () >= 0 && l->cue_id () < DrumStudio::slot_count ()) {
			cues.push_back (std::make_pair (l->start ().beats (), l->cue_id ()));
		}
	}
	std::sort (cues.begin (), cues.end ());
	int prev_cx = 0;
	for (int b = 0; b < n_bars; ++b) {
		Beats const t = bar_start (b);
		int         slot = -1;
		for (auto const& c : cues) {
			if (c.first <= t) {
				slot = c.second;
			}
		}
		if (slot >= 0) {
			int p, c;
			DrumStudio::cell_of_slot (slot, p, c);
			if (DrumStudio::part_is_fill (p)) {
				c = prev_cx > 0 ? prev_cx : c;
			} else if (DrumStudio::parts ()[p].key == X_("ending")) {
				c = 1;
			}
			cx[b]   = c;
			prev_cx = c;
		}
	}

	/* the kicks: from the drum track's regions (a flattened performance),
	 * else from the grooves the cue markers play */
	auto add_kick = [&] (Beats const& at) {
		for (int b = 0; b < n_bars; ++b) {
			Beats const b0 = bar_start (b), b1 = bar_start (b + 1);
			if (at >= b0 && at < b1) {
				kicks[b].push_back ((at - b0).to_ticks () / double (Temporal::ticks_per_beat));
				return;
			}
		}
	};
	std::shared_ptr<RegionList> const rl = drums->playlist ()->region_list ();
	bool any = false;
	for (auto const& r : *rl) {
		std::shared_ptr<MidiRegion> mr = std::dynamic_pointer_cast<MidiRegion> (r);
		if (!mr || mr->muted ()) {
			continue;
		}
		Beats const r0 = mr->position ().beats () - mr->start ().beats ();
		Beats const re = (mr->position () + mr->length ()).beats ();
		for (auto const& n : mr->model ()->notes ()) {
			if (n->note () != 35 && n->note () != 36) {
				continue;
			}
			Beats const at = r0 + n->time ();
			if (at >= mr->position ().beats () && at < re) {
				add_kick (at);
				any = true;
			}
		}
	}
	if (!any) {
		std::shared_ptr<TriggerBox> tb = drums->triggerbox ();
		Beats const                 end = bar_start (n_bars);
		for (size_t i = 0; i < cues.size () && tb; ++i) {
			std::shared_ptr<Trigger>    tr   = tb->trigger (cues[i].second);
			std::shared_ptr<MidiRegion> clip = tr ? std::dynamic_pointer_cast<MidiRegion> (tr->the_region ()) : std::shared_ptr<MidiRegion> ();
			if (!clip) {
				continue;
			}
			Beats const len = clip->length ().beats ();
			if (len == Beats ()) {
				continue;
			}
			int p, c;
			DrumStudio::cell_of_slot (cues[i].second, p, c);
			Beats const seg0 = cues[i].first;
			Beats const seg1 = i + 1 < cues.size () ? cues[i + 1].first : (DrumStudio::part_loops (p) ? end : seg0 + len);
			for (Beats c0 = seg0; c0 < seg1 && c0 < end; c0 += len) {
				for (auto const& n : clip->model ()->notes ()) {
					if ((n->note () == 35 || n->note () == 36) && n->time () < len && c0 + n->time () < seg1 && c0 + n->time () >= chart0) {
						add_kick (c0 + n->time ());
					}
				}
			}
		}
	}
	for (auto& k : kicks) {
		std::sort (k.begin (), k.end ());
		k.erase (std::unique (k.begin (), k.end (), [] (double a, double b) { return std::fabs (a - b) < 0.05; }), k.end ());
	}
}

namespace {

double
quarters_per_bar (int start_bar)
{
	TempoMap::SharedPtr tmap (TempoMap::use ());
	Beats const         a = tmap->quarters_at (BBT_Argument (BBT_Time (start_bar, 1, 0)));
	Beats const         b = tmap->quarters_at (BBT_Argument (BBT_Time (start_bar + 1, 1, 0)));
	return (b - a).to_ticks () / double (Temporal::ticks_per_beat);
}

string
line_file (string const& tag)
{
	string const dir = Glib::build_filename (user_cache_directory (), X_("bassstudio"));
	g_mkdir_with_parents (dir.c_str (), 0755);
	return Glib::build_filename (dir, tag + ".mid");
}

std::shared_ptr<MidiRegion>
region_from_file (Session& s, string const& path, string const& name)
{
	try {
		std::shared_ptr<SMFSource> ms = std::dynamic_pointer_cast<SMFSource> (
		    SourceFactory::createExternal (DataType::MIDI, s, path, 0, Source::Flag (0), false));
		if (!ms) {
			return std::shared_ptr<MidiRegion> ();
		}
		PBD::PropertyList plist;
		plist.add (Properties::start, timepos_t (Beats ()));
		plist.add (Properties::length, ms->length ());
		plist.add (Properties::name, name);
		plist.add (Properties::layer, 0);
		return std::dynamic_pointer_cast<MidiRegion> (RegionFactory::create (std::dynamic_pointer_cast<Source> (ms), plist, false));
	} catch (...) {
		return std::shared_ptr<MidiRegion> ();
	}
}

} // namespace

void
BassStudio::saved (Session& s, string& chart, int& start_bar, Options& opt)
{
	chart     = string ();
	start_bar = 1;
	opt       = Options ();
	XMLNode* n = s.extra_xml (bass_node);
	if (!n) {
		return;
	}
	n->get_property (X_("chart"), chart);
	n->get_property (X_("start-bar"), start_bar);
	n->get_property (X_("style"), opt.style);
	n->get_property (X_("complexity"), opt.complexity);
	int f = 0;
	n->get_property (X_("feel"), f);
	opt.feel = (f == DrumStudio::FeelHalf || f == DrumStudio::FeelDouble) ? (DrumStudio::Feel)f : DrumStudio::FeelNormal;
	n->get_property (X_("program"), opt.program);
	n->get_property (X_("bank"), opt.bank);
	n->get_property (X_("follow-drums"), opt.follow_drums);
}

std::shared_ptr<Region>
BassStudio::preview_region (Session& s, string const& chart, int start_bar, Options const& opt, int n, string& error)
{
	vector<Bar> bars;
	if (!parse_chart (chart, bars, error)) {
		return std::shared_ptr<Region> ();
	}
	if ((int)bars.size () > n) {
		bars.resize (n);
	}
	double const           bpb = quarters_per_bar (start_bar);
	vector<int>            cx;
	vector<vector<double>> kicks;
	if (opt.follow_drums || opt.style == "kick") {
		drum_parts (s, start_bar, bars.size (), cx, kicks);
		if (!opt.follow_drums) {
			cx.clear ();
		}
	}
	vector<Note> const notes = generate (bars, opt, bpb, cx, kicks);
	string const       path  = line_file (X_("preview"));
	if (!write_midi (path, notes, bars.size () * bpb, opt.program, opt.bank)) {
		error = _("The bass line could not be written.");
		return std::shared_ptr<Region> ();
	}
	std::shared_ptr<MidiRegion> r = region_from_file (s, path, _("Bass preview"));
	if (r) {
		r->set_position (timepos_t (Beats ()));
	}
	return r;
}

int
BassStudio::make_bass_line (Session& s, string const& chart, int start_bar, Options const& opt, string& error)
{
	vector<Bar> bars;
	if (!parse_chart (chart, bars, error)) {
		return -1;
	}
	if (!GMSound::available ()) {
		error = _("The General MIDI soundfont or ACE Fluid Synth is missing.");
		return -1;
	}
	start_bar = std::max (1, start_bar);

	double const           bpb = quarters_per_bar (start_bar);
	vector<int>            cx;
	vector<vector<double>> kicks;
	drum_parts (s, start_bar, bars.size (), cx, kicks);
	if (!opt.follow_drums) {
		cx.clear ();
	}
	vector<Note> const notes = generate (bars, opt, bpb, cx, kicks);

	/* a file per line, so that a remake does not overwrite the source of
	 * the region it replaces (undo brings that one back) */
	string const path = line_file (string_compose (X_("line-%1-%2"), (long)time (0), g_random_int ()));
	if (!write_midi (path, notes, bars.size () * bpb, opt.program, opt.bank)) {
		error = _("The bass line could not be written.");
		return -1;
	}
	std::shared_ptr<MidiRegion> tmpl = region_from_file (s, path, line_name);
	if (!tmpl) {
		error = _("The bass line could not be read back.");
		return -1;
	}

	std::shared_ptr<MidiTrack> t = find_bass_track (s);
	if (!t) {
		t = create_bass_track (s);
	}
	if (!t || !t->playlist ()) {
		error = _("The bass track could not be created.");
		return -1;
	}

	TempoMap::SharedPtr tmap (TempoMap::use ());
	timepos_t const     at (tmap->quarters_at (BBT_Argument (BBT_Time (start_bar, 1, 0))));

	std::shared_ptr<Playlist> pl = t->playlist ();
	s.begin_reversible_command (_("make bass line"));
	pl->clear_changes ();
	/* the line made before goes; the user's own regions stay */
	{
		std::shared_ptr<RegionList> const rl = pl->region_list ();
		for (auto const& r : *rl) {
			if (r->name ().compare (0, strlen (line_name), line_name) == 0) {
				pl->remove_region (r);
			}
		}
	}
	/* fork: the line gets its own MIDI data in the session */
	std::shared_ptr<Region> r = RegionFactory::create (std::shared_ptr<Region const> (tmpl), true, true);
	if (r) {
		r->set_name (line_name);
		r->set_length (timecnt_t (Beats::from_double (bars.size () * bpb), at));
		pl->add_region (r, at);
	}
	s.add_command (new PBD::StatefulDiffCommand (pl));
	s.commit_reversible_command ();

	XMLNode* n = s.extra_xml (bass_node, true);
	n->set_property (X_("chart"), chart);
	n->set_property (X_("start-bar"), start_bar);
	n->set_property (X_("style"), opt.style);
	n->set_property (X_("complexity"), opt.complexity);
	n->set_property (X_("feel"), (int)opt.feel);
	n->set_property (X_("program"), opt.program);
	n->set_property (X_("bank"), opt.bank);
	n->set_property (X_("follow-drums"), opt.follow_drums);
	s.set_dirty ();
	return r ? (int)notes.size () : -1;
}

/*
 * MusicDAWM: Waveform / Tracktion project import (see waveform_import.h).
 *
 * The .tracktionedit is a JUCE ValueTree, stored as XML (Waveform 11) or as
 * JUCE's binary ValueTree stream; both are read. Source references of the
 * form "<projectID>/<itemID>" (hex) are resolved through the .tracktion
 * project index ("TP01"), which sits next to the edit or one folder up.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <algorithm>
#include <cmath>
#include <functional>
#include <cstring>
#include <map>
#include <set>
#include <sstream>

#include <archive.h>
#include <archive_entry.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include "pbd/basename.h"
#include "pbd/compose.h"
#include "pbd/convert.h"
#include "pbd/file_utils.h"
#include "pbd/string_convert.h"
#include "pbd/xml++.h"

#include "ardour/audiofilesource.h"
#include "ardour/waveform_import.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using namespace PBD;
using std::string;
using std::vector;

namespace {

/* ---- reading bytes ------------------------------------------------------ */

bool
read_file (string const& path, string& data)
{
	gchar* buf = 0;
	gsize  len = 0;
	if (!g_file_get_contents (path.c_str (), &buf, &len, 0)) {
		return false;
	}
	data.assign (buf, len);
	g_free (buf);
	return true;
}

struct Reader {
	string const& d;
	size_t        pos;
	bool          bad;

	Reader (string const& data, size_t p = 0) : d (data), pos (p), bad (false) {}

	bool have (size_t n) { if (pos + n > d.size ()) { bad = true; } return !bad; }
	uint8_t byte () { return have (1) ? (uint8_t) d[pos++] : 0; }
	int32_t int32 ()
	{
		if (!have (4)) return 0;
		uint32_t v = 0;
		for (int i = 0; i < 4; ++i) v |= (uint32_t) (uint8_t) d[pos + i] << (8 * i);
		pos += 4;
		return (int32_t) v;
	}
	int64_t int64 ()
	{
		if (!have (8)) return 0;
		uint64_t v = 0;
		for (int i = 0; i < 8; ++i) v |= (uint64_t) (uint8_t) d[pos + i] << (8 * i);
		pos += 8;
		return (int64_t) v;
	}
	double dbl ()
	{
		int64_t const i = int64 ();
		double        v;
		memcpy (&v, &i, 8);
		return v;
	}
	string cstring () /* NUL-terminated UTF-8 */
	{
		size_t const e = d.find ('\0', pos);
		if (e == string::npos) { bad = true; return string (); }
		string s = d.substr (pos, e - pos);
		pos      = e + 1;
		return s;
	}
	string raw (size_t n) { if (!have (n)) return string (); string s = d.substr (pos, n); pos += n; return s; }
	/* JUCE OutputStream::writeCompressedInt */
	int compressed ()
	{
		uint8_t const size = byte ();
		int const     n    = size & 0x7f;
		if (n > 4) { bad = true; return 0; }
		uint32_t v = 0;
		for (int i = 0; i < n; ++i) v |= (uint32_t) byte () << (8 * i);
		return (size & 0x80) ? -(int) v : (int) v;
	}
};

/* ---- the .tracktion project index ("TP01") ------------------------------ */

struct ProjectIndex {
	string                   dir;   /* folder of the .tracktion file */
	std::map<int, string>    files; /* item id -> file, relative to dir */
	std::map<int, double>    lengths;
};

bool
read_project_index (string const& path, uint32_t& project_id, ProjectIndex& idx)
{
	string data;
	if (!read_file (path, data) || data.compare (0, 4, "TP01") != 0) {
		return false;
	}
	Reader r (data, 4);
	project_id                = (uint32_t) r.int32 ();
	int32_t const object_off  = r.int32 ();
	r.int32 (); /* search index, not needed */
	int32_t const nprops = r.int32 ();
	for (int i = 0; i < nprops && !r.bad; ++i) {
		r.cstring ();
		r.raw ((size_t) std::max (0, r.int32 ()));
	}
	r.pos = (size_t) object_off;
	int32_t const nitems = r.int32 ();
	for (int i = 0; i < nitems && !r.bad; ++i) {
		int32_t const id  = r.int32 ();
		Reader        ir (data, (size_t) r.int32 ());
		ir.cstring (); /* name */
		ir.cstring (); /* type */
		ir.cstring (); /* description */
		string const file = ir.cstring ();
		double const len  = ir.dbl ();
		if (!ir.bad) {
			idx.files[id]   = file;
			idx.lengths[id] = len;
		}
	}
	idx.dir = Glib::path_get_dirname (path);
	return !r.bad;
}

/* ---- JUCE binary ValueTree -> XMLNode ----------------------------------- */

string
var_to_string (Reader& r)
{
	int const n = r.compressed ();
	if (n <= 0 || r.bad) {
		return string ();
	}
	size_t const end  = r.pos + (size_t) n;
	uint8_t const t   = r.byte ();
	string        out;
	char          buf[64];
	switch (t) {
		case 1: /* int */
			out = PBD::to_string (r.int32 ());
			break;
		case 2:
			out = "1";
			break;
		case 3:
			out = "0";
			break;
		case 4: /* double */
			g_ascii_dtostr (buf, sizeof (buf), r.dbl ());
			out = buf;
			break;
		case 5: /* string, with its terminating NUL */
			out = r.raw ((size_t) n - 1);
			if (!out.empty () && out.back () == '\0') {
				out.pop_back ();
			}
			break;
		case 6: /* int64 */
			out = PBD::to_string (r.int64 ());
			break;
		default: /* arrays, binary blobs, ...: not needed here */
			break;
	}
	r.pos = end;
	return out;
}

XMLNode*
read_value_tree (Reader& r, int depth)
{
	string const type = r.cstring ();
	if (type.empty () || r.bad || depth > 64) {
		return 0;
	}
	XMLNode*  node   = new XMLNode (type);
	int const nprops = r.compressed ();
	for (int i = 0; i < nprops && !r.bad; ++i) {
		string const name = r.cstring ();
		node->set_property (name.c_str (), var_to_string (r));
	}
	int const nchildren = r.compressed ();
	for (int i = 0; i < nchildren && !r.bad; ++i) {
		XMLNode* c = read_value_tree (r, depth + 1);
		if (!c) {
			break;
		}
		node->add_child_nocopy (*c);
	}
	return node;
}

/* ---- helpers ------------------------------------------------------------ */

string
attr (XMLNode const* n, char const* name, string const& dflt = string ())
{
	XMLProperty const* p = n ? n->property (name) : 0;
	return p ? p->value () : dflt;
}

double
num (XMLNode const* n, char const* name, double dflt = 0)
{
	XMLProperty const* p = n ? n->property (name) : 0;
	return p ? g_ascii_strtod (p->value ().c_str (), 0) : dflt;
}

string
fmt (double v)
{
	char buf[G_ASCII_DTOSTR_BUF_SIZE];
	g_ascii_formatd (buf, sizeof (buf), "%.9g", v);
	return buf;
}

string
esc (string const& s)
{
	string o;
	for (char c : s) {
		switch (c) {
			case '&': o += "&amp;"; break;
			case '<': o += "&lt;"; break;
			case '>': o += "&gt;"; break;
			case '"': o += "&quot;"; break;
			default: o += c;
		}
	}
	return o;
}

/* Waveform's fader position (0..1, 0 dB at exp(-6/20)) in dB */
double
fader_db (XMLNode const* plugin)
{
	double const pos = plugin ? num (plugin, "volume", exp (-6.0 / 20.0)) : exp (-6.0 / 20.0);
	return pos > 0 ? 20.0 * log (pos) + 6.0 : -100.0;
}

XMLNode const*
volume_plugin (XMLNode const* parent)
{
	for (auto const* c : parent->children ()) {
		if (c->name () == "PLUGIN" && attr (c, "type") == "volume") {
			return c;
		}
	}
	return 0;
}

/* piecewise-constant tempo map from TEMPOSEQUENCE, for seconds <-> beats */
struct TempoMapLite {
	vector<std::pair<double, double>> pts; /* (start beat, bpm) */

	double seconds_to_beats (double s) const
	{
		double beat = 0, sec = 0;
		for (size_t i = 0; i < pts.size (); ++i) {
			double const bpm = pts[i].second;
			if (i + 1 < pts.size ()) {
				double const seg = (pts[i + 1].first - pts[i].first) * 60.0 / bpm;
				if (s < sec + seg) {
					return pts[i].first + (s - sec) * bpm / 60.0;
				}
				sec += seg;
				beat = pts[i + 1].first;
			} else {
				return pts[i].first + (s - sec) * bpm / 60.0;
			}
		}
		return beat + s * 2; /* no tempo: 120 BPM */
	}
	double bpm_at (double s) const
	{
		double const b   = seconds_to_beats (s);
		double       bpm = pts.empty () ? 120 : pts.front ().second;
		for (auto const& p : pts) {
			if (p.first <= b) {
				bpm = p.second;
			}
		}
		return bpm;
	}
	double beats_to_seconds (double b) const
	{
		double sec = 0;
		for (size_t i = 0; i < pts.size (); ++i) {
			double const bpm = pts[i].second;
			double const end = i + 1 < pts.size () ? pts[i + 1].first : 1e18;
			if (b <= end) {
				return sec + (b - pts[i].first) * 60.0 / bpm;
			}
			sec += (end - pts[i].first) * 60.0 / bpm;
		}
		return b * 0.5;
	}
};

/* ---- the converter ------------------------------------------------------ */

class Converter
{
public:
	Converter (vector<string>& notes) : _notes (notes), _ids (0) {}

	bool load (string const& edit);
	string project_xml (bool embed); /* embed: paths "audio/<name>" for a .dawproject */

	std::map<string, string> const& embedded () const { return _embedded; } /* archive path -> file */

private:
	vector<string>&           _notes;
	std::set<string>          _said;
	string                    _edit_dir;
	XMLTree                   _tree;
	XMLNode const*            _edit = 0;
	std::map<uint32_t, ProjectIndex> _projects;
	TempoMapLite              _tempo;
	int                       _ids;
	std::map<string, string>  _embedded;
	std::map<string, string>  _embedded_of; /* file -> archive path */

	void note (string const& s) { if (_said.insert (s).second) _notes.push_back (s); }
	string id () { return string_compose ("w%1", ++_ids); }
	string resolve (string const& source);
	string file_ref (string const& file, bool embed);
	void   tracks (XMLNode const* parent, std::ostringstream& st, std::ostringstream& arr, bool embed, int& n, int depth);
};

bool
Converter::load (string const& edit)
{
	_edit_dir = Glib::path_get_dirname (edit);

	string data;
	if (!read_file (edit, data)) {
		note (_("The edit file could not be read."));
		return false;
	}
	size_t first = data.find_first_not_of (" \t\r\n\xef\xbb\xbf");
	if (first != string::npos && data[first] == '<') {
		if (!_tree.read_buffer (data.c_str ())) {
			note (_("The edit file is not valid XML."));
			return false;
		}
	} else if (data.size () > 2 && (uint8_t) data[0] == 0x1f && (uint8_t) data[1] == 0x8b) {
		note (_("Compressed edit files are not supported; in Waveform, save the edit again (or use \"Save as\" with a new name)."));
		return false;
	} else {
		Reader   r (data);
		XMLNode* root = read_value_tree (r, 0);
		if (!root) {
			note (_("The edit file is neither XML nor a Tracktion binary edit."));
			return false;
		}
		_tree.set_root (root);
	}
	_edit = _tree.root ();
	if (!_edit || _edit->name () != "EDIT") {
		note (_("This is not a Waveform edit (no EDIT element)."));
		return false;
	}

	/* project indexes next to the edit and one folder up */
	for (string const& dir : { _edit_dir, Glib::path_get_dirname (_edit_dir) }) {
		vector<string> found;
		PBD::find_files_matching_pattern (found, dir, "*.tracktion");
		for (auto const& f : found) {
			uint32_t     pid;
			ProjectIndex idx;
			if (read_project_index (f, pid, idx) && _projects.find (pid) == _projects.end ()) {
				_projects[pid] = idx;
			}
		}
	}

	/* tempo */
	if (XMLNode const* ts = _edit->child ("TEMPOSEQUENCE")) {
		for (auto const* c : ts->children ()) {
			if (c->name () == "TEMPO") {
				_tempo.pts.push_back (std::make_pair (num (c, "startBeat"), num (c, "bpm", 120)));
				if (num (c, "curve", 1) != 1 && _tempo.pts.size () > 1) {
					note (_("Tempo curves were imported as steps."));
				}
			}
		}
	}
	if (_tempo.pts.empty ()) {
		_tempo.pts.push_back (std::make_pair (0.0, 120.0));
	}
	return true;
}

/* a clip's media file, as an absolute path ("" if not found) */
string
Converter::resolve (string const& source)
{
	/* "<project id>/<item id>", both hex */
	size_t const slash = source.find ('/');
	if (slash != string::npos && slash > 0 && source.find_first_not_of ("0123456789abcdefABCDEF/") == string::npos) {
		uint32_t const pid = (uint32_t) strtoul (source.substr (0, slash).c_str (), 0, 16);
		int const      iid = (int) strtol (source.substr (slash + 1).c_str (), 0, 16);
		auto           p   = _projects.find (pid);
		if (p != _projects.end ()) {
			auto i = p->second.files.find (iid);
			if (i != p->second.files.end ()) {
				string f = i->second;
				std::replace (f.begin (), f.end (), '\\', '/');
				string const full = Glib::path_is_absolute (f) ? f : Glib::build_filename (p->second.dir, f);
				if (Glib::file_test (full, Glib::FILE_TEST_EXISTS)) {
					return full;
				}
				/* moved projects: look for the file by name */
				vector<string> found;
				PBD::find_files_matching_pattern (found, p->second.dir, Glib::path_get_basename (f));
				if (!found.empty ()) {
					return found.front ();
				}
			}
		} else {
			note (_("The Waveform project file (.tracktion) was not found next to the edit; clips that refer to it are missing."));
		}
		return string ();
	}
	/* a file path */
	string f = source;
	std::replace (f.begin (), f.end (), '\\', '/');
	string const full = Glib::path_is_absolute (f) ? f : Glib::build_filename (_edit_dir, f);
	return Glib::file_test (full, Glib::FILE_TEST_EXISTS) ? full : string ();
}

string
Converter::file_ref (string const& file, bool embed)
{
	if (!embed) {
		return string_compose ("<File path=\"%1\" external=\"true\"/>", esc (file));
	}
	auto i = _embedded_of.find (file);
	if (i == _embedded_of.end ()) {
		string name = "audio/" + Glib::path_get_basename (file);
		for (int n = 2; _embedded.find (name) != _embedded.end (); ++n) {
			name = string_compose ("audio/%1-%2", n, Glib::path_get_basename (file));
		}
		_embedded[name]    = file;
		i                  = _embedded_of.insert (std::make_pair (file, name)).first;
	}
	return string_compose ("<File path=\"%1\"/>", esc (i->second));
}

void
Converter::tracks (XMLNode const* parent, std::ostringstream& st, std::ostringstream& arr, bool embed, int& n, int depth)
{
	for (auto const* t : parent->children ()) {
		string const tag = t->name ();
		if (tag == "FOLDERTRACK") {
			st << string_compose ("<Track contentType=\"tracks\" id=\"%1\" name=\"%2\">\n", id (), esc (attr (t, "name", _("Folder"))));
			tracks (t, st, arr, embed, n, depth + 1);
			st << "</Track>\n";
			if (volume_plugin (t)) {
				note (_("Folder track volume was not imported (folders are flattened)."));
			}
			continue;
		}
		if (tag == "MARKERTRACK") {
			continue; /* its markers are collected in project_xml () */
		}
		if (tag == "CHORDTRACK" || tag == "ARRANGERTRACK" || tag == "AUTOMATIONTRACK") {
			note (string_compose (_("%1 was not imported."), tag == "CHORDTRACK" ? _("The chord track") : tag == "ARRANGERTRACK" ? _("The arranger track") : _("An automation track")));
			continue;
		}
		if (tag != "TRACK" && tag != "AUDIOTRACK") {
			continue;
		}
		++n;

		bool has_audio = false, has_midi = false;
		string first_file;
		for (auto const* c : t->children ()) {
			if (c->name () == "AUDIOCLIP" || c->name () == "WAVECLIP") {
				has_audio = true;
				if (first_file.empty ()) {
					first_file = resolve (attr (c, "source"));
				}
			} else if (c->name () == "MIDICLIP") {
				has_midi = true;
			} else if (c->name () == "STEPCLIP" || c->name () == "EDITCLIP" || c->name () == "CHORDCLIP") {
				note (string_compose (_("A %1 was not imported."), c->name ()));
			}
		}
		bool const midi = has_midi && !has_audio;
		if (has_midi && has_audio) {
			note (_("A track had both audio and MIDI clips; its MIDI clips were not imported."));
		}

		/* Waveform does not store default track names ("Track 1"...) */
		string name = attr (t, "name");
		if (name.empty ()) {
			name = first_file.empty () ? string_compose (_("Track %1"), n) : string (PBD::basename_nosuffix (first_file));
		}

		/* a mono track for mono recordings (e.g. a Zoom R8's files) */
		int channels = 2;
		if (!first_file.empty ()) {
			SoundFileInfo info;
			string        err;
			if (AudioFileSource::get_soundfile_info (first_file, info, err)) {
				channels = std::min (2, std::max (1, (int) info.channels));
			}
		}

		/* plug-ins go into the DAWproject as devices (not loaded, but listed) */
		std::ostringstream devs;
		for (auto const* p : t->children ()) {
			if (p->name () != "PLUGIN") {
				continue;
			}
			string const type = attr (p, "type");
			if (type == "volume" || type == "level" || type == "levelMeter" || type == "meter") {
				continue;
			}
			if (type == "auxsend") {
				note (string_compose (_("\"%1\": an aux send was not imported; add a send to a bus by hand."), name));
				continue;
			}
			string const file  = attr (p, "filename");
			string const pname = attr (p, "name", file.empty () ? type : file);
			string const lower = PBD::downcase (file);
			char const*  elem  = lower.find (".vst3") != string::npos ? "Vst3Plugin" : (lower.find (".dll") != string::npos || lower.find (".vst") != string::npos) ? "Vst2Plugin" : "Device";
			devs << string_compose ("<%1 deviceName=\"%2\" deviceRole=\"audioFX\" loaded=\"false\" id=\"%3\"/>\n", elem, esc (pname), id ());
		}

		string const tid = id (), cid = id ();
		XMLNode const* vol = volume_plugin (t);
		double const   pan = vol ? num (vol, "pan", 0) : 0;
		st << string_compose ("<Track contentType=\"%1\" id=\"%2\" name=\"%3\">\n", midi ? "notes" : "audio", tid, esc (name))
		   << string_compose ("<Channel audioChannels=\"%3\" role=\"regular\" destination=\"wmaster\" solo=\"%1\" id=\"%2\">\n", attr (t, "solo") == "1" ? "true" : "false", cid, midi ? 2 : channels)
		   << (devs.str ().empty () ? string () : "<Devices>\n" + devs.str () + "</Devices>\n")
		   << string_compose ("<Mute value=\"%1\" id=\"%2\"/>\n", attr (t, "mute") == "1" ? "true" : "false", id ())
		   << string_compose ("<Pan unit=\"normalized\" min=\"0\" max=\"1\" value=\"%1\" id=\"%2\"/>\n", fmt (std::min (1.0, std::max (0.0, (pan + 1.0) / 2.0))), id ())
		   << string_compose ("<Volume unit=\"decibel\" value=\"%1\" id=\"%2\"/>\n", fmt (fader_db (vol)), id ())
		   << "</Channel>\n</Track>\n";

		arr << string_compose ("<Lanes track=\"%1\" timeUnit=\"seconds\" id=\"%2\">\n<Clips id=\"%3\">\n", tid, id (), id ());
		for (auto const* c : t->children ()) {
			string const ctag = c->name ();
			double const start = num (c, "start"), length = num (c, "length"), offset = num (c, "offset");
			if (length <= 0) {
				continue;
			}
			string const cname = attr (c, "name");
			bool const   off   = attr (c, "mute") == "1" || attr (c, "disabled") == "1";

			if ((ctag == "AUDIOCLIP" || ctag == "WAVECLIP") && !midi) {
				string const file = resolve (attr (c, "source"));
				if (file.empty ()) {
					note (string_compose (_("Audio of clip \"%1\" was not found (%2)."), cname, attr (c, "source")));
					continue;
				}
				double const speed = num (c, "speedRatio", 1.0);
				if (fabs (speed - 1.0) > 1e-6) {
					note (string_compose (_("Clip \"%1\" plays at %2%% speed in Waveform; it was imported at its original speed."), cname, (int) lrint (speed * 100)));
				}
				if (num (c, "loopLength", 0) > 0 || num (c, "loopLengthBeats", 0) > 0) {
					note (string_compose (_("Clip \"%1\" is looped in Waveform; it was imported without the loop."), cname));
				}
				if (fabs (num (c, "pan", 0)) > 1e-6) {
					note (string_compose (_("Clip pan of \"%1\" was not imported."), cname));
				}
				arr << string_compose ("<Clip time=\"%1\" duration=\"%2\" contentTimeUnit=\"seconds\" playStart=\"%3\" name=\"%4\"%5",
				                       fmt (start), fmt (length), fmt (offset), esc (cname), off ? " enable=\"false\"" : "");
				double const fin = num (c, "fadeIn", 0), fout = num (c, "fadeOut", 0);
				if (fin > 0 || fout > 0) {
					arr << string_compose (" fadeTimeUnit=\"seconds\" fadeInTime=\"%1\" fadeOutTime=\"%2\"", fmt (fin), fmt (fout));
				}
				arr << ">\n<Lanes timeUnit=\"seconds\" id=\"" << id () << "\">\n"
				    << string_compose ("<Audio channels=\"2\" sampleRate=\"0\" duration=\"0\" id=\"%1\">", id ()) << file_ref (file, embed) << "</Audio>\n";
				double const gain = num (c, "gain", 0);
				if (fabs (gain) > 1e-6) {
					arr << string_compose ("<Points unit=\"decibel\" id=\"%1\"><Target expression=\"gain\"/><RealPoint time=\"0\" value=\"%2\"/></Points>\n", id (), fmt (gain));
				}
				arr << "</Lanes>\n</Clip>\n";
			} else if (ctag == "MIDICLIP" && midi) {
				/* notes are in beats from the start of the sequence */
				double const b_off = offset * _tempo.bpm_at (start) / 60.0;
				arr << string_compose ("<Clip time=\"%1\" duration=\"%2\" contentTimeUnit=\"beats\" playStart=\"%3\" name=\"%4\"%5>\n",
				                       fmt (start), fmt (length), fmt (b_off), esc (cname), off ? " enable=\"false\"" : "")
				    << "<Notes timeUnit=\"beats\" id=\"" << id () << "\">\n";
				for (auto const* seq : c->children ()) {
					if (seq->name () != "SEQUENCE") {
						continue;
					}
					for (auto const* nt : seq->children ()) {
						if (nt->name () != "NOTE") {
							if (nt->name () == "CONTROL") {
								note (_("MIDI controller data was not imported."));
							}
							continue;
						}
						arr << string_compose ("<Note time=\"%1\" duration=\"%2\" channel=\"%3\" key=\"%4\" vel=\"%5\"/>\n",
						                       fmt (num (nt, "b")), fmt (num (nt, "l")), (int) num (nt, "c", 0) & 15, (int) num (nt, "p"), fmt (num (nt, "v", 100) / 127.0));
					}
				}
				arr << "</Notes>\n</Clip>\n";
			}
		}
		arr << "</Clips>\n</Lanes>\n";
	}
}

string
Converter::project_xml (bool embed)
{
	_ids = 0;
	_embedded.clear ();
	_embedded_of.clear ();

	std::ostringstream st, arr, out;
	int                n = 0;
	tracks (_edit, st, arr, embed, n, 0);

	/* markers: MARKERCLIPs on marker tracks */
	std::ostringstream mk;
	std::function<void (XMLNode const*)> find_markers = [&] (XMLNode const* node) {
		for (auto const* c : node->children ()) {
			if (c->name () == "MARKERCLIP") {
				mk << string_compose ("<Marker time=\"%1\" name=\"%2\"/>\n", fmt (num (c, "start")), esc (attr (c, "name", _("mark"))));
			} else if (c->name () == "MARKERTRACK" || c->name () == "FOLDERTRACK") {
				find_markers (c);
			}
		}
	};
	find_markers (_edit);

	/* tempo and time signature */
	XMLNode const* ts       = _edit->child ("TEMPOSEQUENCE");
	int            num0 = 4, den0 = 4;
	std::ostringstream tsa;
	bool first_sig = true;
	if (ts) {
		for (auto const* c : ts->children ()) {
			if (c->name () == "TIMESIG") {
				int const nn = (int) num (c, "numerator", 4), dd = (int) num (c, "denominator", 4);
				if (first_sig) {
					num0 = nn;
					den0 = dd;
					first_sig = false;
				} else {
					tsa << string_compose ("<TimeSignaturePoint time=\"%1\" numerator=\"%2\" denominator=\"%3\"/>\n", fmt (num (c, "startBeat")), nn, dd);
				}
			}
		}
	}
	std::ostringstream ta;
	for (size_t i = 1; i < _tempo.pts.size (); ++i) {
		ta << string_compose ("<RealPoint time=\"%1\" value=\"%2\" interpolation=\"hold\"/>\n", fmt (_tempo.pts[i].first), fmt (_tempo.pts[i].second));
	}

	/* appVersion is e.g. "Waveform 11.0.24" */
	string app_version = attr (_edit, "appVersion");
	if (app_version.compare (0, 9, "Waveform ") == 0) {
		app_version = app_version.substr (9);
	}

	XMLNode const* mv = _edit->child ("MASTERVOLUME");
	XMLNode const* mp = mv ? volume_plugin (mv) : 0;

	out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Project version=\"1.0\">\n"
	    << string_compose ("<Application name=\"Waveform\" version=\"%1\"/>\n", esc (app_version))
	    << "<Transport>\n"
	    << string_compose ("<Tempo unit=\"bpm\" value=\"%1\" id=\"wtempo\"/>\n", fmt (_tempo.pts.front ().second))
	    << string_compose ("<TimeSignature numerator=\"%1\" denominator=\"%2\" id=\"wsig\"/>\n", num0, den0)
	    << "</Transport>\n<Structure>\n"
	    << st.str ()
	    << "<Track contentType=\"audio notes\" id=\"wmastertrack\" name=\"Master\">\n"
	    << "<Channel audioChannels=\"2\" role=\"master\" id=\"wmaster\">\n"
	    << string_compose ("<Volume unit=\"decibel\" value=\"%1\" id=\"wmastervol\"/>\n", fmt (fader_db (mp)))
	    << "</Channel>\n</Track>\n"
	    << "</Structure>\n<Arrangement id=\"warr\">\n<Lanes timeUnit=\"seconds\" id=\"wlanes\">\n"
	    << arr.str ()
	    << "</Lanes>\n";
	if (!mk.str ().empty ()) {
		out << "<Markers timeUnit=\"seconds\" id=\"wmarkers\">\n" << mk.str () << "</Markers>\n";
	}
	if (!ta.str ().empty ()) {
		out << "<TempoAutomation timeUnit=\"beats\" unit=\"bpm\" id=\"wta\">\n<Target parameter=\"wtempo\"/>\n" << ta.str () << "</TempoAutomation>\n";
	}
	if (!tsa.str ().empty ()) {
		out << "<TimeSignatureAutomation timeUnit=\"beats\" id=\"wtsa\">\n<Target parameter=\"wsig\"/>\n" << tsa.str () << "</TimeSignatureAutomation>\n";
	}
	out << "</Arrangement>\n</Project>\n";
	return out.str ();
}

bool
write_zip (string const& out, std::map<string, string> const& files, string const& project_xml)
{
	struct archive* a = archive_write_new ();
	archive_write_set_format_zip (a);
	if (archive_write_open_filename (a, out.c_str ()) != ARCHIVE_OK) {
		archive_write_free (a);
		return false;
	}
	auto add = [&] (string const& name, char const* data, size_t len) {
		struct archive_entry* e = archive_entry_new ();
		archive_entry_set_pathname (e, name.c_str ());
		archive_entry_set_size (e, (la_int64_t) len);
		archive_entry_set_filetype (e, AE_IFREG);
		archive_entry_set_perm (e, 0644);
		bool ok = archive_write_header (a, e) == ARCHIVE_OK && archive_write_data (a, data, len) == (la_ssize_t) len;
		archive_entry_free (e);
		return ok;
	};
	bool ok = add ("project.xml", project_xml.data (), project_xml.size ());
	for (auto const& [name, file] : files) {
		string data;
		ok = ok && read_file (file, data) && add (name, data.data (), data.size ());
	}
	ok = archive_write_close (a) == ARCHIVE_OK && ok;
	archive_write_free (a);
	return ok;
}

} /* anonymous namespace */

bool
WaveformImport::to_dawproject (string const& edit, string& project_xml, vector<string>& notes)
{
	Converter c (notes);
	if (!c.load (edit)) {
		return false;
	}
	project_xml = c.project_xml (false);
	return true;
}

bool
WaveformImport::write_dawproject (string const& edit, string const& out, vector<string>& notes)
{
	Converter c (notes);
	if (!c.load (edit)) {
		return false;
	}
	string const xml = c.project_xml (true);
	if (!write_zip (out, c.embedded (), xml)) {
		notes.push_back (string_compose (_("Could not write %1."), out));
		return false;
	}
	return true;
}

DAWProjectImport::Result
WaveformImport::import (Session& session, string const& edit)
{
	vector<string> notes;
	string         xml;
	DAWProjectImport::Result r;
	if (!to_dawproject (edit, xml, notes)) {
		r.notes = notes;
		return r;
	}
	r = DAWProjectImport::import_xml (session, xml, Glib::path_get_dirname (edit));
	r.notes.insert (r.notes.begin (), notes.begin (), notes.end ());
	return r;
}

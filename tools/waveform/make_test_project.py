#!/usr/bin/env python3
"""Write a small Waveform 11-style project, shaped like the user's "Mother" song.

The layout follows IOdaw's prototype reader (prototype/tracktion_inspect.py),
which was checked on the real Mother project: a binary .tracktion project
index ("TP01") listing the audio files, and an XML .tracktionedit whose clips
refer to them as "<projectID>/<itemID>" (hex). Default track names are not
stored. A second edit holds the same tree in JUCE's binary ValueTree format.

Content, as checked by session_utils/waveform_test.cc:
  tracks 1-7  "01 Guitar L" .. "07 Vocal" (from the file names), 20 s mono files
  track 1     clip gain +15.16 dB, 0.5 s fade in
  track 2     fader -3 dB, pan 0.5 (right of centre)
  track 4     muted
  track 5     split at 12 s: +3 dB, then -6 dB
  track 7     vocal starting at 2 s, plug-in "TDR Nova"
  track 8     MIDI, 3 notes
  marker      "Verse" at 10 s; tempo 120, 4/4

Usage: make_test_project.py <out-dir>
writes <out-dir>/Mother.tracktion, Mother Edit 1.tracktionedit (XML),
Mother Edit 2.tracktionedit (binary) and Audio/*.wav
"""
import io
import math
import struct
import sys
import wave
import xml.etree.ElementTree as ET
from pathlib import Path

import numpy as np

PROJECT_ID = 0x5A3C91E7
NAMES = ["01 Guitar L", "02 Guitar R", "03 Guitar DI", "04 Room", "05 Guitar 2", "06 Voice ref", "07 Vocal"]
SECONDS = 20
RATE = 44100


def fader(db: float) -> float:
    """Waveform fader position for a gain in dB (0 dB at exp(-6/20))."""
    return math.exp((db - 6.0) / 20.0)


def write_wav(path: Path, freq: float) -> None:
    t = np.arange(RATE * SECONDS) / RATE
    x = (0.2 * np.sin(2 * np.pi * freq * t) * 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(x.tobytes())


def cstr(s: str) -> bytes:
    return s.encode("utf-8") + b"\0"


def write_index(path: Path) -> None:
    """TP01: header, properties, item table (as read by tracktion_inspect.py)."""
    props = [("name", b"Mother")]
    head = b"TP01" + struct.pack("<iii", PROJECT_ID - (1 << 32) if PROJECT_ID >= 1 << 31 else PROJECT_ID, 0, 0)
    body = struct.pack("<i", len(props))
    for name, val in props:
        body += cstr(name) + struct.pack("<i", len(val)) + val
    object_offset = len(head) + len(body)
    records = []
    for i, name in enumerate(NAMES, 1):
        records.append((i, cstr(name) + cstr("wave") + cstr("") + cstr(f"Audio/{name}.wav") + struct.pack("<d", SECONDS)))
    table = struct.pack("<i", len(records))
    rec_start = object_offset + 4 + 8 * len(records)
    blobs = b""
    for item_id, rec in records:
        table += struct.pack("<ii", item_id, rec_start + len(blobs))
        blobs += rec
    data = bytearray(head + body + table + blobs)
    struct.pack_into("<i", data, 8, object_offset)
    path.write_bytes(bytes(data))


def edit_tree() -> ET.Element:
    src = lambda i: f"{PROJECT_ID:x}/{i:x}"
    edit = ET.Element("EDIT", appVersion="Waveform 11.0.24", projectID=f"{PROJECT_ID:x}/1001")
    ts = ET.SubElement(edit, "TEMPOSEQUENCE")
    ET.SubElement(ts, "TEMPO", startBeat="0.0", bpm="120.0", curve="1.0")
    ET.SubElement(ts, "TIMESIG", numerator="4", denominator="4", startBeat="0.0")
    mv = ET.SubElement(edit, "MASTERVOLUME")
    ET.SubElement(mv, "PLUGIN", type="volume", volume=repr(fader(0.0)), pan="0.0")
    mt = ET.SubElement(edit, "MARKERTRACK")
    ET.SubElement(mt, "MARKERCLIP", name="Verse", start="10.0", length="0.0")
    for i, name in enumerate(NAMES, 1):
        t = ET.SubElement(edit, "TRACK", mute="1" if i == 4 else "0", solo="0")
        ET.SubElement(t, "PLUGIN", type="volume", volume=repr(fader(-3.0 if i == 2 else 0.0)), pan="0.5" if i == 2 else "0.0")
        if i == 7:
            ET.SubElement(t, "PLUGIN", type="vst", name="TDR Nova", filename="TDR Nova.vst3")
        if i == 5:
            ET.SubElement(t, "AUDIOCLIP", name=name, start="0.0", length="12.0", offset="0.0", source=src(i), gain="3.0")
            ET.SubElement(t, "AUDIOCLIP", name=name + " 2", start="12.0", length="8.0", offset="12.0", source=src(i), gain="-6.0")
        elif i == 7:
            ET.SubElement(t, "AUDIOCLIP", name=name, start="2.0", length="18.0", offset="0.0", source=src(i), gain="0.0")
        else:
            c = ET.SubElement(t, "AUDIOCLIP", name=name, start="0.0", length="20.0", offset="0.0", source=src(i),
                              gain="15.16" if i == 1 else "0.0")
            if i == 1:
                c.set("fadeIn", "0.5")
    t = ET.SubElement(edit, "TRACK", mute="0", solo="0")
    ET.SubElement(t, "PLUGIN", type="volume", volume=repr(fader(0.0)), pan="0.0")
    mc = ET.SubElement(t, "MIDICLIP", name="Bass idea", start="4.0", length="4.0", offset="0.0")
    seq = ET.SubElement(mc, "SEQUENCE")
    for b, p in ((0.0, 40), (2.0, 43), (4.0, 45)):
        ET.SubElement(seq, "NOTE", p=str(p), b=repr(b), l="1.0", v="100", c="0")
    return edit


def compressed_int(v: int) -> bytes:
    neg = v < 0
    v = abs(v)
    raw = v.to_bytes(4, "little").rstrip(b"\0") or b""
    return bytes([len(raw) | (0x80 if neg else 0)]) + raw


def var(value: str) -> bytes:
    """JUCE var: numbers as int/double, the rest as strings."""
    try:
        if value.lstrip("-").isdigit():
            payload = b"\x01" + struct.pack("<i", int(value))
        else:
            payload = b"\x04" + struct.pack("<d", float(value))
    except ValueError:
        payload = b"\x05" + cstr(value)
    return compressed_int(len(payload)) + payload


def value_tree(e: ET.Element) -> bytes:
    out = cstr(e.tag) + compressed_int(len(e.attrib))
    for k, v in e.attrib.items():
        out += cstr(k) + var(v)
    out += compressed_int(len(e))
    for c in e:
        out += value_tree(c)
    return out


def main() -> None:
    out = Path(sys.argv[1])
    (out / "Audio").mkdir(parents=True, exist_ok=True)
    for i, name in enumerate(NAMES, 1):
        write_wav(out / "Audio" / f"{name}.wav", 110.0 * i)
    write_index(out / "Mother.tracktion")
    tree = edit_tree()
    ET.ElementTree(tree).write(out / "Mother Edit 1.tracktionedit", encoding="UTF-8", xml_declaration=True)
    (out / "Mother Edit 2.tracktionedit").write_bytes(value_tree(tree))
    print(out)


if __name__ == "__main__":
    main()

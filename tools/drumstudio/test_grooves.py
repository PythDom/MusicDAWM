#!/usr/bin/env python3
"""Checks for the Drum Studio groove library.

- the committed library in share/drumstudio/grooves is exactly what
  make_grooves.py generates (no hand edits, deterministic output);
- every file is a valid SMF that mido can read, with the declared meter;
- each file ends exactly on its last bar line (clean loops);
- only the GM/AVL-compatible drum notes are used, all on MIDI channel 10;
- busier complexity means at least as many notes (looping parts).

Usage: test_grooves.py            (needs the `mido` package)
"""

import json
import sys
import tempfile
from pathlib import Path

import mido

HERE = Path(__file__).resolve().parent
LIB = HERE.parent.parent / "share" / "drumstudio" / "grooves"
sys.path.insert(0, str(HERE))
import make_grooves  # noqa: E402

ALLOWED = {36, 37, 38, 39, 41, 42, 44, 45, 46, 49, 51, 53, 54, 56, 57}
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


def main():
    with tempfile.TemporaryDirectory() as tmp:
        sys.argv = ["make_grooves.py", tmp]
        make_grooves.main()
        fresh = sorted(p.relative_to(tmp) for p in Path(tmp).rglob("*") if p.is_file())
        committed = sorted(p.relative_to(LIB) for p in LIB.rglob("*") if p.is_file() and p.name != "README.md")
        check(fresh == committed, "committed file list differs from generator output")
        for rel in fresh:
            check((Path(tmp) / rel).read_bytes() == (LIB / rel).read_bytes(),
                  f"{rel}: committed file differs from generator output (regenerate it)")

    index = json.loads((LIB / "index.json").read_text())
    check(len(index["styles"]) >= 12, "fewer than 12 styles")
    for style in index["styles"]:
        num, den = style["meter"]
        counts = {}
        for g in style["grooves"]:
            mid = mido.MidiFile(LIB / g["file"])
            ticks = 0
            notes = 0
            meter = None
            for msg in mid.tracks[0]:
                ticks += msg.time
                if msg.type == "time_signature":
                    meter = (msg.numerator, msg.denominator)
                if msg.type == "note_on" and msg.velocity > 0:
                    notes += 1
                    check(msg.note in ALLOWED, f"{g['file']}: note {msg.note} outside the drum map")
                    check(msg.channel == 9, f"{g['file']}: not on MIDI channel 10")
            bar = num * mid.ticks_per_beat * 4 // den
            check(meter == (num, den), f"{g['file']}: meter {meter}, expected {num}/{den}")
            check(ticks == g["bars"] * bar, f"{g['file']}: length {ticks} ticks, expected {g['bars']} bars")
            check(notes > 0, f"{g['file']}: no notes")
            counts[(g["part"], g["complexity"])] = notes
        for part in ("verse", "chorus", "bridge", "prechorus"):
            seq = [counts[(part, c)] for c in range(1, 5)]
            check(seq == sorted(seq), f"{style['key']}/{part}: notes per complexity {seq} not increasing")

    if failures:
        print("\n".join("FAIL  " + f for f in failures))
        print(f"FAILED: {len(failures)} problem(s)")
        sys.exit(1)
    print(f"PASSED: {sum(len(s['grooves']) for s in index['styles'])} grooves checked")


if __name__ == "__main__":
    main()

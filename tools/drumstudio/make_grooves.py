#!/usr/bin/env python3
"""Generate MusicDAWM's Drum Studio groove library (CC0).

12 styles x 7 song parts x 4 complexity levels = 336 Standard MIDI Files,
written by rules (no samples, no third-party patterns), plus index.json.

Every file uses the part of the General MIDI drum map that also matches the
bundled AVL kit (x42-avldrums), so the grooves play correctly on both:
  36 kick, 37 side-stick, 38 snare, 39 clap, 41 floor tom, 42 closed hi-hat,
  44 pedal hi-hat, 45 rack tom, 46 open hi-hat, 49 crash, 51 ride,
  53 ride bell, 54 tambourine, 56 cowbell, 57 crash 2.
(GM toms 48 and 50 are avoided: in the AVL map they are hi-hat swish and a
choked crash.)

Part lengths: intro, verse, pre-chorus, chorus and bridge are 2 bars and loop;
a fill is 1 bar and plays once; an ending is 2 bars (a final hit that rings).
Complexity 1 is the simplest, 4 the busiest.

Output is deterministic: running the script twice gives identical files
(tested by tools/drumstudio/test_grooves.py).

Usage: make_grooves.py <out-dir>    (normally share/drumstudio/grooves)
"""

import json
import random
import struct
import sys
from pathlib import Path

PPQ = 480  # ticks per quarter note

KICK, STICK, SNARE, CLAP = 36, 37, 38, 39
FTOM, CHH, PHH, RTOM, OHH = 41, 42, 44, 45, 46
CRASH, RIDE, BELL, TAMB, COWBELL, CRASH2 = 49, 51, 53, 54, 56, 57

PARTS = ["intro", "verse", "prechorus", "chorus", "bridge", "fill", "ending"]
PART_NAMES = {
    "intro": "Intro", "verse": "Verse", "prechorus": "Pre-chorus", "chorus": "Chorus",
    "bridge": "Bridge", "fill": "Fill", "ending": "Ending",
}
PART_BARS = {"intro": 2, "verse": 2, "prechorus": 2, "chorus": 2, "bridge": 2, "fill": 1, "ending": 2}


class Style:
    """A style: meter, feel and the rules that place notes on a step grid.

    The grid has `steps` steps per bar. Straight 4/4 styles use 16 steps
    (sixteenth notes); shuffle, swing and 6/8 use 12 (eighth-note triplets in
    4/4, or sixteenths in 6/8).
    """

    def __init__(self, key, name, num, den, steps, tempo, swing=0.0):
        self.key, self.name = key, name
        self.num, self.den = num, den
        self.steps = steps
        self.tempo = tempo      # suggested audition tempo, BPM
        self.swing = swing      # 0..1: delay of off-beat sixteenths (straight grids only)

    @property
    def bar_ticks(self):
        return self.num * PPQ * 4 // self.den

    def step_ticks(self, step):
        """Tick position of a grid step within the bar, with swing applied."""
        t = step * self.bar_ticks // self.steps
        if self.swing and self.steps == 16 and step % 2 == 1:
            t += int(self.swing * (self.bar_ticks // self.steps) / 2)
        return t


STYLES = [
    Style("rock", "Rock", 4, 4, 16, 120),
    Style("pop", "Pop", 4, 4, 16, 104),
    Style("ballad", "Ballad", 4, 4, 16, 72),
    Style("blues_shuffle", "Blues shuffle", 4, 4, 12, 96),
    Style("funk", "Funk", 4, 4, 16, 100, swing=0.15),
    Style("country", "Country", 4, 4, 16, 112),
    Style("reggae", "Reggae", 4, 4, 16, 80, swing=0.3),
    Style("bossa", "Bossa nova", 4, 4, 16, 128),
    Style("jazz_swing", "Jazz swing", 4, 4, 12, 140),
    Style("halftime", "Half-time", 4, 4, 16, 140),
    Style("six_eight", "6/8", 6, 8, 12, 60),
    Style("punk", "Punk", 4, 4, 16, 170),
]


class Bar:
    """Notes of one bar: (step, note, velocity)."""

    def __init__(self):
        self.hits = []

    def add(self, step, note, vel):
        self.hits.append((step, note, vel))

    def add_all(self, steps, note, vel):
        for s in steps:
            self.add(s, note, vel)


def beats(style, every):
    """Grid steps of each beat (or of every n-th grid step)."""
    return list(range(0, style.steps, every))


# --------------------------------------------------------------------------
# Time keeping (hi-hat, ride, ...), per style and complexity

def timekeeping(style, cx, part, bar_index):
    b = Bar()
    s = style.steps
    k = style.key
    loud = part in ("chorus", "bridge")
    if k == "jazz_swing":
        # ride "spang-a-lang": beats, plus the triplet skip note before 2 and 4
        b.add_all(beats(style, 3), RIDE, 92)
        if cx >= 2:
            b.add_all([5, 11], RIDE, 70)
        if cx >= 3:
            b.add_all([3, 9], PHH, 60)          # hi-hat foot on 2 and 4
        if cx >= 4:
            b.add(2, RIDE, 55)
        return b
    if k == "blues_shuffle" or k == "six_eight":
        cym = RIDE if loud else CHH
        step = 2 if cx == 1 else 1 if cx == 4 else 2
        if k == "blues_shuffle":
            pattern = [0, 3, 6, 9] if cx == 1 else [0, 2, 3, 5, 6, 8, 9, 11]
            if cx == 4:
                pattern = list(range(12))
        else:
            pattern = [0, 6] if cx == 1 else list(range(0, 12, step))
            if cx >= 3:
                pattern = list(range(0, 12, 2)) if cx == 3 else list(range(12))
        for p in pattern:
            b.add(p, cym, 88 if p % (s // style.num) == 0 else 66)
        return b
    if k == "bossa":
        b.add_all(range(0, s, 2), CHH if not loud else RIDE, 64)
        if cx >= 3:
            b.add_all(range(1, s, 2), CHH if not loud else RIDE, 46)
        return b
    if k == "reggae":
        b.add_all(range(0, s, 2), CHH, 70)
        if cx >= 2:
            b.add_all([6, 14], OHH, 74)
        if cx >= 4:
            b.add_all(range(1, s, 2), CHH, 44)
        return b
    # straight styles
    cym = RIDE if (loud and k in ("rock", "pop", "country", "ballad")) else CHH
    if k == "punk" and loud:
        cym = CRASH2
    if k == "halftime" and cx >= 2:
        sub = 2 if cx == 2 else 1
    else:
        sub = {1: 4, 2: 2, 3: 2, 4: 1}[cx]
    if k == "funk":
        sub = 2 if cx <= 2 else 1
    if k == "ballad":
        sub = {1: 4, 2: 4, 3: 2, 4: 2}[cx]
    for p in range(0, s, sub):
        accent = p % 4 == 0
        b.add(p, cym, 90 if accent else (60 if sub == 1 else 72))
    if cx >= 3 and cym == CHH and k in ("rock", "pop", "country", "punk"):
        b.hits = [h for h in b.hits if h[0] != 14]
        b.add(14, OHH, 82)                      # open hat lift into the next bar
    return b


# --------------------------------------------------------------------------
# Kick and snare, per style and complexity

def kick_snare(style, cx, part, bar_index):
    b = Bar()
    k = style.key
    second = bar_index % 2 == 1
    if k == "jazz_swing":
        b.add_all([3, 9], STICK if cx <= 2 else SNARE, 40 if cx <= 2 else 36)  # light 2 and 4
        if cx >= 2:
            b.add_all(beats(style, 3), KICK, 38)     # feathered kick
        if cx >= 4:
            b.add(8, SNARE, 58)                      # comping "and" of 3
            if second:
                b.add(11, SNARE, 64)
        return b
    if k == "blues_shuffle":
        b.add_all([0, 6], KICK, 104)
        b.add_all([3, 9], SNARE, 108)
        if cx >= 2:
            b.add(5, KICK, 80)
        if cx >= 3:
            b.add(11, KICK, 76)
        if cx >= 4:
            b.add_all([2, 8], SNARE, 42)
        return b
    if k == "six_eight":
        b.add(0, KICK, 104)
        b.add(6, SNARE if part != "intro" else STICK, 100)
        if cx >= 2:
            b.add(4, KICK, 78)
        if cx >= 3:
            b.add(10, KICK, 70)
        if cx >= 4:
            b.add_all([8, 11], SNARE, 40)
        return b
    if k == "bossa":
        # side-stick clave (bossa rhythm over two bars) and surdo-like kick
        clave = [0, 6, 12] if not second else [4, 10]
        b.add_all(clave, STICK, 84)
        b.add_all([0, 8], KICK, 86)
        if cx >= 2:
            b.add_all([6, 14], KICK, 70)
        if cx >= 4:
            b.add_all([3, 11], KICK, 56)
        return b
    if k == "reggae":
        # one drop: kick and side-stick (or snare) together on beat 3
        b.add(8, KICK, 104)
        b.add(8, STICK if cx <= 2 else SNARE, 98)
        if cx >= 3:
            b.add(0, KICK, 80)                       # steppers-ish push
        if cx >= 4:
            b.add_all([4, 12], KICK, 74)
        return b
    if k == "halftime":
        b.add(8, SNARE, 112)
        b.add(0, KICK, 108)
        if cx >= 2:
            b.add(14, KICK, 84)
        if cx >= 3:
            b.add_all([3, 10], KICK, 88)
        if cx >= 4:
            b.add_all([5, 13], SNARE, 42)
        return b
    if k == "punk":
        if cx == 1:
            b.add_all([0, 8], KICK, 110)
            b.add_all([4, 12], SNARE, 112)
        else:
            b.add_all([0, 4, 8, 12], KICK, 108)
            b.add_all([2, 6, 10, 14], SNARE, 110)  # "skank" kick/snare alternation
        if cx >= 3:
            b.add_all([3, 11], KICK, 90)
        if cx >= 4:
            b.add(15, SNARE, 96)
        return b
    if k == "funk":
        b.add_all([4, 12], SNARE, 112)
        b.add_all([0, 10], KICK, 108)
        if cx >= 2:
            b.add(3, KICK, 92)
        if cx >= 3:
            b.add_all([7, 15], SNARE, 40)          # ghost notes
            b.add(13, KICK, 84)
        if cx >= 4:
            b.add_all([1, 9, 11], SNARE, 36)
            b.add(6, KICK, 80)
        return b
    if k == "country":
        b.add_all([0, 8], KICK, 100)
        b.add_all([4, 12], SNARE, 104)
        if cx >= 2:
            b.add(14, KICK, 70)
        if cx >= 3:
            b.add_all([2, 6, 10, 14], SNARE, 44)   # train-beat ghosts
        if cx >= 4:
            b.add(6, KICK, 76)
        return b
    if k == "ballad":
        b.add(0, KICK, 92)
        b.add_all([4, 12], STICK if part in ("intro", "verse") else SNARE, 90)
        if cx >= 2:
            b.add(10, KICK, 74)
        if cx >= 3:
            b.add(7, KICK, 60)
        if cx >= 4:
            b.add(15, SNARE, 40)
        return b
    # rock and pop
    b.add_all([4, 12], SNARE, 112 if k == "rock" else 104)
    b.add_all([0, 8], KICK, 108)
    if cx >= 2:
        b.add(10, KICK, 90)
    if cx >= 3:
        b.add(3 if not second else 7, KICK, 84)
        if k == "pop":
            b.add(12, CLAP, 70)
    if cx >= 4:
        b.add(14 if not second else 15, KICK, 86)
        b.add(15, SNARE, 44)
    return b


# --------------------------------------------------------------------------
# Song parts

def groove_bar(style, cx, part, bar_index):
    a = timekeeping(style, cx, part, bar_index)
    bks = kick_snare(style, cx, part, bar_index)
    a.hits += bks.hits
    if part == "chorus" and bar_index == 0:
        a.add(0, CRASH, 112)
    if part == "chorus" and style.key == "pop" and cx >= 2:
        a.add_all(beats(style, style.steps // style.num), TAMB, 64)
    if part == "prechorus":
        # build: drop the hats on the first bar's downbeat, add floor-tom pulse
        a.add_all(beats(style, style.steps // style.num), FTOM, 58 + 8 * cx)
    if part == "bridge" and cx >= 3 and style.key in ("rock", "pop", "country", "punk"):
        a.add(0, BELL, 86)
    if part == "intro":
        # thinner: halve the kick, keep time keeping and backbeat
        a.hits = [h for h in a.hits if not (h[1] == KICK and h[0] % (style.steps // 2) != 0)]
        if bar_index == 0 and cx >= 3:
            a.add(0, CRASH, 100)
    return a


def fill_bar(style, cx):
    """A one-bar fill; busier with complexity. The next part's downbeat follows."""
    b = Bar()
    s = style.steps
    beat = s // style.num
    if style.key == "jazz_swing":
        b.add_all(beats(style, 3), RIDE, 86)
        b.add_all([3, 9], PHH, 56)
        comp = {1: [11], 2: [8, 11], 3: [5, 8, 11], 4: [2, 5, 7, 8, 10, 11]}[cx]
        b.add_all(comp, SNARE, 70)
        b.add(11, KICK, 60)
        return b
    # keep time for the first part of the bar, then the fill
    start = {1: s - beat // 2 if beat % 2 == 0 else s - 1, 2: s - beat, 3: s - 2 * beat, 4: 0}[cx]
    if style.num == 6:
        start = {1: s - 2, 2: s - 3, 3: s - 6, 4: 0}[cx]
    groove = groove_bar(style, min(cx, 2), "verse", 0)
    b.hits = [h for h in groove.hits if h[0] < start]
    sub = 1 if (cx >= 3 or style.steps == 12) else 2
    run = list(range(start, s, sub))
    voices = [SNARE] * len(run)
    if cx >= 3:
        # move down the kit: snare, rack tom, floor tom
        third = max(1, len(run) // 3)
        voices = [SNARE] * third + [RTOM] * third + [FTOM] * (len(run) - 2 * third)
    for i, (step, v) in enumerate(zip(run, voices)):
        b.add(step, v, 72 + int(36 * i / max(1, len(run) - 1)))   # crescendo
    if cx >= 2:
        b.add(start, KICK, 96)
    b.add(s - 1 if sub == 1 else s - 2, KICK, 100)
    return b


def ending_bars(style, cx):
    """Ending: an optional short pickup, then a final hit that rings for a bar."""
    first, second = Bar(), Bar()
    s = style.steps
    beat = s // style.num
    if cx == 1:
        first.add(0, CRASH, 116)
        first.add(0, KICK, 112)
    else:
        pickup = {2: s - beat, 3: s - 2 * beat, 4: s // 2}[cx]
        groove = groove_bar(style, 2, "chorus", 0)
        first.hits = [h for h in groove.hits if h[0] < pickup]
        sub = 1 if (cx >= 3 or style.steps == 12) else 2
        for i, step in enumerate(range(pickup, s, sub)):
            first.add(step, SNARE if i % 2 == 0 or cx < 4 else RTOM, 80 + i * 3)
        second.add(0, CRASH, 118)
        second.add(0, CRASH2 if cx >= 3 else KICK, 112)
        second.add(0, KICK, 112)
    return [first, second]


def make_part(style, part, cx):
    if part == "fill":
        return [fill_bar(style, cx)]
    if part == "ending":
        return ending_bars(style, cx)
    return [groove_bar(style, cx, part, i) for i in range(PART_BARS[part])]


# --------------------------------------------------------------------------
# Standard MIDI File output

def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.insert(0, (n & 0x7F) | 0x80)
        n >>= 7
    return bytes(out)


def humanise(seed, vel):
    """Small, deterministic velocity variation so repeated hits are not identical."""
    rnd = random.Random(seed)
    return max(1, min(127, vel + rnd.randint(-5, 5)))


def smf(style, part, cx, bars):
    events = []  # (tick, order, bytes): note-offs (0) before note-ons (1)
    note_len = PPQ // 8
    for bi, bar in enumerate(bars):
        for (step, note, vel) in sorted(set(bar.hits)):
            t = bi * style.bar_ticks + style.step_ticks(step)
            v = humanise(f"{style.key}/{part}/{cx}/{bi}/{step}/{note}", vel)
            events.append((t, 1, bytes([0x99, note, v])))
            events.append((t + note_len, 0, bytes([0x89, note, 0])))
    events.sort(key=lambda e: (e[0], e[1], e[2]))
    total = len(bars) * style.bar_ticks
    name = f"{style.name} {PART_NAMES[part]} {cx}".encode()
    track = bytearray()
    track += vlq(0) + b"\xff\x03" + vlq(len(name)) + name
    den_pow = {4: 2, 8: 3}[style.den]
    clocks = 24 if style.den == 4 else 36
    track += vlq(0) + b"\xff\x58\x04" + bytes([style.num, den_pow, clocks, 8])
    tempo = 60_000_000 // style.tempo
    track += vlq(0) + b"\xff\x51\x03" + tempo.to_bytes(3, "big")
    now = 0
    for t, _, data in events:
        track += vlq(t - now) + data
        now = t
    track += vlq(total - now) + b"\xff\x2f\x00"     # end of track exactly at the last bar line
    header = b"MThd" + struct.pack(">IHHH", 6, 0, 1, PPQ)
    return header + b"MTrk" + struct.pack(">I", len(track)) + bytes(track)


def index_xml(index):
    """The same index as XML, which libardour can read with its own parser."""
    from xml.sax.saxutils import quoteattr as q
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             f'<DrumStudioGrooves version="1" license={q(index["license"])} ppq="{index["ppq"]}">']
    for p in index["parts"]:
        lines.append(f'  <Part key={q(p["key"])} name={q(p["name"])}/>')
    for st in index["styles"]:
        lines.append(f'  <Style key={q(st["key"])} name={q(st["name"])} meter-num="{st["meter"][0]}" '
                     f'meter-den="{st["meter"][1]}" tempo="{st["tempo"]}">')
        for g in st["grooves"]:
            lines.append(f'    <Groove part={q(g["part"])} complexity="{g["complexity"]}" '
                         f'bars="{g["bars"]}" file={q(g["file"])}/>')
        lines.append('  </Style>')
    lines.append('</DrumStudioGrooves>')
    return "\n".join(lines) + "\n"


def main():
    out = Path(sys.argv[1])
    index = {"license": "CC0-1.0", "generator": "tools/drumstudio/make_grooves.py",
             "ppq": PPQ, "parts": [{"key": p, "name": PART_NAMES[p]} for p in PARTS], "styles": []}
    for style in STYLES:
        entry = {"key": style.key, "name": style.name, "meter": [style.num, style.den],
                 "tempo": style.tempo, "grooves": []}
        d = out / style.key
        d.mkdir(parents=True, exist_ok=True)
        for part in PARTS:
            for cx in range(1, 5):
                bars = make_part(style, part, cx)
                fname = f"{part}-{cx}.mid"
                (d / fname).write_bytes(smf(style, part, cx, bars))
                entry["grooves"].append({"part": part, "complexity": cx, "bars": len(bars),
                                         "file": f"{style.key}/{fname}"})
        index["styles"].append(entry)
    (out / "index.json").write_text(json.dumps(index, indent=1) + "\n")
    (out / "index.xml").write_text(index_xml(index))
    print(f"{sum(len(s['grooves']) for s in index['styles'])} grooves in {out}")


if __name__ == "__main__":
    main()

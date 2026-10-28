# WP6 report: migration from Waveform 11 and Cubase

*2026-10-24. MusicDAWM 0.1.0.*

## What was built

- **Waveform reader** (`libs/ardour/waveform_import.cc`, D-031), in C++ inside the program:
  - It reads the `.tracktion` project index (binary "TP01") and the `.tracktionedit`, stored as XML or as JUCE's binary ValueTree.
  - It converts the edit to DAWproject and imports that with the WP5 importer.
  - What comes across: tracks, clips (position, trim, clip gain, fades), faders, pan, mute, solo, master, tempo, time signature, markers and MIDI clips.
  - It reports plug-ins, aux sends, clip pan, speed changes, loops, and chord, arranger and automation tracks.
- **Waveform specifics** (D-032):
  - Tracks are named after their files, because Waveform stores no default names.
  - Mono files give mono tracks.
  - Clip gain travels as gain automation inside the clip; the importer applies a constant one as the region gain.
- **DAWproject writer:** the converted song can be saved as a `.dawproject` with the audio inside. It validates against the official `Project.xsd` (xmllint).
- **Menu entries** (Session menu):
  - Import Waveform Project…
  - Convert Waveform Project to DAWproject…
  - Import Cubase Project…: explains the DAWproject export in Cubase 14.0.20+, then opens the DAWproject import (D-033).

## Requirement IDs

| ID | Status |
|---|---|
| S4 / WP6: Waveform 11 and Cubase migration | Pass on a generated Mother-shaped Waveform project, in XML and in binary form, and through the `.dawproject` written from it. IOdaw's prototype reader, which was checked on the real Mother project, reads the generated files the same way. **Real Mother project (2026-10-28): pass.** The user imported `Mother Edit 1.tracktionedit` (Waveform 11.0.24) in the Windows build; every clip matched the edit file: 7 tracks (the 7th has no clip in Waveform either), 7 clips, clip gains +2.85 / +9.91 / +14.91 / +14.66 / +7.37 and −0.06 at the 292 s split / +15.16 dB, track 4 muted, no markers, all audio found. A real Cubase export has not been tried (the user no longer has Cubase). |

## Tests

| Check | Result |
|---|---|
| `musicdawm9-waveform_test` on `tools/waveform/make_test_project.py`'s project, three ways: XML edit, binary edit, converted `.dawproject` re-imported. It checks: the application; 8 tracks named after their files; clip gain +15.16 dB; fade in; resampled length; fader −3 dB; pan; mute; split at 12 s with +3/−6 dB; vocal at 2 s; plug-in reported; MIDI clip with 3 notes; tempo; marker; master | 22/22 each way |
| Written `project.xml` against the official `Project.xsd` | validates |
| DAWproject import test (WP5), after adding in-memory import and clip gain | 44/44 |
| GUI (Linux, Xvfb): Session → Import Waveform Project… on the binary edit | tracks, clip gain and the marker shown; summary dialog |
| Windows CI: the three Waveform runs against the portable folder, plus all earlier tests | pass |

## CI

Green on `173906f752`: `Windows build` run 40070543662 (started by hand, D-030; 20 min, 96.4% compiler-cache hits). It runs the Mother smoke test, the Drum Studio test, the DAWproject test and the three Waveform runs (XML, binary, converted `.dawproject`), all from the portable folder. `Linux build` is green too.

## Known issues

- The fader law (dB = 20 ln(position) + 6) comes from IOdaw's prototype and is not yet compared with Waveform's display. If the imported faders differ from what Waveform showed, this is the place to fix.
- Not imported: Waveform plug-ins (listed by name), aux sends, clip pan, time-stretch and clip loops (listed), automation curves.
- Compressed edit files are refused with a message: re-save in Waveform. They have not been seen in practice.
- The Cubase menu path given to the user (*File → Export → DAWproject*) is from Steinberg's description of Cubase 14 and is not checked here.

## Time

| | Estimate (brief) | Actual |
|---|---|---|
| WP6 | 1–2 weeks | about half a day of agent time, plus CI runs |

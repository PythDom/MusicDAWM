# WP5 report: DAWproject import

*2026-10-24. MusicDAWM 0.1.0. Added to the plan by the user before WP6 (D-026).*

## What was built

- **Importer** (`libs/ardour/dawproject_import.cc`, D-027), written in C++ from the DAWproject 1.0 schema and documentation. It does not use the reference code. It reads the ZIP safely: entries with absolute paths or `..` are refused, because Ardour's archive reader would otherwise write them outside the target folder. It brings across:
  - **Song:** tempo and time signature, including changes; markers.
  - **Tracks:** audio and MIDI tracks; folders flattened.
  - **Mixer:** effect and group channels as busses; output routing; pre/post sends; volume, pan, mute, solo, colours.
  - **Automation:** volume, pan, mute and send level.
  - **Audio clips:** at any nesting depth (Bitwig-style clips inside clips, warps), in beats or seconds; offsets, fades, loops, disabled clips, names.
  - **MIDI clips:** notes.
- **Report:** everything that is not imported is reported, one line per kind: plug-ins (D-028), time stretch with its ratio (D-029), note expressions, VCAs, scenes, video, send pan.
- **Menu:** Session → Import DAWproject…, with a summary dialog afterwards; the editor then zooms to the song.

## Requirement IDs

| ID | Status |
|---|---|
| S4 Import DAWproject | Pass for the content listed above, on a test file using every element the importer handles. **Not yet tried with a real Cubase 14 export**: none was available here; that is the user's part of the final acceptance check. |

## Tests

| Check | Result |
|---|---|
| `musicdawm9-dawproject_test` on `tools/dawproject/make_test_project.py`'s file, local. It covers: tempo and meter changes; mono 44.1 kHz and stereo 48 kHz audio (resampled); clip offset, fade, name; nested clip with warps; disabled clip after a tempo change; loop; folder; MIDI notes and the clip window; volume, pan, mute, master gain, group routing, send level, volume automation; markers; the plug-in report; refusing an archive entry with `..`; session range. | 44/44 pass |
| Export of the imported session (`musicdawm9-export`) | audio where expected: guitar through its group bus from 2 s, vocal 4–12 s; muted parts silent |
| GUI, Linux (Xvfb): Session → Import DAWproject… with the same file | tracks, busses, colours, tempo and meter changes, markers and the summary dialog as expected |
| Linux and Windows CI: the same test (on Windows against the portable folder) | *see CI below* |
| Ardour unit tests, Mother smoke test, Drum Studio test | no regression (*CI below*) |

## CI

*Filled in when the runs on the WP5 commit finish.*

## Known issues

- Only tested on generated files. Real exports differ in detail: units, nesting, how Cubase writes clip gain. The first Cubase 14 file from the user is the real test. The importer reports what it skips rather than failing.
- No clip gain: DAWproject 1.0 has no clip gain attribute. If Cubase writes it as gain automation inside the clip, it is reported as not imported.
- The import runs in the GUI thread, so a very large project freezes the window until it is done (with a busy cursor).
- In the file chooser, typing a path and pressing Return navigates instead of choosing the file in this GTK version; click **Import**.

## Time

| | Estimate (brief) | Actual |
|---|---|---|
| WP5 | 3–4 weeks | about half a day of agent time, plus CI runs |

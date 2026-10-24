# WP9 report: Melodyne through ARA 2

*2026-10-24. MusicDAWM 0.1.0.*

## What was built

- **ARA SDK** 2.3 (Apache-2.0), host subset vendored in `libs/ara` (API, host dispatch, debug helpers). It is built in with `./waf configure --ara`. The Windows build uses it, so the program is distributed under GPLv3 (D-005, D-034), and the zip carries the licence texts in `licences\`.
- **ARA hosting** (`libs/ardour/ara_host.cc`), following PHASE0 §3:

  | ARA | MusicDAWM |
  |---|---|
  | document | one per session and ARA plug-in |
  | musical context | the tempo map (tempo entries and bar signatures; updated when the map changes) |
  | region sequence | the track |
  | audio source | the region's channel sources, read through the host |
  | audio modification | the plug-in's edits, ID kept in the region (shared by split pieces and copies) |
  | playback region | the region, while it is in a playlist; moves, trims and renames update it |

  - Host interfaces: audio access, archiving, content access (tempo, bar signatures), model updates, playback (the plug-in's transport buttons).
- **VST3 binding:** a VST3 plug-in that offers `ARA::IPlugInEntryPoint2` is bound to the session's document when it becomes a **Region Effect**. Binding happens before the plug-in is activated, with the playback-renderer, editor-renderer and editor-view roles. As a track insert, the plug-in stays a normal plug-in.
- **Rendering** goes through the existing Region FX path, which passes timeline positions. Region gain and the gain envelope are applied after an ARA plug-in (D-035a); fades already were.
- **Saving:** the plug-in's archive is written to `plugins/ara-<factory>-<snapshot>.dat` when the session is saved. It is restored per source and modification when the regions come back (D-036).
- **GUI:**
  - Model updates and analysis progress are polled from the 0.1 s timer.
  - Opening the plug-in's window from the region editor shows that region, because the region is selected in the plug-in's ARA editor view.

## Requirement IDs

| ID | Status |
|---|---|
| WP9: Melodyne opens a clip; edits follow moves and trims; edits are saved | **Pass with the ARA SDK's test plug-in** (it validates API use and has analysis, rendering and persistence). **Not yet tried with Melodyne:** that is the user's acceptance step (below). Time used: about 1 day of the 2-month limit. |

## Tests

`session_utils/ara_test.cc` runs in two processes: build, check and save, then `--reload`. Results:

| Check | Linux (local) | CI |
|---|---|---|
| ARATestPlugIn loads, is an ARA 2 plug-in, is not in ARA mode as a normal plug-in | pass | Linux and Windows jobs |
| As Region FX: ARA mode, one playback region | pass | |
| The plug-in reads the whole file through the host and finds 2 notes, the second at 3.5 s on the timeline | pass | |
| Renders the region at its position; after a move to 10 s; after a front trim | pass | |
| The notes follow the trim (11.5 s) | pass | |
| Region gain 0.5 applies after the ARA rendering | pass | |
| ARA data file written on save | pass | |
| Reload in a new process: ARA mode again, one playback region, notes restored from the saved data (no new analysis), renders as before | pass | |
| No regressions: libardour unit tests (25 OK), Mother smoke + export (343.0 s), Drum Studio, DAWproject, Waveform (XML and binary) | pass | |

Two things were found by the SDK's validating plug-in and fixed:

- Playback regions must be added or removed only while the plug-in is inactive.
- The document must outlive the plug-in instances bound to it when a session closes.

## Known limits

- Copies share the plug-in's edits with the original (D-035b). Separate edits per copy need Ardour to tell a duplicate from a split; that is left for later.
- **Session utilities: pre-existing limitation, not ARA.** They cannot load a second session after unloading one in the same process: the process threads hang on teardown, with or without ARA. That is why the test reloads in a new process.
- **Rendering thread.** Region FX renders ahead of playback in the disk thread (PHASE0 §3, risk 1). The test plug-in is fine with this. Melodyne should be too (it supports offline rendering), but only the real test will tell.
- Only VST3 ARA, not CLAP ARA (WP8).

## Melodyne acceptance test (for the user)

1. Unzip the new build. Check that Melodyne essential (VST3) shows in the plug-in list after a scan.
2. Open a song (or import a vocal WAV) and double-click a vocal clip. In **Region Effects**, right-click → **New Plugin** → Melodyne.
3. Double-click Melodyne in the box. **Expected:** the Melodyne window shows the clip's notes after a short analysis. Report what you see if it stays empty or says "Transfer".
4. Move a note, play: you hear the change. Move the clip on the timeline and trim its start: the edit stays on the same sung note.
5. Change the clip gain: the level changes, with Melodyne still active.
6. Save, close, reopen: the edit is still there, without re-analysis. The session folder has `plugins\ara-…dat`.
7. Split the clip: both halves keep the edits.

Please send the result of each step, plus any error window. If step 3 fails, the log (*Window → Log*) is the most useful thing to send.

## Files

- New: `libs/ara/` (vendored SDK subset and README), `libs/ardour/ara_host.cc`, `libs/ardour/ardour/ara_host.h`, `session_utils/ara_test.cc`, `ci/ara-testplugin-build.sh`, `COPYING-GPLv3`.
- Changed:
  - `wscript`, `libs/ardour/wscript`, `session_utils/wscript`;
  - `libs/ardour/vst3_plugin.cc` and `.h`, `plugin.h`, `region_fx_plugin.cc`, `audioregion.cc` and `.h`, `session_state.cc`;
  - `gtk2_ardour/ardour_ui.cc`;
  - CI: `ci/*-build.sh`, `ci/windows-package.sh`, both workflows;
  - docs: user guide §9, DECISIONS D-034 to D-036, THIRD_PARTY.

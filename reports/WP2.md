# WP2 report: start screen, new song from WAV files, songwriter layout

*2026-11-01. MusicDAWM 0.2.0.*

## What was built

- **New song from WAV files** (D-049), in libardour (`ARDOUR::SongImport`, `libs/ardour/song_import.cc`):
  - one track per WAV file, named from the file name (`01_Guitar_L.WAV` → "Guitar L"), mono or stereo as the file;
  - each take placed by its **Broadcast WAV time stamp** (the Zoom R8 writes one per take), relative to the earliest take, to the sample, also when files have different sample rates; files without a stamp go to the start;
  - files copied into the song and converted to its sample rate (best quality) when needed; the song range set to cover the takes;
  - a summary: tracks made, files converted, files without a stamp, files that could not be read.
- **Start screen** (D-050, `gtk2_ardour/session_dialog.cc`): a **START** page first in the session dialog with *New song from WAV files…*, *Open a song…*, *Recent songs*, *Bring a song from another program* (DAWproject / Waveform / Cubase) and *New empty song*. Ardour's own pages are unchanged. The WAV song is named after the takes' folder (the project folder for an R8 `AUDIO` folder) and created without further questions; the file picker accepts files or the folder.
- **Songwriter layout** (D-051): **View > Show Recording Controls** (off by default, remembered) shows or hides the transport Record button, Punch In/Out, the Rec: mode, the Rec page button, the tracks' record-arm buttons and the mixer strips' record and In/Disk buttons.
- User guide §12 (and §2), DECISIONS D-049 to D-051, the test in both CI workflows, release notes for v0.2.0. THIRD_PARTY: nothing new (libsndfile, already bundled and listed, reads the time stamps).

## Requirement IDs

| ID | Status |
|---|---|
| WP2: start screen with the common actions | **Pass** in the program (Xvfb). |
| WP2: new song from Zoom R8 WAV files, takes aligned | **Pass** in tests and in the program; **to try on a real R8 project** (acceptance below). |
| WP2: recording controls hidden, switch in the View menu | **Pass** in the program: hidden by default, shown and hidden live, remembered after restart. |

## Tests

`session_utils/song_import_test.cc` (Linux CI, and on the Windows bundle). It writes takes as the R8 does (24-bit BWF with time stamps) into a "card" folder:

| Take | Format | Time stamp | Expected position | Result |
|---|---|---|---|---|
| `01 Guitar L.wav` | mono 48 kHz, 3 s | 10.0 s | 0 s | pass |
| `02_Guitar_R.WAV` | mono 48 kHz, 3 s | 12.0 s | 2.0 s | pass |
| `03 Vocal take.wav` | stereo 48 kHz, 2 s | 11.5 s | 1.5 s | pass |
| `04-Bass.wav` | mono **44.1 kHz**, 4 s | 10.25 s | 0.25 s, converted to 48 kHz, still 4 s | pass |
| `05 Tambourine.wav` | mono, no time stamp | – | 0 s, counted as "no stamp" | pass |
| `notes.txt` | not audio | – | ignored by the folder scan | pass |

Also checked: the time stamp is read exactly (452025 samples), the track names, 5 tracks with 1 converted and 1 without a stamp, every position to within one sample, the song range 0–5 s.

In the program (Xvfb): the START page; *New song from WAV files…* on a copy of those takes in `R8song/AUDIO`; the song "R8song" is created, the five tracks appear at their positions (screenshot checked against the table) with the summary message; reopening the song. The songwriter layout: hidden at start, **View > Show Recording Controls** brings every control back and unticking hides them again, live; the choice survives a restart.

## Found and fixed during the work

- "Click" is a name Ardour reserves (the metronome): a take called `Click.wav` gets "Click 1"-style renaming by Ardour. The test uses another name; real takes keep Ardour's normal behaviour.
- The GTK file picker opened on "Recently Used", where typing a folder or pressing *Make the song* did nothing useful; it now opens in the last takes folder (first: the home folder) and also accepts a selected folder.
- Hiding the strips' whole record/monitor row would also have hidden a bus's "sends" button, which sits there; only the track buttons are hidden.

## Known limits

- The song gets the sample rate chosen in the Audio/MIDI Setup; choose the R8's rate there to avoid converting.
- Takes with more than two channels use the first two.
- The R8's own project file (track names, faders, pan) is not read: the names come from the file names. Tell me if the R8 files are named `TRACK01.WAV`… and you would like the names from the project instead.
- Keyboard shortcuts for recording still work while the controls are hidden.

## Acceptance test (for the user)

1. Copy a real R8 project folder from the SD card to the PC.
2. Start MusicDAWM: **New song from WAV files…** → go into `AUDIO` → Ctrl+A → **Make the song** (or select the `AUDIO` folder).
3. Check: one track per take with sensible names, the takes in time with each other (play a passage where all instruments play), the summary message.
4. Check that no recording controls are visible; tick **View > Show Recording Controls** and untick it again.
5. Try **Bring a song from another program** with one DAWproject or Waveform file.

## Update 2026-11-04: files start at 0 (D-055)

Acceptance on the user's files: a Zoom R8 take (`SoloL-01.WAV`, mono 48 kHz, no bext chunk, 9.5 s of silence before the solo) and a Korg SR-1 take (`M000001_.WAV`, bext with time reference 0 and only the date and time). Neither says where it goes; the R8's `PRJDATA.ZDT` lists the file of each track but no positions. Both recorders write every file from the start of the song (confirmed by the user), so every file now starts at 0 by default; placing by BWF time stamp is an option in the file picker, and a stamp of 0 is no stamp. `song_import_test` checks both modes and the Korg case.

# WP4 report: Tap Tempo Map

*2026-10-29. MusicDAWM 0.1.0+ (part of the next release).*

## What was built

- **Core** (`libs/ardour/tap_tempo.cc`, `ardour/tap_tempo.h`), independent of the GUI:
  - **Takes:** merging, so that a new take replaces the earlier taps between its first and last tap.
  - **Cleaning:**
    - double taps dropped;
    - missed taps filled;
    - timing jitter smoothed by a curved (2nd-degree) fit over 7 taps, which follows tempo drift.
  - **Onset detection and snapping:** a small energy-based detector; each tap moves to the nearest hit within a quarter of the average tap interval (60 ms at most).
  - **Map building:**
    - one tempo point per tap, constant tempo between taps;
    - taps on beats or on bar starts, any time signature;
    - bar 1 on the first tap, through a run-in tempo and a BBT marker (D-039).
- **Window:** *Edit → Tempo → Tap Tempo Map…* (`gtk2_ardour/tap_tempo_window.cc`).
  - **Settings:** beats or bars, time signature, "Smooth my timing", and "Snap taps to the hits of" a chosen track.
  - **Tapping:** the Tap button, or the T key, which goes through the global key handler like Drum Studio's keys.
  - **Feedback:** live tap count and tempo; after Apply, a summary.
  - **Undo:** a single step ("Undo (tap tempo map)").
  - Tap times are the audible position, with playback latency compensated.
- Tests in both CI workflows; user guide §10; DECISIONS D-039.

## Requirement IDs

| ID | Status |
|---|---|
| WP4: tap along a recording made without a click; the bar grid follows the performance | **Pass** on simulated performances (below). The GUI was checked in the real program (Xvfb). **Not yet tried on a real song by the user**: that is the acceptance step. |

## Tests

`session_utils/tap_tempo_test.cc` uses a performance whose tempo drifts from 96 to 108 bpm, starting at 2.3 s.

| Check | Result |
|---|---|
| Cleaning: 1 double tap dropped, 2 missed taps filled (63 taps → 64) | pass |
| Bar 1 on the first tap (1\|1\|0) | pass |
| One tempo point per tap | pass |
| Beats, with taps jittered by 8 ms: grid against the true beats | rms 5.2 ms, max 11.5 ms (limit 15 ms): pass |
| Re-tapping a passage replaces it | pass |
| Snap: onsets found on a click track (64 of 64) | pass |
| Snap: sloppy taps (20 ms jitter) → grid on the hits | max 0.0 ms: pass |
| Bars in 6/8 (taps on bar starts, tempo drifting): meter, bar numbering, error | rms 3.8 ms, max 7.2 ms: pass |

**GUI check (Xvfb):**
- the menu entry and the window;
- 20 taps during playback, live tempo shown;
- Apply with snapping: map rebuilt in 4/4 with bar 1 on the first tap, and the summary shown;
- Edit → Undo restores the previous map;
- the T key taps.

## Found and fixed during the work

- A BBT marker must be on a whole beat. The first version put bar 1 at 3.69 beats; Ardour moved the marker's beat to 4.0 and the grid ended up 180 ms off. The run-in tempo now puts the first tap on a whole beat.
- A straight-line smoothing lagged behind tempo drift with bar taps (rms 7.6 ms); the curved fit fixed it.
- Ardour's aubio onset plug-in returned no onsets on the test click track in this build; replaced by the energy detector.
- T presses never reached the window: the global key handler sees them first. They now go through that handler, as in Drum Studio.

## Known limits

- The tempo is constant between taps; a gradual change within one beat or bar is not modelled.
- Tapping replaces the whole tempo map, including time-signature changes. Songs with several time signatures need the grid fine-tuned with Ardour's tempo tools afterwards.
- Snapping needs a track with clear attacks. On a smooth pad or a vocal it may find few hits, and then the taps stay as tapped.

## Acceptance test (for the user)

1. Open a song recorded without a click (e.g. one of the Zoom R8 songs).
2. Open *Edit → Tempo → Tap Tempo Map…*, choose "every beat" and 4/4, play the song, and tap T along for one or two minutes; stop.
3. Apply. Check that the bar lines in the editor fall on the beats you hear: turn on the grid, or play a Drum Studio part over it.
4. Try once more with "Snap taps to the hits of" set to the drum or guitar track.
5. Edit → Undo, and check that the old grid comes back.

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
- Beyond the taps, the grid now follows the chosen track's beat or keeps the tapped tempo (BACKLOG B-1, done 2026-10-31, D-046; see *Update* below). In a break with no hits it runs on the tempo trend: in the test, at most 15.6 ms off within a 4-bar break.
- Snapping needs a track with clear attacks. On a smooth pad or a vocal it may find few hits, and then the taps stay as tapped.

## Acceptance test (for the user)

1. Open a song recorded without a click (e.g. one of the Zoom R8 songs).
2. Open *Edit → Tempo → Tap Tempo Map…*, choose "every beat" and 4/4, play the song, and tap T along for one or two minutes; stop.
3. Apply. Check that the bar lines in the editor fall on the beats you hear: turn on the grid, or play a Drum Studio part over it.
4. Try once more with "Snap taps to the hits of" set to the drum or guitar track.
5. Edit → Undo, and check that the old grid comes back.

## Update 2026-10-31: extending a tapped passage (BACKLOG B-1)

- `TapTempo::extend ()`: from the taps, the beat is followed on the hits of the track chosen for snapping (each next beat predicted from the current tempo and its trend, moved onto a hit within 15 % of a beat, 25 % after a miss; the tempo follows a quarter of each error), backwards before the first tap and forwards after the last; with no track, the tempo of the nearest 16 tapped intervals continues. Stretches of 2 or more beats with no hit are reported; nothing is added before the first or after the last hit (count-in, end of the song). Before the taps only whole bars are added, so the first tap stays a bar start.
- `Options::keep_outside`: only the passage changes; the tempos before and after keep their times (the tempo just before the passage is adjusted so that whole bars end on the first tap, the one just after so that whole beats end on the next old tempo point; no BBT marker, which would move the later points by beats).
- Window: *Outside the taps:* **Extend the grid to the whole song** (default) or **Keep the tempo map outside the tapped passage**; the result lists the bars without clear hits.

| Check (64 bars of 4/4, 96 → 108 bpm, hi-hat eighths, a 4-bar break, bars 1–8 tapped) | Result |
|---|---|
| Grid over the whole song, following the drums | 256 beats; rms 2.8 ms, max 8.9 ms outside the break, 15.6 ms in it |
| The break reported | 1 stretch, covering bars 41–44 |
| No track: the tapped tempo continues (mean of the last 16 intervals) | pass |
| Keep outside: the tempos before (120 bpm) and after (140 bpm at 100 s) stay, the passage starts on a bar | pass |

## Update 2026-11-03: Detect beats and bars (from RunBPM)

- `ARDOUR::BeatDetect` (`libs/ardour/beat_detect.cc`, D-052): RunBPM's detector ported to C++: spectral-flux onset curve (~11 kHz, 1024/128), tempo by autocorrelation with a 4- then 16-beat comb and a confidence, beats by dynamic programming (Ellis 2007). For MusicDAWM: octave from the user's taps (else nearest 110 per minute, the other one offered), whole-song analysis of one or several tracks mixed (`analyse_tracks`), beats before the music dropped, the first beat of the bar guessed (bass onset + change of sound, D-053), passages without clear beats reported.
- Window: **Listen to:** (one or several audio tracks; drum-named ones ticked by default), **Detect beats and bars**, **Bar 1 one beat earlier / later**. With taps, the taps choose the octave and the first tap marks a bar start. The map is made with `TapTempo::apply` (one tempo point per beat, one undo step).
- Test `session_utils/beat_detect_test.cc` (Linux CI and the Windows bundle):

| Check | Result |
|---|---|
| Steady 120, drums + chords, two-beat pickup: tempo, beats, bar 1 | 120.00; 82 beats, rms 4.4 ms, max 9.7 ms; bar 1 on the right beat, marked sure; nothing unsure; 0.1 s for 42 s of audio |
| Taps at about 62 choose half time | 60.00, 120 offered |
| Played without a click, 96 → 108, a two-bar break | 101.9 average; rms 4.8 ms, max 9.4 ms outside the break; bar 1 right; the break reported (30.45–34.63 s for 30.44–34.62 s) |
| The same on two tracks mixed / on the drum track alone | same beats and bar 1 / rms 5.0 ms |
| The tempo map from the beats: bars 1–30 against the real bars | rms 4.2 ms, max 7.2 ms |

In the program (Xvfb, the test's session with a Drums and a Chords track): the drum track ticked by default, Detect makes the map (about 102 per minute, 130 beats, the break listed), Bar 1 one beat later moves the bar lines by one beat.

Limits: tested on synthetic songs; to try on real songs (acceptance below). Odd meters (6/8) are detected at the pulse found, so the bar lines may need the correction or taps. MIDI tracks cannot be listened to.

Acceptance: on a Zoom R8 song, tick the drums (or drums + bass + guitar), Detect, check the bar lines against what you hear and the "1" of each bar; correct with the buttons if needed; try a song with a break and one at a slow tempo (half/double).

### Improvements, 2026-11-03 (D-054)

| Case (synthetic, drums + chords unless said) | Before | After |
|---|---|---|
| Steady 120: beat error | rms 4.4 ms, max 9.7 ms | rms 2.7 ms, max 5.1 ms |
| Drift 80 → 120 over 160 beats | 129 of 160 beats within 35 ms, rms 124 ms, bar 1 wrong | 160 of 160, rms 2.5 ms, bar 1 right |
| Jump 100 → 128 | 80 of 128, rms 99 ms | 128 of 128, rms 2.6 ms |
| Loose playing (hits ±20 ms) | 115 of 120 | 114 of 120 (the beats sit on the hits) |
| Acoustic guitar alone, 92 → 100 | 119 of 120 | 119 of 120 |
| Ballad at 66, hi-hat eighths | found at 132, bar 1 wrong | 66.0, 80 of 80, bar 1 right |
| Tempo map bars 1–30 | rms 4.2 ms | rms 2.0 ms |

The local period and the sub-frame beats were also put into RunBPM (`src/core/beats.ts`); the octave rule was not (RunBPM prefers the faster octave for running).

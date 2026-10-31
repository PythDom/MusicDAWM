# MusicDAWM backlog

Work agreed with the user but deliberately left for later. Each item says what it is, why, and roughly what it takes. When an item is started it moves to a work package and its report.

| # | Item | Origin | Status |
|---|---|---|---|
| B-1 | **Tap Tempo Map: extrapolate a tapped passage to the whole song** (below) | User, 2026-10-31 | **Done** 2026-10-31 (D-046, `reports/WP4.md`) |
| B-2 | Reference matching R2–R4 (learned matching, Python/PyTorch module, ST-ITO, AFx-Rep) | D-038 | Postponed |
| B-3 | WP8: CLAP plug-ins | D-038 | Postponed |
| B-4 | Melodyne / ARA: pitch edits not heard, crashes; diagnostic plan in `reports/WP9.md` | D-037 | On hold |

## B-1. Tap Tempo Map: extrapolation to the whole song

**Today** (WP4, `TapTempo::apply`): *Apply* replaces the whole tempo map. After the last tap, the tempo of the **last** tap interval continues to the end; before the first tap a run-in tempo is used. On a song played without a click the extrapolated grid drifts away from the music (a 1–2 % drift is half a beat off after about 30 bars), and tapping one passage wipes any tempo work elsewhere.

**To make:**
1. **Steady extrapolation** (fallback): beyond the taps, continue with the average tempo (or the fitted trend) of the tapped passage rather than the last interval. About half a day with a test.
2. **Follow the music** (main): when a track is chosen in "Snap taps to the hits of", the taps give the starting tempo and phase, and MusicDAWM tracks the beat over the rest of the song on that track: predict the next beat, look for a hit near it (the existing onset detector), lock onto it so the tempo follows the drift; with no hit (break, quiet bar) keep the tempo and catch up at the next clear hit. Show where it was unsure so that only those passages need re-tapping (a new take already replaces the taps there). About 1–2 days, tested on a drifting performance like `session_utils/tap_tempo_test.cc`.
3. A choice in the window: **extend to the whole song** or **keep the map outside the tapped passage**.

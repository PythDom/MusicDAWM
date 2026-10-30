# WP7 report: Final Mix (with reference matching R1)

*2026-10-30. MusicDAWM 0.1.0+ (part of the next release). WP7 was merged with the Final Mix window (D-038); Step 3, reference matching R1, is included.*

## What was built

- **M1. Bundled plug-ins and one-click Freeze** (D-040):
  - x42-dpl (limiter, GPLv3+) and x42-fil4 (EQ with high-pass, GPLv2+), built from pinned releases without their OpenGL GUIs by `ci/x42-mix-plugins-build.sh`, shipped in `lib\ardour9\LV2` with their licences;
  - a **Freeze / Frozen** button on every audio track's mixer strip, with a one-time upgrade of saved strip settings so that v0.1.0 users see it.
- **M2. Master loudness meter:** the session measures the master bus outputs (integrated and momentary LUFS, true peak) with Ardour's `LUFSMeter`, only while the Final Mix window is open; each playback starts a new measurement.
- **M3. The Final Mix window** (*Window → Final Mix…*, `gtk2_ardour/final_mix_window.cc`), on a model in libardour (`ARDOUR::FinalMix`, `libs/ardour/final_mix.cc`, D-041):
  - one column per track, grouped by role: name and reset, pan, fader with meter and dB, FX panel (high-pass, brightness, punch, space, role), mute/solo;
  - **⊖** removes a track from this mix (muted, remembered, undoable); **+ Tracks** brings it back;
  - top bar: FX ON, Reset settings, Genre and Starting mix, Before / After / Ref, Open classic mixer; transport row;
  - master column: loudness against the target (Streaming −14, Loud −9, Custom), limiter ceiling, master fader;
  - the tools are ordinary named processors (Final Mix EQ, Punch, Space bus, Match EQ, Limiter), so the classic mixer shows them and stock Ardour opens the sessions.
- **M4. Starting mix and Randomise FX** (D-042): balance from each track's measured loudness per genre and role, pan from role and names, effects from genre × role tables; Randomise varies the effects. Each is one undo step, like Reset settings.
- **M5. Before / After / Ref and Finalise Mix** (D-043): Before at the mix's loudness, the mix restored exactly; a reference track that bypasses the master bus; Finalise exports WAV 24-bit (and MP3 with FFmpeg) normalised to the target under a true-peak ceiling.
- **Step 3, R1. Match to reference** (D-044): analysis of loudness, peaks, six bands and width; target from the reference, a gentle master EQ towards its tonal balance, and advice on width and punch; Clear match reverts.
- **M6.** User guide §11 (and the FFmpeg row in §5), THIRD_PARTY rows, DECISIONS D-040 to D-044, tests in both CI workflows, Windows build.

## Requirement IDs

| ID | Status |
|---|---|
| WP7: limiter and one-click freeze | **Pass** (tests, Windows bundle test, GUI check). |
| Final Mix window (columns by role, ⊖ / + Tracks, master loudness, target, ceiling) | **Pass** in the program (Xvfb) and in tests. |
| Starting mix per genre (Pop, Rock, Acoustic/Folk, Electronic, Hip-hop, Metal, Jazz) | **Implemented and tested**; the genre × role tables are a first guess, **to be tuned by the user's listening rounds**. |
| Before / After / Ref at matched loudness; Finalise Mix WAV + MP3 at the target | **Pass** (tests; GUI: Before/After/Ref, Finalise checked). MP3 not tried here (no FFmpeg on the test machine); it uses Ardour's own MP3 export. |
| R1: rule-based reference matching | **Pass** on synthetic songs; to try on real songs. |

## Tests

`session_utils/final_mix_test.cc` (Linux CI, and on the Windows bundle with the bundled plug-ins):

| Check | Result |
|---|---|
| Limiter: +6 dBFS in, peaks at the −1 dB ceiling | −1.00 dBFS |
| High-pass 120 Hz: 40 Hz cut, 1 kHz untouched | −19.1 dB / 0.0 dB |
| Freeze, then unfreeze restores regions and plug-ins | pass |
| Master meter: a −20 dBFS 1 kHz tone, centre-panned | −23.0 LUFS momentary, −23.2 integrated, −23.0 dBTP (expected −23.0) |
| A new playback measures afresh (6 dB quieter) | −29.3 LUFS |
| Roles guessed from names; tools made in order (EQ, Punch before the fader; send after); FX off/on; reset | pass |
| ⊖ mutes and remembers; undo, redo; earlier mute state back | pass |
| Starting mix (Rock): vocal −22, drums −24, bass −26, two guitars −29 LUFS in the mix; guitars L/R by name; vocal effects | pass, within 0.1 dB |
| Starting mix and Randomise undo / redo | pass |
| Analysis: −20 LUFS stereo 1 kHz, a 2 kHz tone in the mids band, width of a one-sided signal | pass |
| Before: faders 0 dB, master turned down to the mix's loudness; After restores everything exactly | pass |
| Finalise: WAV at −14 LUFS (target), true peak under −1 dBTP | −14.2 LUFS |
| Reference: imported, not one of the mix's tracks, not through the master bus, silent except in Ref, Ref at the mix's loudness | pass |
| R1: target from the reference, more air, width advice; Match EQ before the limiter; Clear match | pass |

In the program (Xvfb, the Mother smoke session and the test session reopened): the columns, FX panel, ⊖ and + Tracks, Freeze, Starting mix, Before/After (about 5 s for 7 tracks of 6 minutes), Ref, Match to reference with its report, and Finalise (−7.7 LUFS for a −7.5 target) were checked; everything came back after reopening the session.

## Found and fixed during the work

- Finalise on Windows: the command-line tools in `bin` did not set `VAMP_PATH` as the GUI does, so Ardour's export found no loudness analysis plug-ins and normalised blindly (a clipped file at −1.9 LUFS). The session utilities now set it, and Finalise checks the file it made: a result that misses the target by more than 1.5 LU or the ceiling by more than 0.5 dB is reported as a failure, never as a finished song.
- Loudness meter: on the Windows runner the first cycle of a playback carried garbage samples (≈1e26), which spoilt the measurement; such cycles are now skipped.
- `Playlist::region_list ()` returns a fresh copy: iterating it directly used a destroyed list (crash).
- Adding a plug-in from the FX panel rebuilt the columns and deleted the panel; panels now belong to the window.
- Track loudness was measured again at every switch (slow on long songs); it is now loudness-only and cached until the track's regions change.
- The Freeze button was hidden for users with saved v0.1.0 settings (one-time upgrade).

## Known limits

- The Starting mix tables, the punch and space amounts and the matching rules are first guesses until the user's listening rounds.
- Tracks are measured as recorded, before their own plug-ins (an amp simulator changes the real loudness); MIDI tracks (Drum Studio) keep their fader.
- Before estimates the loudness from the tracks (effects not counted); Ref uses the last mix analysis when there is one.
- Match to reference is not an Edit → Undo step (Clear match reverts it).
- MP3 needs FFmpeg installed separately (§5).

## Acceptance test (for the user)

1. On one of your songs: set the roles, remove guide tracks (⊖), pick the genre, **Starting mix**, listen with **Before / After**; say per genre and role what sounds off.
2. **Reference…** a finished song in that style, **Ref** to compare, **Match to reference**, read the advice, listen again.
3. **Finalise Mix…**, then play the WAV (and MP3, with FFmpeg) in another player.

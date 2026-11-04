# Bass Studio report

*2026-11-04. Part of the next release (D-058).*

## What was built

- `ARDOUR::BassStudio` (`libs/ardour/bass_studio.cc`): the chord chart parser; 11 styles × 4 complexities (Root notes, Rock / pop eighths, Country / two-feel, Disco octaves, Jazz walking, Reggae one-drop, Funk sixteenths, Ballad, Blues shuffle, Bossa nova, Follow the kick drum); the feel (normal, half-time, double-time); the link to Drum Studio (complexity per bar from the drum parts; kicks from the drum track); the bass track (ACE Fluid Synth, GeneralUser GS, program change in the line); the line as one editable MIDI region, remade in one undo step; the preview region.
- `BassStudioPanel` (`gtk2_ardour/bass_studio_panel.cc`): **Window → Bass Studio**, a page of the Editor sidebar: chords, start bar, style, complexity, feel, sound, *Follow the drum parts*, **Listen**, **Make Bass Line**.
- User guide §6 (*Bass Studio*), DECISIONS D-058, the test in both CI workflows.

## Tests

`session_utils/bass_studio_test.cc` (Linux CI with the soundfont; the Windows bundle requiring it):

| Check | Result |
|---|---|
| Parser: 13 bars with comments, `|`, new lines, slash chord, two chords in a bar, `%`, `N.C.`, maj7, m7b5, sus4, dim7, aug | pass |
| A wrong chord is named with its bar; an empty chart is refused | pass |
| Rock eighths on C G Am F: 32 eighths, roots C2 G1 A1 F1, the downbeat accented, notes separated | pass |
| Jazz walking C → F: C E G, then E1 (a semitone below F1) | pass |
| C/E: a whole note E1; half-time and double-time; a 3/4 bar; follow the kick; no bass in an N.C. bar | pass |
| In a session: a Bass track played by Fluid Synth; one region *Bass line*, 4 bars, 16 editable notes, program change 33 (Finger Bass) | pass |
| A remake (5 bars, complexity 2) replaces it; undo brings the first back; chart and options saved | pass |
| With a Drum Studio track and cue markers (verse 1 from bar 1, chorus 4 from bar 3): complexity per bar 1 1 4 4; the kicks of the grooves | pass |
| Preview region of the first 2 bars | pass |

In the program (Xvfb): the page, Listen (the auditioner plays, Audition lit), Make Bass Line (a Bass track with the *Bass line* region, Finger Bass, the soundfont loaded in Fluid Synth).

## Known limits

- The patterns are first versions, to be tuned by listening (as the Drum Studio grooves).
- One meter for the whole chart (the meter at the start bar).
- The bass sound can be changed per line; a song with two different basses needs two tracks (move the region).

## Acceptance test (for the user)

1. On a song with a Drum Studio track: type the chords, Listen in a few styles, Make Bass Line.
2. Play the song: check that the bass follows the chords and, with *Follow the drum parts*, gets busier in the chorus.
3. Change one note in the region; Make Bass Line again; Undo.

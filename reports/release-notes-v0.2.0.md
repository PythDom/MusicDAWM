# MusicDAWM 0.2.0 (preview)

Second preview of MusicDAWM, a songwriting DAW based on Ardour 9.8, for Windows x64.

## Download and start

1. Download `MusicDAWM-0.2.0-win64.zip` below.
2. Before unpacking: right-click the zip → **Properties** → tick **Unblock** → OK. The program is not code-signed.
   On Windows 11, if **Smart App Control** is On, the program will not start at all (user guide §1).
3. Unpack it next to the old version and start `bin\MusicDAWM.exe`. Settings (`%LOCALAPPDATA%\MusicDAWM9`) and songs are kept; delete the old folder when the new one works.

The user guide (`docs/user-guide.md` in the repository) covers setup and each feature.

## New since 0.1.0

| Work package | What you get | User guide |
|---|---|---|
| WP2 | **Start screen**: new song from WAV files, open, recent songs, bring a song from DAWproject / Waveform / Cubase, new empty song. **New song from WAV files**: the takes of a Zoom R8 project become one track each, named from the files and placed where they were recorded (Broadcast WAV time stamp). **Songwriter layout**: recording controls hidden, **View > Show Recording Controls** brings them back. | §12 |
| WP4 | **Tap Tempo Map**: tap along to a song recorded without a click and get a tempo map that follows it; extend the tapped passage to the whole song (beat-following) or keep the tempo elsewhere. | §10 |
| WP7 | **Final Mix window** (*Window → Final Mix…*): one column per track grouped by role, vertical faders with meters, high-pass, brightness, punch and space per track, ⊖ to leave a track out of the mix; **Starting mix** per genre and **Randomise FX**; master loudness (LUFS, true peak) against a target with a limiter; **Before / After / Ref** at matched loudness; **Finalise Mix** to WAV (and MP3 with FFmpeg) at the target. **One-click Freeze** on every audio track. Bundled **x42 limiter and EQ**. | §11 |
| R1 | **Match to reference**: compare with a finished song in the same style and get its loudness, a gentle tonal match and advice on width and punch. | §11 |
| R5 | **Vocal presets** (EQ, compression, reverb), about ten, translated from the DiffVox research presets (MIT). | §11 |

Fixes:
- Loudness normalisation and Ardour's loudness analysis were missing from the 0.1.0 zip (the Vamp analysis plug-ins were not where the program looks for them).
- No sound while the Final Mix window was open; a crash at closing or reopening a song after using the Final Mix window or a plug-in whose saved state file was missing.

## Known issues

- **Melodyne / ARA is on hold** (unchanged from 0.1.0: tested only with the ARA SDK's test plug-in).
- The Starting mix tables, the vocal presets' choice and names and the matching rules are first versions, to be tuned by listening.
- New song from WAV files has been tested with files written like the R8's, not yet with a real R8 project.
- MP3 export needs FFmpeg installed separately.
- Waveform: plug-ins, sends, clip pan, time-stretch and loops are listed in the import summary, not converted. Cubase's own `.cpr` files cannot be read; export DAWproject from Cubase 14.0.20 or later.

## Licence

MusicDAWM is a modified version of Ardour. Because this build includes the ARA SDK (Apache-2.0), the program is distributed under the **GNU GPL version 3**. The source remains "GPL version 2 or later". Licence texts are in the `licences` folder of the zip. Third-party components are listed in `THIRD_PARTY.md`.

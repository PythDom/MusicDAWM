# MusicDAWM 0.1.0 (preview)

First preview of MusicDAWM, a songwriting DAW based on Ardour 9.8, for Windows x64.

## Download and start

1. Download `MusicDAWM-0.1.0-win64.zip` below.
2. Before unpacking: right-click the zip → **Properties** → tick **Unblock** → OK. The program is not code-signed.
   On Windows 11, if **Smart App Control** is On, the program will not start at all (user guide §1).
3. Unpack it anywhere and start `bin\MusicDAWM.exe`. Nothing is installed; settings live in `%LOCALAPPDATA%\MusicDAWM9`.

The user guide (`docs/user-guide.md` in the repository) covers setup and each feature.

## What's in it

| Work package | What you get | User guide |
|---|---|---|
| WP1 | MusicDAWM name and icon, portable folder, release process | §1–4 |
| WP3 | **Drum Studio**: a drum track with styles and parts, played live from the keyboard; record a performance, adjust it, flatten it to MIDI. AVL drum kits included. | §6 |
| WP5 | **Import DAWproject** files (Bitwig, Studio One, Cubase 14, …): tracks, clips, fades, gain, pan, mute, solo, tempo, markers, MIDI | §7 |
| WP6 | **Import Waveform projects** (11 and earlier) and convert them to DAWproject; a guided route for **Cubase** | §8 |
| WP9 | **Melodyne through ARA**: add Melodyne (or another VST3 ARA plug-in) to a clip as a Region Effect; its edits follow moves, trims and splits and are saved with the session | §9 |

## Known issues

- **Melodyne has not been tried yet.** ARA hosting is tested with the ARA SDK's test plug-in only. Please report what you see; the test steps are in `reports/WP9.md`.
- **The program's windows have not been opened on Windows by the automated tests**, which run without a screen. This is the first build to look at on a real PC.
- Copies of a clip share Melodyne's edits with the original.
- Waveform: plug-ins, sends, clip pan, time-stretch and loops are listed in the import summary, not converted. Cubase's own `.cpr` files cannot be read; export DAWproject from Cubase 14.0.20 or later.

## Licence

MusicDAWM is a modified version of Ardour. Because this build includes the ARA SDK (Apache-2.0), the program is distributed under the **GNU GPL version 3**. The source remains "GPL version 2 or later". Licence texts are in the `licences` folder of the zip.

# MusicDAWM user guide

MusicDAWM is a songwriting and arranging DAW based on [Ardour](https://ardour.org) 9.8.
This guide covers only what is **specific to MusicDAWM**. Everything that works as in Ardour (editing, mixing, automation, MIDI, export and so on) is described in the [Ardour manual](https://manual.ardour.org).

*Status: 2026-10-23. Phase 0 build; work package WP1 (branding, portable package) has not started.*

> **What exists today.** This build is Ardour 9.8 with the changes listed in §4, packaged as a portable Windows folder. The MusicDAWM features from the project brief (Drum Studio, Tap Tempo Map, DAWproject import, one-click freeze, limiter, CLAP, Melodyne via ARA) are **not built yet**. §6 lists them so you know what is coming; this guide will describe each one when it ships.

## 1. Getting the program

MusicDAWM is a portable folder: there is no installer, and nothing is written outside the folder except your settings (§3).

1. Open the repository on GitHub → **Actions** → **Windows build** → the latest green run.
2. Under **Artifacts**, download `musicdawm-windows-x64-portable` (about 120 MB zipped, 450 MB unpacked). You must be signed in to GitHub.
3. **Before unzipping:** right-click the zip → **Properties** → tick **Unblock** → **OK**. This stops the "Windows protected your PC" warning on every program inside. (In PowerShell: `Unblock-File .\musicdawm-windows-x64-portable.zip`.)
4. Unzip to a folder of your choice, for example `C:\MusicDAWM`. Keep the folder structure as it is: the program finds its files relative to the `bin` folder.

**Smart App Control.** On Windows 11, Smart App Control blocks unsigned programs with no "Run anyway" button, and MusicDAWM is not signed. Check it under **Windows Security → App & browser control → Smart App Control**. If it is **On**, MusicDAWM will not start. Turning it off cannot be undone without resetting Windows, so decide deliberately. When it is **Off** or **Evaluation**, the Unblock step above is enough.

## 2. Starting and setting up audio

Start `bin\ardour-<version>.exe`, for example `bin\ardour-9.8.23.exe`. The number after 9.8 counts MusicDAWM's own changes; it changes with every build. Until WP1 the window title and program name still say "Ardour".

On the first start you get Ardour's welcome dialog (GUI scaling, then a few preferences). Then comes the **Audio/MIDI Setup** dialog:

| Setting | Choose |
|---|---|
| Audio System | **PortAudio (default)** |
| Driver | **Windows WASAPI**. If your device does not show up there, try **MME**. DirectSound and WDM-KS are also available. |
| Output Device | your speakers or headphones |
| Input Device | anything, or none: MusicDAWM does not record |
| Sample Rate | the rate of your recordings, usually **44.1 kHz** or **48 kHz** for Zoom R8 files |
| Buffer Size | 1024 samples is fine. You never play live through the DAW, so low latency does not matter. |

Then click **Start**.

**No ASIO.** This build has no ASIO driver (Ardour's official Windows build can have one). For playback-only work WASAPI is the better choice anyway.

## 3. Where settings and sessions live

- **Settings:** `%LOCALAPPDATA%\Ardour9` (for example `C:\Users\<you>\AppData\Local\Ardour9`). **If stock Ardour 9 is also installed, both programs share this folder** until WP1 gives MusicDAWM its own name. To start over with default settings, close the program and rename or delete that folder.
- **Sessions:** wherever you create them; a session is a folder with a `.ardour` file. Sessions are ordinary Ardour 9 sessions: they open in stock Ardour 9.8, and stock Ardour 9.8 sessions open in MusicDAWM.

## 4. How this build differs from Ardour 9.8

| Area | MusicDAWM | Stock Ardour 9.8 for Windows |
|---|---|---|
| Network | **Never contacts the internet.** The start-up announcement and update check is compiled out. | Checks ardour.org for news and updates. |
| Packaging | Portable folder (zip) | Installer |
| Audio drivers | PortAudio: WASAPI, MME, DirectSound, WDM-KS. **No ASIO.** | Same, plus ASIO in official builds |
| Plugin formats | VST3, VST2 and LV2. CLAP is planned (§6). | VST3, VST2, LV2 |
| Bundled plugins | Ardour's own ACE plugins only: compressor (a-comp), delay, EQ, expander, reverb, FluidSynth player, Reasonable Synth | Also bundles third-party plugins: Harrison channel strip, several x42 plugins (including the AVL drum kit and a limiter) and a General MIDI synth |
| Video | No video tools | Bundles the video helpers (harvid, xjadeo) |
| Fonts | Uses fontconfig to draw text (see §5) | Uses the Windows font system |
| Bug fixes | Correct peak meters and waveform overviews on CPUs with AVX-512 (Ardour 9.8 can show a wrong minimum or maximum there) | Not fixed in 9.8 |

The missing third-party plugins are a packaging choice for now, not a limitation of the program; WP3 adds the AVL drum kit and WP7 adds a limiter.

## 5. Troubleshooting

| Problem | What to do |
|---|---|
| "Windows protected your PC" | Click **More info → Run anyway**, or unblock the zip and unzip again (§1). |
| The program does not start at all, no message | Check Smart App Control (§1). |
| No sound | In the Audio/MIDI Setup dialog, try the **MME** driver instead of WASAPI, and check that the sample rate matches your device's setting in Windows Sound settings. |
| Text or button labels look garbled or empty | MusicDAWM draws text through fontconfig. That was tested under Wine but not yet on real Windows. To try the Windows font system instead, start the program from a command prompt with `set PANGOCAIRO_BACKEND=win32` and then `bin\ardour-<version>.exe`, and report which one looks right. |
| A plugin crashes the program on start | Use **Safe Mode**: tick "Safe Mode: Disable all Plugins" in the session dialog, open the session, then remove the plugin. |

**Reporting a problem:** open an issue in the MusicDAWM repository with what you did, what you expected, and what happened. Please do not report MusicDAWM problems to the Ardour project: Ardour's developers cannot accept MusicDAWM's code (see `DECISIONS.md` D-010), and a problem may come from our changes rather than theirs. A problem you can also reproduce in stock Ardour 9.8 is fine to report to them in your own words.

## 6. Coming next

None of these are available yet. Each work package adds its section to this guide when it ships.

| Feature | What it will do for you | Work package |
|---|---|---|
| MusicDAWM name and icon | Own name, own settings folder (no sharing with Ardour) | WP1 |
| Songwriter layout and start screen | Recording controls hidden; "New song from WAV files" imports your Zoom R8 tracks aligned in one step | WP2 |
| Drum Studio | Pick a style, get intro/verse/chorus/fill/ending grooves in 4 variations, played by a realistic drum kit with nothing to buy. Switch variations live with the keys 1–4 and I/V/C/B/F/E while the guitar plays, then replay, adjust and turn the result into editable MIDI. | WP3 |
| Tap Tempo Map | Tap a key along with a recording played without a click, and the bar grid follows the performance | WP4 |
| DAWproject import | Open projects exported from Cubase (and other DAWs) | WP5 |
| Waveform 11 and Cubase import | Menu entries that bring your old projects across | WP6 |
| Limiter and one-click freeze | A master limiter; freeze a track's plugins with one click and unfreeze later | WP7 |
| CLAP plugins | Load CLAP plugins such as Surge XT | WP8 |
| Melodyne via ARA | Open a clip in Melodyne essential directly; corrections follow when you move or trim the clip | WP9 |

**Melodyne today:** Melodyne loads in Ardour as a normal VST3 plugin, and that should work in this build too (not yet tested). Without ARA you use Melodyne's **Transfer** mode: insert Melodyne on the vocal track, press Transfer in Melodyne, play the section so Melodyne records it, then edit. Melodyne essential edits monophonic material only: vocals and single-note lines, not chords.

## Appendix: command-line tools in `bin`

The folder also contains Ardour's command-line session tools (`ardour9-new_session.exe`, `ardour9-new_empty_session.exe`, `ardour9-export.exe`, `ardour9-lua.exe`), which work as in Ardour.

`ardour9-mother_smoke.exe` is MusicDAWM's own self-test. It builds a test session from seven WAV files with the exact track names of the "Mother" reference project, checks it, saves it and reopens it. It is meant for the build pipeline, not for your songs: it only accepts the generated test files from `tools/mother-smoke/make_wavs.py`.

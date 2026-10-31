# MusicDAWM user guide

MusicDAWM is a songwriting and arranging DAW based on [Ardour](https://ardour.org) 9.8.
This guide covers only what is **specific to MusicDAWM**. Everything that works as in Ardour (editing, mixing, automation, MIDI, export and so on) is described in the [Ardour manual](https://manual.ardour.org).

*Status: 2026-10-24, MusicDAWM 0.1.0 (work packages WP1, WP3, WP5, WP6, WP9).*

> **What exists today.** MusicDAWM is Ardour 9.8 under its own name (§4), packaged as a portable Windows folder, with Drum Studio (§6), DAWproject import (§7), Waveform and Cubase migration (§8), Melodyne through ARA (§9, on hold: see there) the Tap Tempo Map (§10) and the Final Mix window (§11). §12 lists what is still to come.

## 1. Getting the program

MusicDAWM is a portable folder: there is no installer, and nothing is written outside the folder except your settings (§3).

1. Download `MusicDAWM-<version>-win64.zip` (about 210 MB) from the repository's **Releases** page. For a build between releases: **Actions** → **Windows build** → the latest green run → **Artifacts** → `MusicDAWM-<version>-win64` (GitHub wraps it in a second zip; unpack that once). You must be signed in to GitHub.
2. **Before unzipping:** right-click the zip → **Properties** → tick **Unblock** → **OK**. This stops the "Windows protected your PC" warning on every program inside. (In PowerShell: `Unblock-File .\MusicDAWM-0.1.0-win64.zip`.)
3. Unzip to a folder of your choice, for example `C:\`. You get a folder `MusicDAWM-<version>-win64`. Keep its structure as it is: the program finds its files relative to the `bin` folder.
4. To update, unzip the new version next to the old one. Settings and sessions are not inside the folder (§3), so nothing is lost; delete the old folder when the new one works.

**Smart App Control.** On Windows 11, Smart App Control blocks unsigned programs with no "Run anyway" button, and MusicDAWM is not signed. Check it under **Windows Security → App & browser control → Smart App Control**. If it is **On**, MusicDAWM will not start. Turning it off cannot be undone without resetting Windows, so decide deliberately. When it is **Off** or **Evaluation**, the Unblock step above is enough.

## 2. Starting and setting up audio

Start **`bin\MusicDAWM.exe`**. You can make a shortcut to it (right-click → Send to → Desktop). **Help → About** shows the MusicDAWM version and the Ardour version it is based on.

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

- **Settings:** `%LOCALAPPDATA%\MusicDAWM9` (for example `C:\Users\<you>\AppData\Local\MusicDAWM9`), separate from stock Ardour's `Ardour9` folder. To start over with default settings, close the program and rename or delete that folder.
- **Sessions:** wherever you create them; a session is a folder with a `.ardour` file. Sessions are ordinary Ardour 9 sessions: they open in stock Ardour 9.8, and stock Ardour 9.8 sessions open in MusicDAWM.

## 4. How this build differs from Ardour 9.8

| Area | MusicDAWM | Stock Ardour 9.8 for Windows |
|---|---|---|
| Name, icon, settings | MusicDAWM, own icon and splash screen, own settings folder `MusicDAWM9` | Ardour, `Ardour9` |
| Network | **Never contacts the internet on its own.** The start-up announcement and update check is compiled out. Features you start yourself (for example the Freesound search in the import dialog) still go online. | Checks ardour.org for news and updates. |
| Crash reports | None: no crash reporter is built in, nothing is sent anywhere | Official builds can write crash dumps |
| Help → Report a Bug | Opens MusicDAWM's issue page on GitHub | Opens Ardour's bug tracker |
| Packaging | Portable folder (zip), releases on GitHub | Installer |
| Audio drivers | PortAudio: WASAPI, MME, DirectSound, WDM-KS. **No ASIO.** | Same, plus ASIO in official builds |
| Plugin formats | VST3, VST2 and LV2. CLAP is planned (§6). | VST3, VST2, LV2 |
| Bundled plugins | Ardour's own ACE plugins (compressor, delay, EQ, expander, reverb, FluidSynth player, Reasonable Synth) and the **AVL drum kits** (x42-avldrums) for Drum Studio | Also bundles other third-party plugins: Harrison channel strip, several x42 plugins (including the AVL drum kit and a limiter) and a General MIDI synth |
| Video | No video tools | Bundles the video helpers (harvid, xjadeo) |
| Fonts | Uses fontconfig to draw text (see §5) | Uses the Windows font system |
| Bug fixes | Correct peak meters and waveform overviews on CPUs with AVX-512 (Ardour 9.8 can show a wrong minimum or maximum there) | Not fixed in 9.8 |

The missing third-party plugins are a packaging choice for now, not a limitation of the program; WP7 adds a limiter.

## 5. Troubleshooting

| Problem | What to do |
|---|---|
| "Windows protected your PC" | Click **More info → Run anyway**, or unblock the zip and unzip again (§1). |
| The program does not start at all, no message | Check Smart App Control (§1). |
| No sound | In the Audio/MIDI Setup dialog, try the **MME** driver instead of WASAPI, and check that the sample rate matches your device's setting in Windows Sound settings. |
| Text or button labels look garbled or empty | MusicDAWM draws text through fontconfig. That was tested under Wine but not yet on real Windows. To try the Windows font system instead, start the program from a command prompt with `set PANGOCAIRO_BACKEND=win32` and then `bin\MusicDAWM.exe`, and report which one looks right. |
| After an export, a window "Support Ardour Development" appears | That is Ardour's own request for donations: MusicDAWM is built on Ardour's work. Choose "Never ever ask me about this again" if you don't want to see it again. Nothing is sent anywhere. |
| No MP3 option (Finalise Mix, export, reference songs) | MP3 needs the free FFmpeg program, which MusicDAWM does not ship. Download a Windows build of FFmpeg ("release essentials" from gyan.dev, for example) and copy `ffmpeg.exe` and `ffprobe.exe` (from its `bin` folder) directly into `C:\Program Files\ffmpeg\`; then restart MusicDAWM. WAV always works. |
| A plugin crashes the program on start | Use **Safe Mode**: tick "Safe Mode: Disable all Plugins" in the session dialog, open the session, then remove the plugin. |

**Reporting a problem:** use **Help → Report a Bug**, which opens the MusicDAWM issue page, and describe what you did, what you expected, and what happened. Please do not report MusicDAWM problems to the Ardour project: Ardour's developers cannot accept MusicDAWM's code (see `DECISIONS.md` D-010), and a problem may come from our changes rather than theirs. A problem you can also reproduce in stock Ardour 9.8 is fine to report to them in your own words.

## 6. Drum Studio

Drum Studio gives a song a drummer: pick a style, then play the song parts live while your guitar track plays, the way you would call out parts to a drummer. Everything you play is recorded on the timeline, plays back the same way, and can be turned into ordinary MIDI that you edit note by note.

**Open it:** **Window → Drum Studio**. It is a page of the Editor's sidebar (the list on the right, next to Regions and Ranges & Marks), so the timeline stays in view.

### Make the drum track

1. **Style:** Rock, Pop, Ballad, Blues shuffle, Funk, Country, Reggae, Bossa nova, Jazz swing, Half-time, 6/8 or Punk.
2. **Kit:** one of the bundled AVL acoustic kits (Black Pearl, Red Zeppelin, Blonde Bop, Buskman's Holiday), or **No instrument** to use your own drum plugin, such as MT Power DrumKit. The grooves are plain General MIDI drum notes, so any GM drum instrument plays them.
3. **Create Drum Track.** You get a MIDI track called *Drums* with the kit and all 28 grooves loaded: 7 parts (intro, verse, pre-chorus, chorus, bridge, fill, ending) × 4 complexities (1 = simple, 4 = busy). If the style is in 6/8 and the song has a single time signature, the song is switched to 6/8 (undo reverts it). The style's suggested tempo is shown; your song's tempo is not changed.

To change style later, pick another style and press **Load Style**: the drum track gets the new grooves.

### Perform

- **Click a cell** to start or switch. With the transport stopped, a click starts playback at the playhead with that groove, so you can audition each cell at your song's tempo.
- While playing, a click switches at the next **Switch at** point: *Next beat*, *Next bar* (default) or *Next 2 bars* (two-bar phrases start on odd bars). The waiting cell is lit; the playing cell is red.
- **Keys:** turn on **Keys** to play with the keyboard. **I** intro, **V** verse, **P** pre-chorus, **C** chorus, **B** bridge, **F** fill, **E** ending; **1–4** change the complexity of the current part. A part key keeps the last complexity. While Keys is on these keys switch the drums instead of doing their usual Editor shortcuts. Turn it off, or switch the sidebar to another page, to get the shortcuts back.
- A **fill** plays for one bar, then the part you were in comes back by itself. The **ending** plays once.
- **Stop** silences the drums at once; the transport keeps rolling.

### Record, replay, adjust

1. Turn on **Record** (it is Ardour's *Rec Cues*), put the playhead at the start and play the song, switching parts as you go. Stop the transport: every switch is now a cue marker in the *Location Markers* ruler, on the beat or bar where it happened. Record turns itself off at the stop.
2. Play from the start: the drums replay your switches.
3. Adjust without re-playing: **drag** a marker to move a switch, or delete it. To redo a section, arm Record again and play over it: only the markers in the range you play are replaced.

### Flatten to MIDI

**Flatten** turns the recorded switches into MIDI regions on the drum track, named for example *Rock Verse 2*. Each region has its own copy of the notes, so deleting one kick in one verse leaves the other verses alone. Flatten also turns **Play Cues** off so the cue markers no longer play the drums a second time. Undo (Ctrl+Z) removes the regions; turning Play Cues back on lets the markers play again.

After flattening, edit the drums like any MIDI: double-click a region to edit notes, or use Ardour's MIDI tools. Exports include them.

### Good to know

- Drum tracks start at −6 dB, because the AVL kits are loud on accented crashes. The track monitors **In** and **Disk** together, so it plays the regions, the cues and a MIDI drum pad you connect.
- Switches are timed by the program's display loop. If the program freezes for longer than a beat right before a switch (for example while a plugin window opens), that switch can land one beat late; drag its marker into place.
- A session with a drum track is an ordinary Ardour session plus a few extra settings that stock Ardour ignores, so it should open in stock Ardour 9.8 without the Drum Studio page. This has not been tested yet.

## 7. Importing a project from another DAW (DAWproject)

DAWproject is an open exchange format that Cubase 14, Studio One 6.5, Bitwig Studio 5 and others can export. MusicDAWM reads it.

1. In the other program, export the song as a **.dawproject** file (Cubase: *File → Export → DAWproject*).
2. In MusicDAWM, create a new, empty session at the sample rate you want (audio files with another rate are converted).
3. **Session → Import DAWproject…**, pick the file, **Import**. A summary lists what came across and what did not.

**What comes across**

- Tempo and time signature, including changes (a tempo *ramp* becomes a step at its start).
- Markers.
- Audio and MIDI tracks with their names and colours. Folder tracks are flattened: their tracks are imported one by one. Group and effect channels become busses, with each track's output routed to its group and its sends (level, pre/post) to the effect busses.
- Volume, pan, mute and solo; automation of volume, pan, mute and send levels.
- Audio clips: position, which part of the file plays, fades, loops (as repeated regions), disabled clips (as muted regions), clip names.
- MIDI clips with their notes (time, length, velocity, channel).

**What does not (yet), and is listed in the summary**

- Plug-ins and their settings, including the other program's built-in EQs and compressors. Insert them again by hand, using the summary as a list.
- Time-stretched audio plays at its original speed; the summary says which clip and by how much it was stretched.
- Note expressions, crossfade shapes, send pan, VCAs, clip-launcher scenes, video.

The import adds to the open session and cannot be undone in one step. If you don't like the result, close the session without saving.

## 8. Bringing your Waveform and Cubase songs across

**Waveform (11 and earlier): Session → Import Waveform Project…**

1. Create a new, empty session in MusicDAWM.
2. Pick the song's **edit file** (`<name> Edit 1.tracktionedit`, in the Waveform project folder). Keep the folder as Waveform left it: MusicDAWM finds the audio through the project file (`<name>.tracktion`) next to the edit, or by file name in that folder if the files were moved.
3. You get a summary like the DAWproject import.

What comes across: tracks (named after their audio files, since Waveform does not store default names like "Track 1"; mono files give mono tracks), clip positions, trims, clip gain, fades, faders, pan, mute, solo, master level, tempo and time signature, markers, MIDI clips. Plug-ins, aux sends, clip pan, time-stretch and loops are listed in the summary. Waveform 14 has no `.tracktion` file and can export DAWproject itself; use **Import DAWproject** for it.

**Session → Convert Waveform Project to DAWproject…** writes the same song as a `.dawproject` file with the audio inside, to open in other programs (Cubase 14, Studio One, Bitwig) or to keep as an archive.

**Cubase: Session → Import Cubase Project…** explains the one step needed in Cubase: Cubase's own `.cpr` files cannot be read (the format is not published), so export the project from Cubase 14.0.20 or later as DAWproject (*File → Export → DAWproject*), then pick that file. Older Cubase versions cannot export DAWproject; export the tracks as audio files instead and import them with *Import Audio/MIDI files*.

## 9. Melodyne and other ARA plug-ins

ARA lets a plug-in such as Melodyne work on a clip directly: it sees the whole clip, not just what passes through it while the song plays, so there is no "Transfer" step.

> **On hold (October 2026).** Tried with Melodyne essential: the clip opens in Melodyne and deleted notes are silent, but **pitch changes are not heard** and Melodyne **crashes often**. Until this is fixed, use Melodyne as a normal plug-in on the track (Transfer mode). Details: `reports/WP9.md`.

**Open a clip in Melodyne**

1. Double-click the clip (region) to open its **region editor**.
2. In the **Region Effects** box, right-click → **New Plugin** and pick Melodyne (VST3).
3. Double-click Melodyne in the box to open its window. It shows this clip, already analysed (the analysis takes a few seconds the first time).
4. Edit as usual. You hear the result straight away when you play the song.

**What follows the clip**

- Move it, trim either end, or rename it: Melodyne's edits stay on the right notes.
- Split it: both pieces keep the edits. Copies (duplicate, copy/paste) share them too, so editing one copy changes the others. Separate edits per copy are not possible yet.
- Clip gain and the gain envelope apply after Melodyne, as you would expect. Fades too.
- Tempo and time-signature changes reach Melodyne (its grid follows the session).

**Saving:** Melodyne's edits are saved with the session, in the session folder under `plugins\ara-….dat` (one file per snapshot). Keep that file with the session; copying the whole session folder is enough. Without it the clips open in Melodyne unedited.

**Good to know**

- ARA works when Melodyne is a **Region Effect**. As a normal track plug-in (mixer strip) it works as before, without ARA (Transfer mode).
- Melodyne essential edits monophonic material only: vocals and single-note lines, not chords.
- Other ARA plug-ins in VST3 format go through the same path, but none has been tried. Only VST3 is supported (no CLAP ARA yet).
- **Licence:** because the ARA library is Apache-2.0, the Windows program is distributed under the **GNU GPL version 3** (the source stays "GPL version 2 or later"). The licence texts are in the `licences` folder.

## 10. Tap Tempo Map

For songs recorded without a click (like your Zoom R8 sessions): tap along while the song plays, and the bar grid follows the performance. Drum Studio parts, MIDI and anything snapped to the grid then line up with the music.

**Edit → Tempo → Tap Tempo Map…**

1. Choose what you tap on: **every beat** or **every bar start**, and the **time signature** (for example 4/4, 3/4 or 6/8).
2. Start playback (Space) from a little before the first beat, and **press T** (or click **Tap**) in time with the song. The **first tap starts a bar** (bar 1 when you tap from the beginning), so start tapping on a downbeat; a count-in or silence before it is fine. You do not have to tap the whole song (see *Outside the taps* below).
3. Stop playback when you are done. Each time the song plays is a **take**: to fix a passage, play it again and tap only that passage; the new taps replace the old ones there.
4. Click **Apply to the tempo map**. The window says how many taps were used, how many double taps were ignored and how many missed taps were filled in.

Options:
- **Smooth my timing** (on by default) evens out small timing wobble in your taps while following the real tempo changes of the performance.
- **Snap taps to the hits of:** pick a track with clear attacks (drums, a strummed guitar). Each tap then moves onto the nearest hit, so the grid lands exactly on the beat even if your tapping was a little loose. Taps far from any hit stay where they were.

**Outside the taps** — what happens before your first and after your last tap:
- **Extend the grid to the whole song** (default):
  - If a track is chosen in **Snap taps to the hits of**, MusicDAWM **follows that track's beat** before and after your taps. It starts from the tempo and timing you tapped, moves each beat onto the hit it finds there, and lets the tempo follow the band as it speeds up or slows down. So you can tap 8 bars over the drums and get a grid for the whole song.
  - Where it finds no clear hits for two beats or more (a break, a quiet verse), it carries on at the tempo trend and catches up at the next hits. The window lists those bars ("No clear hits in bars 41–44") so you can check the grid there, or play and tap just those bars again.
  - With no track chosen, the grid continues at the tempo of your last (and, before the taps, first) 16 taps.
- **Keep the tempo map outside the tapped passage**: only the tapped passage changes; the tempo map before and after it stays where it was in time, and the bar numbers carry on. The tempo just before and just after the passage is adjusted slightly so that the passage starts on a bar and the later tempo changes stay in place. The time signature is kept as it was.

What changes and what does not:
- **Audio stays exactly where it was recorded.** Only the bar grid moves.
- MIDI and Drum Studio parts follow bars and beats, so they move with the new grid.
- With **Extend the grid to the whole song**, the whole tempo map is replaced (tempo and time signature). **Edit → Undo** brings back the previous map in one step, in either mode.
- The grid can still be fine-tuned afterwards with Ardour's own tempo tools (drag the tempo markers).

Good to know: tap times are taken from what you hear (the playback latency is compensated), so tapping is as accurate as your timing; with **Snap** it is as accurate as the hits on the chosen track.

## 11. Mixing: the Final Mix window, Freeze, limiter and EQ

### The Final Mix window

**Window → Final Mix…** shows the song as a simple mixing desk: one column per track, grouped by role (Drums, Bass, Guitars, Keys, Vocals, Backing vocals, Other). The role is guessed from the track name ("Kick", "Bass DI", "Lead Vox", …) and can be changed in the track's FX panel.

Each column, from top to bottom:
- **Name** and **↺** (reset: no effects, fader at 0 dB, pan in the centre).
- **Pan** knob (left – centre – right; double-click returns to the centre).
- **Fader**: the same vertical fader and level meter as in the classic mixer. Drag it up or down; the box above it shows the level in dB and accepts a typed value (the box next to it is the peak level; click it to reset).
- **FX**: opens the track's panel:
  - **High-pass**: removes the low end the track does not need (rumble, mud). The frequency starts at a value that suits the role (for example 100 Hz for a vocal, 35 Hz for a bass).
  - **Brightness**: a gentle lift (or cut) of the highs above about 6 kHz.
  - **Punch**: compression that evens out the level and keeps the attacks.
  - **Space**: how much of the track goes to a shared reverb.
  - **Role**: moves the column to another group.
- **M** / **S**: mute and solo.
- **⊖**: removes the track from this mix. Its column disappears and the track is muted (not heard while you mix, and not in the export). **+ Tracks** at the bottom lists the removed tracks and brings one back, with the mute state it had before. **Edit → Undo** works too. The choice is saved with the session.

At the top: **FX ON** switches all these effects off and on, to compare (the settings are kept); **Reset settings** resets every track; **Open classic mixer** shows the full mixer. Below, ⏮ and ▶/■ move to the start and play or stop (Space works too).

**Starting mix.** Choose the **Genre** (Pop, Rock, Acoustic / Folk, Electronic, Hip-hop, Metal, Jazz) and click **Starting mix**. MusicDAWM measures how loud each track's recording is and sets:
- the **faders**, so that each role sits at a level typical of the genre, relative to the lead vocal (for example, in Rock the drums 2 dB and the bass 4 dB under the vocal; two guitar tracks share the guitars' level);
- the **pan**: drums, bass and lead vocal in the centre; tracks whose names end in " L" / " R" (or contain "left" / "right") to that side; several guitars, keyboards or backing vocals spread left and right; a single guitar slightly to one side;
- the **effects**: a high-pass on every track at its role's frequency, and brightness, punch and space amounts typical of the genre and role.

It is a starting point to listen to and adjust, not a finished mix: set the roles right first (FX panel), and remove guide tracks (⊖) before clicking it. **Randomise FX** (at the bottom) tries other effect amounts around the genre's, keeping the faders and pans. **Starting mix**, **Randomise FX** and **Reset settings** are each one step of **Edit → Undo**.

Tracks are measured as recorded (with clip gain), before their own plug-ins; Drum Studio (MIDI) tracks keep their fader and only get pan and effects.

**Vocal presets.** For a track whose role is **Vocals** or **Backing vocals**, the FX panel has a **Vocal preset** menu with 10 vocal sounds taken from professional mixes: the DiffVox research project (Sony and Queen Mary University of London) measured the effects used on hundreds of mixed vocals, and MusicDAWM picked 10 typical ones and rebuilt them with its own effects (EQ, compressor, reverb amount; no delay). Their names say what they do, for example *Bright, present, smooth, lots of reverb*, *Warm, smooth, lots of reverb*, *Very punchy, some reverb* or *Natural, dry*.
- Choosing a preset sets the track's EQ, compression and Space (reverb) amount, and its high-pass. **None** goes back to the plain tools; **Edit → Undo** too.
- The panel's controls then work from the preset: **Brightness** adds to (or takes from) the preset's highs, **Punch** at 50 % is the preset's compression (0 % none, 100 % twice as much), **Space** starts at the preset's reverb amount.
- The compression adapts to how loud your recording is, so a preset works the same on a quiet and on a loud take.
- The reverb is the shared Space reverb: a preset sets how much reverb, not which room.

The **Master** column on the right:
- The **loudness** of the song while it plays, in LUFS (the unit streaming services use): the big figure is the whole playback so far, **Now** the last moment, **Peak** the highest true peak. Each new playback starts a new measurement (**Restart** does it by hand).
- **Target**: **Streaming (−14 LUFS)**, **Loud (−9 LUFS)** or **Custom**. The column says how far the song is from it.
- **Limiter ceiling**: changing it puts a limiter last on the master bus (the first time), which keeps the true peaks under that level; −1 dBTP is safe for streaming and MP3.
- The master fader and meter, as in the classic mixer.

**Before / After / Ref** (top right) — to judge the mix with your ears, at the same loudness, because louder always sounds "better":
- **After**: the mix.
- **Before**: the tracks as recorded (faders at 0 dB, pan in the centre, no Final Mix effects), turned down or up to the loudness of the mix. The mix is kept and comes back with **After**; while **Before** is on, the mix's controls are greyed. Closing the window (or reopening the session) always returns to **After**.
- **Ref**: the reference song alone (see below), at the loudness of the mix.

**A reference song.** **Reference…** chooses a finished song you like, in the style you are aiming for (WAV, FLAC, AIFF or Ogg; MP3 and M4A when FFmpeg is installed). It is imported as a track called **Reference** that plays straight to your speakers, not through the master bus, so the mix's EQ and limiter do not colour it; it is silent except in **Ref**, and is never part of the export. A new reference replaces the old one.

**Match to reference** (rule-based, nothing is sent anywhere) analyses the mix (a quick silent export) and the reference — loudness, peaks, the balance of six frequency bands (sub bass, bass, low mids, mids, presence, air) and the stereo width — and then:
- sets the **loudness target** to the reference's (Custom);
- puts a gentle **master EQ** ("Final Mix Match EQ", before the limiter) that moves the mix a little more than half way towards the reference's tonal balance, at most 4 dB per band;
- tells you what it did and what to try by hand: wider or narrower panning, more or less Punch.

Listen with **After** and **Ref**, adjust, and match again if you change a lot. **Clear match** switches the match EQ off and brings back the earlier target. **FX ON** also switches the match EQ.

**Finalise Mix…** (bottom right) exports the song, from the session start to its end marker, as a 24-bit **WAV**, and as an **MP3** when FFmpeg is installed (see §5). It is made exactly as loud as the **Target**, with a true-peak limiter at the **Limiter ceiling**, so it is ready to upload. The dialog chooses the folder and name (by default the session's *export* folder); at the end it shows the measured loudness and peak and can open the folder.

Everything the window does is made of ordinary plug-ins and sends, named "Final Mix EQ", "Final Mix Punch", the "Final Mix Space" bus, "Final Mix Match EQ" and "Final Mix Limiter", so the classic mixer shows them and they can be fine-tuned there.

### Freeze, limiter and EQ

**Freeze a track.** Plug-ins such as Melodyne, amp simulators or big reverbs can make the computer struggle. In the **Mixer**, click **Freeze** at the bottom of the track's strip: the track is rendered once with its plug-ins and then plays the rendered copy, using almost no processing power. The button then reads **Frozen**. Click it again to **unfreeze**: the original recording and plug-ins come back, unchanged. While a track is frozen its plug-ins and regions cannot be edited.
- The button is shown on audio tracks only. It can be hidden in **Edit > Preferences > Appearance > Mixer** (Mixer Strip), like Ardour's other strip elements.
- Freezing renders the whole song (the session range, from the start to the end marker).

**Bundled plug-ins.** Two free plug-ins by Robin Gareus come with MusicDAWM, ready in the plug-in list:
- **x42-dpl Digital Peak Limiter** (mono and stereo): keeps the loudest peaks under a ceiling, for example −1 dB, so the song can be made louder without clipping. Put it last on the master bus.
- **x42-fil4 Parametric Equalizer** (mono and stereo): a high-pass filter (removes rumble below, for example, 80–120 Hz), shelves and four bands.

They appear with MusicDAWM's generic controls (sliders), not their own graphic windows.

## 12. Coming next

None of these are available yet. Each work package adds its section to this guide when it ships.

| Feature | What it will do for you | Work package |
|---|---|---|
| Songwriter layout and start screen | Recording controls hidden; "New song from WAV files" imports your Zoom R8 tracks aligned in one step | WP2 |
| CLAP plugins | Load CLAP plugins such as Surge XT | WP8 |

## Appendix: command-line tools in `bin`

The folder also contains Ardour's command-line session tools under MusicDAWM names (`musicdawm9-new_session.exe`, `musicdawm9-new_empty_session.exe`, `musicdawm9-export.exe`, `musicdawm9-lua.exe`), which work as in Ardour.

`musicdawm9-mother_smoke.exe` is MusicDAWM's own self-test. It builds a test session from seven WAV files with the exact track names of the "Mother" reference project, checks it, saves it and reopens it. It is meant for the build pipeline, not for your songs: it only accepts the generated test files from `tools/mother-smoke/make_wavs.py`.

`musicdawm9-waveform_test.exe` checks the Waveform import, and with `--convert <edit> <out.dawproject>` converts a Waveform edit to a DAWproject file.

`musicdawm9-ara_test.exe <folder> <ARATestPlugIn.vst3>` checks ARA hosting with the ARA SDK's test plug-in (not shipped); then `musicdawm9-ara_test.exe --reload <folder> <ARATestPlugIn.vst3>` checks that the saved session brings the plug-in's data back.

`musicdawm9-tap_tempo_test.exe <folder>` checks the Tap Tempo Map on a simulated performance with a drifting tempo, sloppy, missed and double taps, and a click track for snapping.

`musicdawm9-dawproject_test.exe` checks the DAWproject import against a test file made by `tools/dawproject/make_test_project.py`.

`musicdawm9-drum_studio_test.exe` tests Drum Studio the same way: it creates a rock drum track, performs intro → verse → fill → chorus → ending in real time with Rec Cues, replays it, drags a switch, flattens, edits one kick, saves and reopens. `musicdawm9-drum_studio_demo.exe <folder> <style>` writes a short demo song in one style (for example `rock` or `six_eight`) that you can open or export to hear the style.

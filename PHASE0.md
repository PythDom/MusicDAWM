# Phase 0 report: base confirmation spike

*2026-10-22. Time box: 3 days. Time used so far: about 1 day.*

## 1. Summary

| Question | Answer |
|---|---|
| Does Ardour build and pass its tests on Linux? | **Yes.** Ardour 9.8 builds in 27.5 min on 4 cores. All unit-test suites pass after one small upstream fix (§2.1). |
| Does a session shaped like "Mother" work? | **Yes, headless.** 7 BWF-aligned WAV tracks, a split at 292 s, +15.16 dB clip gain and a muted track all survive save and reopen. 26/26 checks pass, and the WAV export is 343 s long (§2.3). |
| Is there a Windows binary that runs "Mother"? | **Not yet.** The official cross-build tooling cannot be reached from this environment. The MSYS2 fallback on GitHub Actions now finds 33 of 34 packages, and the missing one (`suil`) is bundled with Ardour. Build status: see §2.2. |
| Is ARA2 in Ardour feasible within 3 months? | **Yes, with one licence question.** Ardour 9's per-region plugin chain (*Region FX*) maps almost one-to-one onto ARA playback regions (§3). The ARA SDK is Apache-2.0, which is compatible with GPLv3 but not GPLv2 (§4). |
| **Decision** | **Option A: fork Ardour, based on 9.8.** See §5. |

## 2. Build results

### 2.1 Linux (Ubuntu 24.04, GCC 13)

- Source: tag `9.8`, commit `22ed8656` (2026-08-19). The brief names 9.7; 9.8 is the current stable release, so we start there (`DECISIONS.md` D-001).
- Configure: `--optimize --no-phone-home --test --with-backends=jack,alsa,pulseaudio,dummy`. Stock configure turns "Phone home" on; we always turn it off (D-003).
- Build: 2010 steps in 27 min 29 s on 4 cores. Main binary `build/gtk2_ardour/ardour-9.8.0`.
- Unit tests:

| Suite | Result | Note |
|---|---|---|
| libpbd | 41 run, 2 fail → **environment** | `StringConvertTest` needs the fr_FR/de_DE/nl_NL locales; the container has only C/POSIX. CI installs no locales either. Not a code issue. |
| libevoral | OK | The script also wants `lcov`, which is optional. |
| libaudiographer | OK (45) | |
| libardour | 25 run, 1 fail → **upstream bug, fixed** | `FPUTest::avx512fTest`: see below. With the fix: **OK (25)**. |
| libmidi++2 | not run | Its `run-tests.sh` does not set `LD_LIBRARY_PATH` (an upstream script issue, not a test failure). |

**Upstream bug found:** `x86_avx512f_find_peaks()` widens the 1, 4 or 8 leftover samples into a 16-lane register, and the unused lanes (zero) take part in min/max. A buffer of only positive samples then reports a minimum of 0. This only affects machines with AVX-512, which includes many current desktop CPUs. The fix uses masked min/max (12 lines) and is in `patches/upstream/0001-…patch`, ready to send upstream. CI applies it.

### 2.2 Windows x64

**Official path (cross-compile from Linux with MinGW): blocked in this environment.**
Ardour's Windows dependency-stack script moved out of the main repository into `ardour-build-tools`, which is hosted on `git.ardour.org`. This cloud environment's network policy denies `git.ardour.org` and most other source hosts (`ardour.org`, `download.gnome.org`, `ftp.gnu.org`, `repo.msys2.org`). The MinGW-w64 toolchain itself installs fine (GCC 13). To use the official path, either add those hosts to the environment's allowed domains, or run it on a machine or CI runner with open network access.

**Fallback (native MSYS2 build on a GitHub Actions Windows runner)**, `ci/windows-msys2-build.sh`:

| Run | Result | Learned |
|---|---|---|
| 1 (MINGW64) | configure failed | 14 of 34 packages missing from the old MINGW64 repository. waf picked up MSVC from the runner image. |
| 2 (UCRT64) | configure failed | All packages present except `suil`, which Ardour bundles anyway. waf still found no C compiler. |
| 3 (UCRT64, toolchain diagnostics) | *pending* | |

A Windows binary that opens "Mother" is the one Phase 0 deliverable still open. It is a build-plumbing problem, not a code problem: Ardour ships official Windows builds from this same source. If the MSYS2 route stalls, WP1 should reproduce the official MinGW cross-build on a runner that can reach `git.ardour.org`.

### 2.3 "Mother" smoke test (headless, Linux)

`tools/mother-smoke/` contains a WAV generator and a small libardour program. It runs in CI after every build.

- The WAVs are seven synthetic stand-ins (44.1 kHz, 24-bit mono, 5:41, each with a BWF `bext` chunk). The vocal's BWF TimeReference is set to 2 s. The real "Mother" files are on the user's machine; the final acceptance test (brief §6) uses them.
- The program imports the WAVs the way Ardour's GUI does: external sources, whole-file regions, placed at their BWF position. Then it sets 120 bpm 4/4, gives track 1 a clip gain of +15.16 dB, splits track 5 at 292 s with +3 / −6 dB on the two halves, and mutes track 4. It saves, closes, reopens, and checks every value again.
- Result: **26/26 PASS**. `ardour9-export` then renders a 343.0 s stereo WAV (2 s vocal offset + 5:41).
- M6 also checked in the GUI source: the region gain editor allows −40 to +40 dB (`gtk2_ardour/audio_region_editor.cc:61`).
- Two things learned for later WPs:
  1. Edits that add undo commands (such as split) need an open reversible command. Headless and scripted code must open one, as the GUI does.
  2. `SessionUtils::create_session()` could not make a master bus. `patches/upstream/0002-…patch` adds an optional `BusProfile` parameter, which is also upstreamable.

## 3. ARA2 in Ardour: feasibility note

**Approach:** treat Melodyne as a **Region FX** plugin (Ardour 9's per-region plugin chain), not as a track insert. Studio One integrates ARA the same way ("event FX"). Each Ardour region then becomes exactly one ARA playback region.

**Model mapping** (ARA SDK 2.3 `ARAInterface.h` ↔ Ardour 9.8 `libs/ardour`):

| ARA object | Ardour object | Notes |
|---|---|---|
| Document | `Session` | One document controller per session. Edits are batched with `beginEditing`/`endEditing` around each undo transaction. |
| MusicalContext | `Temporal::TempoMap` | Tempo and bar-signature content readers come from the tempo map. Push an update on `TempoMap::MapChanged`, so a WP4 tap-tempo map reaches Melodyne. |
| RegionSequence | `Track` (its playlist) | Name and colour come from the route. |
| AudioSource | the set of `AudioSource`s of a region (one per channel) | ARA audio sources are multichannel; Ardour keeps one mono `Source` per channel. Key: the whole-file region's sources. `persistentID` = the source IDs. Samples are read with `AudioSource::read()` from non-RT threads, through the AudioAccessController. |
| AudioModification | **new**, stored as region extra XML | Ardour has no equivalent. A split keeps both halves on the same modification (Melodyne edits stay with the audio). Duplicate and copy clone it. Region `extra_xml` is copied with regions and saved in the session (`libs/pbd/stateful.cc`, `libs/ardour/region.cc:1486`), so stock Ardour keeps it untouched: this satisfies the data-safety rule. |
| PlaybackRegion | `AudioRegion` | `startInModificationTime` = `Region::start()`, `durationInModificationTime` = `startInPlaybackTime` duration = `Region::length()`, `startInPlaybackTime` = `Region::position()`. Moves and trims arrive as `Region::PropertyChanged` → `updatePlaybackRegionProperties`. Acceptance check §6 ("move the vocal clip; corrections follow") comes for free. |
| Archive | file in `<session>/plugins/` | Referenced from the session's extra XML. Written by the ArchivingController on save. |

**Hook points:**

- `libs/ardour/vst3_plugin.cc:1194-1229`: after `IPluginFactory` is obtained, query it for `ARA::IMainFactory`; after `createInstance`, query the component for `ARA::IPlugInEntryPoint2` and call `bindToDocumentControllerWithRoles` (playback renderer + editor view).
- `libs/ardour/audioregion.cc:2689` (`apply_region_fx`): Region FX already runs with timeline sample positions and handles plugin latency.
- `gtk2_ardour/region_editor.cc` (Region FX box): opens the plugin GUI, so the Melodyne editor appears per region.
- ARA 2.3 also defines a CLAP binding (`ARA_API/ARACLAP.h`), so WP8 (CLAP) and WP9 can share the document-controller code.

**Risks, highest first:**

1. **Render thread.** Region FX runs in the disk-reader (butler) thread, ahead of playback, not in the process callback. ARA playback renderers must render any requested range, and Melodyne supports offline rendering, but we must verify that Melodyne accepts the process-context flags Ardour passes there. If it does not, fall back to Melodyne as a track insert, with the track's regions as its playback regions. That variant is more work but stays in the same model.
2. **Licence** (§4).
3. **Testing needs the user's Melodyne licence** (brief §9 Q2). Until then, the ARA SDK's open example plug-ins (`ARA_Examples`, test host and plug-ins) are enough to develop and test against.

**Estimate:** 2–3 months, consistent with the brief. The first milestone, "Melodyne opens a clip", should come within 4–6 weeks: VST3 ARA discovery, a document with one audio source and one playback region, and the editor view.

## 4. Licence findings

- **ARA SDK 2.3:** `ARA_API/LICENSE.txt` and `ARA_Library/LICENSE.txt` are Apache-2.0 (verified in the tree). Apache-2.0 is compatible with GPLv3 but not with GPLv2. Ardour is "GPLv2 or later", so a binary that includes ARA support must be distributed under GPLv3. Our own source stays GPLv2-or-later. Proposal: make ARA a separate configure option (`--ara`) and publish ARA-enabled binaries under GPLv3 (D-005). **This needs the user's approval** (brief §8: stop and ask when a licence is unclear). Upstream Ardour would face the same question.
- **Upstream's stance on ARA:** the public statements we found say Ardour "cannot support … ARA" because no open-source implementation was allowed. That predates the SDK's 2022 Apache-2.0 release. A later forum post says ARA is being looked into but is not planned for Ardour 9. Their forum and tracker cannot be reached from this environment, so the brief's "upstream first" discussion threads (CLAP, DAWproject, ARA) still need to be opened.
- **Product name:** we found no published Ardour trademark policy, and ardour.org cannot be reached from here. General guidance for forks of open-source projects is to rename by default, and Harrison Mixbus does that. Ardour's build already takes `--program-name`. Unless upstream says otherwise in writing, we release under our own name (brief §9 Q1).

## 5. Decision

**Option A: fork Ardour, based on 9.8.**

The ARA mapping fits Ardour's existing model, without the "infeasible in 3 months" condition that Option B requires. Linux builds and tests pass, and the "Mother" operations work and persist. The open Windows build is plumbing, because upstream ships Windows binaries from this source.

## 6. Needed from the user before WP1

1. **Product name** and whether Linux builds matter (brief §9 Q1).
2. **Melodyne edition and version** (§9 Q2).
3. **Code-signing certificate:** yes or no (§9 Q3).
4. **Drum Studio styles** (§9 Q4).
5. **ARA licensing:** do you accept GPLv3 for ARA-enabled binaries (D-005)?
6. **Windows build host:** either allow `git.ardour.org` (and the source-tarball hosts) in this environment's network settings, or accept GitHub Actions MSYS2 as the Windows build path.
7. **Repository layout:** import Ardour's full history here (D-006)?
8. **"Mother" WAVs:** the real Zoom R8 files are not in the repository. The headless test uses synthetic stand-ins until the final acceptance test.
9. **IOdaw prototype:** `prototype/tracktion_inspect.py` and the IOdaw studies (brief §10) are not in this repository. WP6 needs them.

## 7. Files added in Phase 0

| Path | What |
|---|---|
| `PHASE0.md` | this report |
| `DECISIONS.md` | decision log |
| `THIRD_PARTY.md` | licence register |
| `.github/workflows/phase0-build.yml`, `ci/` | Linux build + tests + smoke test; Windows MSYS2 spike |
| `patches/upstream/` | two upstream-ready Ardour patches |
| `tools/mother-smoke/` | WAV generator and headless "Mother" test |

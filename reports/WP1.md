# WP1 report: build, CI, branding

*2026-10-23. MusicDAWM 0.1.0.*

## What was built

- **Builds and CI.** GitHub Actions builds the fork on every push: `Windows build` (native MSYS2/UCRT64, the shipped platform) and `Linux build` (fast unit tests). Both build this repository's Ardour tree in place, fetch Ardour's release tag for `git describe` (`ci/ardour-tag.sh`), and cache compiler output with ccache.
- **Package.** Portable folder, zipped as `MusicDAWM-<version>-win64.zip`, with `bin\MusicDAWM.exe`. Ardour's configuration, theme engine and fontconfig files are in the places Ardour looks for them (`ci/windows-package.sh`). There is no installer; the user asked for a portable folder (D-009).
- **Name and icon.** Built with `--program-name=MusicDAWM`. It has its own settings folder (`MusicDAWM9`), window titles, and command-line tool names (`musicdawm9-*`). The icon and splash are new artwork, with SVG sources and a render script in `tools/branding/`. Ardour's colour themes are installed under the MusicDAWM name.
- **Version scheme** (D-014). `MUSICDAWM_VERSION` holds the product version, 0.1.0. (A first attempt named it `VERSION`; on Windows that file shadowed the C++ header `<version>` and broke configure.) Ardour's version (`git describe`) stays untouched, because sessions and the settings folder depend on it. The About dialog shows both and says MusicDAWM is a modified version of Ardour, as the GPL requires; Ardour's credits, licence and website stay.
- **Release process** (D-015, `docs/releasing.md`). Run `Windows build` by hand with **publish** ticked. It builds the commit, runs the smoke test and creates the GitHub release `v<version>` with the zip.
- **No crash reporting, no telemetry** (D-017). There is no Dr. MinGW crash handler, and the announcement/update check is compiled out. The remaining network code is only reached through features the user starts.
- **Honest links** (D-016):
  - The donation dialog after export names Ardour, who receives the money, instead of the program name.
  - Help → Report a Bug opens MusicDAWM's issue page instead of Ardour's tracker.

## Requirement IDs

| ID | Status |
|---|---|
| S2 Runs on Windows | Pass in CI: the portable build runs the Mother smoke test and export on a Windows runner with no MSYS2 DLLs on `PATH`. The GUI has been started under Wine only; it still needs a manual start on the user's Windows machine. |
| S3 Stable releases / release process | Process in place (`docs/releasing.md`). No release published yet; that is the user's call. |

## Tests

| Check | Result |
|---|---|
| Ardour unit tests (libpbd, libevoral, libaudiographer, libardour), Linux, local | all pass |
| Mother smoke test + export, Linux, local | 28/28, export 343.0 s |
| `Linux build` workflow | *see CI section below* |
| `Windows build` workflow (build, portable folder, smoke test, zip) | *see CI section below* |

## Known issues

- The GUI has not been started on real Windows. Under Wine it starts and renders, but Wine has no audio device here, so a session can't be opened.
- The settings folder name is `MusicDAWM9`. The "9" comes from Ardour's major version and will change with Ardour 10.
- The MSYS2 runtime DLLs in the zip still need their licence texts listed before a public release (THIRD_PARTY.md).
- The Ardour version shown (for example 9.8.25) counts all commits since Ardour's tag, documentation included.

## Time

| | Estimate (brief) | Actual |
|---|---|---|
| WP1 | 1–2 weeks | about half a day of agent time. Most of the build work was already done in Phase 0, and CI builds take up to 2 hours each. |

# Releasing MusicDAWM

## Version scheme

- **MusicDAWM version:** `MAJOR.MINOR.PATCH` in the top-level `MUSICDAWM_VERSION` file, for example `0.1.0`. (Not `VERSION`: on Windows that name shadows the C++ header `<version>`.)
  - PATCH: fixes only.
  - MINOR: a new work package or feature.
  - MAJOR: 1 once the final "Mother" acceptance test (brief §6) passes.
  - Versions below 1.0 are previews.
- **Ardour base:** Ardour's own version stays as it is (for example `9.8.30`: Ardour 9.8 plus 30 MusicDAWM commits, from `git describe`). Ardour uses it for session compatibility and for the settings folder (`MusicDAWM9`), so it is never changed by hand.
- The About dialog shows both: "MusicDAWM 0.1.0 — a modified version of Ardour 9.8.30".

## Every push

The `Windows build` workflow builds the portable zip `MusicDAWM-<version>-win64.zip`, runs the Mother smoke test from it, and keeps the zip as a workflow artifact (90 days). The `Linux build` workflow runs the unit tests and the smoke test. Both must be green before a release.

## Making a release

1. Raise `MUSICDAWM_VERSION` in a commit and push it. Wait for both workflows to be green.
2. On GitHub: **Actions → Windows build → Run workflow**, choose the branch, tick **publish**, and run.
3. The workflow builds the commit again. If the build is green, it creates the release `v<version>` with the zip attached. The tag is created by GitHub on that commit.
4. Add release notes to the release page: what changed, which work packages are included, and known issues.

A release is never made from a red build. Releases are not code-signed (see PHASE0.md §8): users unblock the zip before unpacking it (docs/user-guide.md §1).

## Updating to a new Ardour release

See README.md. After a merge, raise MINOR, because the Ardour base changed.

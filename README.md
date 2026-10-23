# MusicDAWM

A songwriting and arranging DAW for one user, built as a fork of [Ardour](https://ardour.org) 9.8. Windows x64, as a portable folder.
Its workflow: import recordings, add drums the Korg Sound On Sound way, fix vocals with Melodyne, and mix with free plugins.

**Status:** Phase 0 is done, and the user has answered its questions (see [`PHASE0.md`](PHASE0.md) §8). This repository now *is* the fork: Ardour's full history up to tag `9.8`, our changes as commits on top. Next: WP1 (build, branding, portable package).

| File | Contents |
|---|---|
| [`PHASE0.md`](PHASE0.md) | Spike results, ARA2 feasibility note, base decision |
| [`DECISIONS.md`](DECISIONS.md) | Choices made where the brief was open |
| [`THIRD_PARTY.md`](THIRD_PARTY.md) | Licences of everything bundled or linked |
| everything else at the top level | Ardour's source tree (`wscript`, `libs/`, `gtk2_ardour/`, ...), see Ardour's own `README` |
| `ci/`, `.github/workflows/` | Windows build and portable folder (shipped); Linux build for fast tests |
| `session_utils/mother_smoke.cc`, `tools/mother-smoke/` | Headless test of a session shaped like the "Mother" reference project |

**Updating to a new Ardour release:** fetch the release tag from https://github.com/Ardour/ardour (`git fetch --no-tags https://github.com/Ardour/ardour.git refs/tags/X.Y:refs/tags/X.Y`), merge it, resolve conflicts, update `ci/ardour-ref`. The build needs the release's annotated tag, because Ardour takes its version number from `git describe`.

Ardour does not accept LLM-generated code (see DECISIONS.md D-010), so MusicDAWM's changes stay in this fork.

New code is GPLv2-or-later, like Ardour.

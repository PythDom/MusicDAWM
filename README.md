# MusicDAWM

A songwriting and arranging DAW for one user, built as a fork of [Ardour](https://ardour.org).
Its workflow: import recordings, add drums the Korg Sound On Sound way, fix vocals with Melodyne, and mix with free plugins.

**Status:** Phase 0 (base confirmation) is done; see [`PHASE0.md`](PHASE0.md). WP1 starts once the open questions in PHASE0.md §6 are answered.

| File | Contents |
|---|---|
| [`PHASE0.md`](PHASE0.md) | Spike results, ARA2 feasibility note, base decision |
| [`DECISIONS.md`](DECISIONS.md) | Choices made where the brief was open |
| [`THIRD_PARTY.md`](THIRD_PARTY.md) | Licences of everything bundled or linked |
| `ci/`, `.github/workflows/` | Builds of Ardour on Linux and Windows |
| `patches/upstream/` | Fixes meant for upstream Ardour |
| `tools/mother-smoke/` | Headless test of a session shaped like the "Mother" reference project |

New code is GPLv2-or-later, like Ardour.

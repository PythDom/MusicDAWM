# ARA SDK (vendored subset)

MusicDAWM hosts ARA 2 plug-ins (e.g. Melodyne) when built with
`./waf configure --ara` (WP9). This directory holds the part of the
[ARA SDK](https://github.com/Celemony/ARA_SDK) that a host needs:

| Path | From | What |
|---|---|---|
| `ARA_API/` | ARA_API, commit `65ec5c4` (ARA 2.3) | the C API (`ARAInterface.h`) and its VST3 binding (`ARAVST3.h`) |
| `ARA_Library/Dispatch/` | ARA_Library 2.3 | C++ wrappers for the host side (`ARAHostDispatch.*`) |
| `ARA_Library/Debug/` | ARA_Library 2.3 | debug helpers needed by the dispatch code (output and asserts are off) |

The files are unchanged copies. Both parts are under the Apache License 2.0
(`ARA_API/LICENSE.txt`, `ARA_Library/LICENSE.txt`); see `THIRD_PARTY.md`.
Apache-2.0 is compatible with GPLv3 but not with GPLv2, so binaries built with
`--ara` are distributed under GPLv3 (DECISIONS.md D-005, D-034).

The host code that uses it is `libs/ardour/ara_host.cc`; its test is
`session_utils/ara_test.cc`, run with the SDK's example plug-in
(`ci/ara-testplugin-build.sh`).

To update: copy the same files from a newer ARA_SDK checkout and note the
commit here.

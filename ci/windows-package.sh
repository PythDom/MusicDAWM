#!/bin/bash
# Phase 0 spike: make the installed tree in <stage-dir> self-contained, so it
# runs on a Windows machine without MSYS2. Copies every MSYS2 DLL that the
# installed executables and DLLs need into <stage-dir>/bin, and the core Ardour
# DLLs next to the executables.
# Usage (inside the MSYS2 shell used for the build): ci/windows-package.sh <stage-dir>
set -euo pipefail

STAGE=${1:?stage dir}
cd "$STAGE"
mkdir -p bin

# Core libraries (libardour, libpbd, ...) sit at the top of lib/<name>; plugins
# (backends, surfaces, panners) stay in their subfolders, which Ardour scans.
find lib -maxdepth 2 -name '*.dll' -exec cp -n {} bin/ \;

# Close over MSYS2 runtime dependencies; repeat until nothing new is copied.
while :; do
	before=$(ls bin | wc -l)
	find . -name '*.exe' -o -name '*.dll' | while read -r f; do
		ldd "$f" 2>/dev/null | awk '{print $3}' | grep -i "^$MINGW_PREFIX/bin/" || true
	done | sort -u | while read -r dll; do
		cp -n "$dll" bin/
	done
	[ "$(ls bin | wc -l)" -eq "$before" ] && break
done

echo "bundle: $(ls bin | wc -l) files in bin, $(du -sh . | cut -f1) total"
ls bin | grep -i '\.exe$'

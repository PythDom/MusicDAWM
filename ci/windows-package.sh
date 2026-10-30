#!/bin/bash
# Make the installed tree in <stage-dir> a self-contained, portable folder that
# runs on Windows without MSYS2. Mirrors what Ardour's tools/x-win/package.sh does
# for its own builds: executables and core DLLs in bin, configuration files in
# share/<name>, the clearlooks GTK engine, plus every MSYS2 DLL that is needed.
# Usage (inside the MSYS2 shell used for the build): ci/windows-package.sh <stage-dir>
set -euo pipefail

STAGE=${1:?stage dir}
SRC=$(cd "$(dirname "$0")/.." && pwd)
cd "$STAGE"
mkdir -p bin

# Core libraries (libardour, libpbd, ...) sit at the top of lib/<name>; plugins
# (backends, surfaces, panners) stay in their subfolders, which Ardour scans.
find lib -maxdepth 2 -name '*.dll' -exec cp -n {} bin/ \;

# The GUI and the plugin scanners must sit in bin: Ardour finds its data from
# the folder above the executable's bin folder. The GUI gets the product name.
cp -n lib/*/*.exe bin/
cp lib/*/ardour-[0-9]*.exe bin/MusicDAWM.exe
rm -f bin/ardour-[0-9]*.exe bin/hardour-[0-9]*.exe   # MusicDAWM.exe is the GUI; no headless server
# Import libraries are for linking only.
find . -name '*.dll.a' -delete

# The Vamp analysis plug-ins (export loudness normalisation, the loudness
# analysis, Final Mix) are found by library name: lib/<name>/vamp/
# libardourvampplugins.dll, as Ardour's own tools/x-win/package.sh ships them.
ALIB=$(ls -d lib/ardour[0-9]* | head -n 1)
mkdir -p "$ALIB/vamp"
for n in ardourvampplugins ardourvamppyin; do
	f=$(find "$SRC/build/libs" -name "*${n}*.dll" ! -name '*.dll.a' | head -n 1)
	[ -n "$f" ] || { echo "vamp plug-in $n not built" >&2; exit 1; }
	cp "$f" "$ALIB/vamp/lib${n}.dll"
done
find "$ALIB/vamp" -name '*.dll' ! -name 'libardourvamp*.dll' -delete
ls -l "$ALIB/vamp"

# On Windows, Ardour looks for its configuration (theme, keybindings, menus)
# only in share/<name>, not in etc/<name>.
for d in etc/*/; do
	name=$(basename "$d")
	[ "$name" = fonts ] && continue
	cp -r "$d"* "share/$name/"
done

# GTK loads its theme engine from lib/gtk-2.0/engines.
mkdir -p lib/gtk-2.0/engines
cp lib/*/engines/clearlooks.dll lib/gtk-2.0/engines/libclearlooks.la

# fontconfig configuration; its presence switches the GUI to the fontconfig
# font backend (gtk2_ardour/bundle_env_mingw.cc).
mkdir -p etc
cp -r "$MINGW_PREFIX/etc/fonts" etc/

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

# Licences: built with --ara, the program is distributed under GPLv3
# (DECISIONS.md D-005); the ARA SDK is Apache-2.0.
mkdir -p licences
cp "$SRC/COPYING-GPLv3" licences/GPLv3.txt
cp "$SRC/COPYING" licences/Ardour-COPYING.txt
cp "$SRC/libs/ara/ARA_API/LICENSE.txt" licences/ARA_API-Apache-2.0.txt
cp "$SRC/libs/ara/ARA_Library/LICENSE.txt" licences/ARA_Library-Apache-2.0.txt
cp "$SRC/THIRD_PARTY.md" licences/

echo "bundle: $(ls bin | wc -l) files in bin, $(du -sh . | cut -f1) total"
ls bin | grep -i '\.exe$'

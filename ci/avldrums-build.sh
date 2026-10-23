#!/bin/bash
# Build x42-avldrums (the Drum Studio's default kits, DECISIONS.md D-019) at a
# pinned commit and install the bundle as <lv2-dir>/avldrums.lv2.
# No plugin GUI (BUILDOPENGL=no): Ardour shows its generic controls.
# Usage: ci/avldrums-build.sh <lv2-dir>   (on Windows: inside the MSYS2 UCRT64 shell)
set -euo pipefail

LV2DIR=${1:?lv2 dir}
AVL_COMMIT=9389f16f139410d9a55f9006976bd54cbd8b7806
SRC=build/avldrums.lv2

cd "$(dirname "$0")/.."
if [ ! -d "$SRC/.git" ]; then
	git clone -q https://github.com/x42/avldrums.lv2.git "$SRC"
fi
git -C "$SRC" fetch -q origin
git -C "$SRC" checkout -q "$AVL_COMMIT"

ARGS=(BUILDOPENGL=no)
if [ -n "${MINGW_PREFIX:-}" ]; then
	# XWIN selects the Windows file names (.dll); the native MSYS2 tools do the work.
	ARGS+=(XWIN=x86_64-w64-mingw32 CC=gcc CXX=g++ STRIP=strip)
fi
make -C "$SRC" "${ARGS[@]}" -j"$(nproc)"
make -C "$SRC" "${ARGS[@]}" install DESTDIR= LV2DIR="$LV2DIR"
cp "$SRC/sf2/README" "$LV2DIR/avldrums.lv2/README-kits.txt"
cp "$SRC/COPYING" "$LV2DIR/avldrums.lv2/COPYING"
ls -l "$LV2DIR/avldrums.lv2"

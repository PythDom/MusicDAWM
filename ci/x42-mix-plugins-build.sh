#!/bin/bash
# Build the x42 plug-ins used by the Final Mix window (WP7, DECISIONS.md D-040)
# at pinned releases and install their bundles into <lv2-dir>:
#   x42-dpl   (Digital Peak Limiter, GPLv3+)  -> dpl.lv2
#   x42-fil4  (parametric EQ with high-pass, GPLv2+) -> fil4.lv2
# No plug-in GUIs (BUILDOPENGL=no): MusicDAWM shows its generic controls.
# Usage: ci/x42-mix-plugins-build.sh <lv2-dir>   (on Windows: inside the MSYS2 UCRT64 shell)
set -euo pipefail

LV2DIR=${1:?lv2 dir}
mkdir -p "$LV2DIR"
LV2DIR=$(cd "$LV2DIR" && pwd)

cd "$(dirname "$0")/.."

build () {
	local name=$1 commit=$2 bundle=$3
	local src=build/$name
	if [ ! -d "$src/.git" ]; then
		git clone -q "https://github.com/x42/$name.git" "$src"
	fi
	git -C "$src" fetch -q origin
	git -C "$src" checkout -q "$commit"
	# robtk is a submodule; fetch it now, not during the parallel build
	git -C "$src" submodule -q update --init

	local args=(BUILDOPENGL=no BUILDJACKAPP=no)
	if [ -n "${MINGW_PREFIX:-}" ]; then
		# XWIN selects the Windows file names (.dll); the native MSYS2 tools do the work.
		args+=(XWIN=x86_64-w64-mingw32 CC=gcc CXX=g++ STRIP=strip)
	fi
	make -C "$src" "${args[@]}" -j"$(nproc)"
	make -C "$src" "${args[@]}" install DESTDIR= LV2DIR="$LV2DIR"
	cp "$src/COPYING" "$LV2DIR/$bundle/COPYING"
	ls -l "$LV2DIR/$bundle"
}

build dpl.lv2  8bcd25deaf673fc93c66c4637fdc52bfdfe8ed6a dpl.lv2   # v0.7.3
build fil4.lv2 885edf4d4c8da3f1414462ec7b5fc1c2da0f3ac1 fil4.lv2  # v0.8.13

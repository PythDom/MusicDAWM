#!/bin/bash
# MusicDAWM WP9: build the ARA SDK's example plug-in (ARATestPlugIn.vst3), used
# by session_utils/ara_test.cc. Test-only; it is not shipped.
#
# Usage: ci/ara-testplugin-build.sh <work-dir>
# Prints the path of the plug-in (bundle or module) on the last line.
set -euo pipefail

WORK=$(mkdir -p "$1" && cd "$1" && pwd)
ARA_TAG=releases/2.3.0
VST3_TAG=v3.7.11_build_10

cd "$WORK"
if [ ! -d ARA_SDK ]; then
	git clone -q --depth 1 https://github.com/Celemony/ARA_SDK.git ARA_SDK >&2
	for m in ARA_API ARA_Library ARA_Examples; do
		git clone -q --depth 1 --branch "$ARA_TAG" "https://github.com/Celemony/$m.git" "ARA_SDK/$m" >&2
	done
	git clone -q --depth 1 --branch "$VST3_TAG" --recurse-submodules --shallow-submodules \
		https://github.com/steinbergmedia/vst3sdk.git ARA_SDK/vst3sdk >&2
fi

GEN=()
case "$(uname -s)" in
	# GCC 16 (MSYS2): ARA_Examples' ICST_AudioFile uses int64_t without <cstdint>
	MINGW*|MSYS*) GEN=(-G "MSYS Makefiles" -DCMAKE_CXX_FLAGS="-include cstdint") ;;
esac

case "$(uname -s)" in
	# MinGW has no std::aligned_alloc; use _aligned_malloc as for MSVC
	MINGW*|MSYS*) sed -i 's/#elif defined(_MSC_VER)$/#elif defined(_MSC_VER) || defined(__MINGW32__)/; s/^#if defined(_MSC_VER)$/#if defined(_MSC_VER) || defined(__MINGW32__)/' \
		ARA_SDK/vst3sdk/public.sdk/source/vst/utility/alignedalloc.h
	# windres cannot join string literals; the file is only version info
	echo '// version info left out for MinGW (test build)' > ARA_SDK/ARA_Examples/ExamplesCommon/Windows/ARAExamples.rc ;;
esac

cmake -S ARA_SDK/ARA_Examples -B build "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release \
	-DARA_API_DIR="$WORK/ARA_SDK/ARA_API" -DARA_LIBRARY_DIR="$WORK/ARA_SDK/ARA_Library" \
	-DARA_VST3_SDK_DIR="$WORK/ARA_SDK/vst3sdk" \
	-DARA_SETUP_DEBUGGING=OFF -DSMTG_RUN_VST_VALIDATOR=OFF -DSMTG_CREATE_PLUGIN_LINK=OFF -DSMTG_ADD_VST3_UTILITIES=OFF >&2
cmake --build build --config Release --target ARATestPlugInVST3 -j"$(nproc)" -- -k >&2

# the plug-in: its bundle (ARATestPlugIn.vst3/Contents/<arch>/...) if there is
# one, else the single-file module
MOD=$(find "$WORK/build" -type d -name ARATestPlugIn.vst3 -exec test -d '{}/Contents' ';' -print | head -1)
if [ -z "$MOD" ]; then
	MOD=$(find "$WORK/build" -type f \( -name 'ARATestPlugIn.so' -o -name 'ARATestPlugIn.vst3' \) | head -1)
fi
test -n "$MOD"
echo "$MOD"

#!/bin/bash
# Phase 0 spike: native Windows x64 build of upstream Ardour in an MSYS2 shell (UCRT64).
# This is the fallback path. The official path (ardour-build-tools/win/x-mingw.sh,
# cross-compiling from Linux) is hosted on git.ardour.org, which our cloud dev
# environment cannot reach. See PHASE0.md section 2.2.
# Applies our upstream-candidate patches and adds the Mother smoke test tool,
# like ci/linux-build.sh. Installs into <stage-dir> (default: ./stage).
# Usage (inside an MSYS2 UCRT64 or MINGW64 shell):
#   ci/windows-msys2-build.sh <ardour-src-dir> [<stage-dir>]
set -uo pipefail

SRC=${1:?ardour source dir}
STAGE=$(cygpath -m "$(realpath -m "${2:-stage}")")
HERE=$(cd "$(dirname "$0")/.." && pwd)
P=${MINGW_PACKAGE_PREFIX:?run inside an MSYS2 UCRT64 or MINGW64 shell}

PKGS="toolchain gcc pkgconf python boost glibmm libsndfile curl libarchive liblo taglib
vamp-plugin-sdk rubberband fftw aubio libsamplerate lv2 serd sord sratom lilv
cairomm pangomm libwebsockets libxml2 cppunit portaudio libusb gtk2 libogg flac
libvorbis opus soundtouch fontconfig python-numpy"

# Install each package on its own so the log shows exactly which ones MSYS2 lacks.
missing=""
for p in $PKGS; do
	if ! pacman -S --noconfirm --needed "$P-$p" >/dev/null 2>&1; then
		missing="$missing $p"
		echo "not found: $P-$p; candidates: $(pacman -Ssq "$p" 2>/dev/null | grep "^$P-" | head -5 | tr '\n' ' ')"
	fi
done
echo "MSYS2 packages not available:${missing:- none}" | tee msys2-missing.txt

set -e
echo "toolchain: CC=$(command -v gcc || echo none) PY=$(command -v python3 || echo none)"
gcc --version | head -1
python3 -c 'import sys; print("python", sys.version.split()[0], sys.platform, sys.executable)'
cd "$SRC"
for p in "$HERE"/patches/upstream/*.patch; do
	git apply --verbose "$p"
done
cp "$HERE/tools/mother-smoke/mother_smoke.cc" session_utils/

# waf would otherwise try MSVC from the runner image first (hence also the
# --check-c*-compiler options). Under a native Windows Python it needs full
# Windows paths ("gcc" alone is "not executable").
export CC="$(cygpath -m "$(command -v gcc).exe")" CXX="$(cygpath -m "$(command -v g++).exe")"
echo "CC=$CC CXX=$CXX"
# --no-lrdf: liblrdf (LADSPA metadata only) is not packaged for MSYS2.
# suil is not packaged either, but Ardour bundles it (libs/tk/suil).
# --no-dr-mingw: Dr. MinGW (exchndl, crash backtraces) is not packaged; it is optional.
# --cxx17: MSYS2 ships GCC 16, which defaults to C++20 (u8"" literals become char8_t).
if ! python3 ./waf configure --check-c-compiler=gcc --check-cxx-compiler=g++ \
	--dist-target=mingw --optimize --cxx17 --no-phone-home --no-lrdf --no-dr-mingw \
	--with-backends=dummy,portaudio \
	--prefix="$STAGE" --libdir="$STAGE/lib"; then
	echo "---- tail of build/config.log"
	tail -n 80 build/config.log
	exit 1
fi
if ! python3 ./waf build -j"$(nproc)" > build.log 2>&1; then
	echo "---- first errors in build.log"
	grep -n -m 30 -E "error:|Error [0-9]|cannot find|undefined reference" build.log || tail -n 60 build.log
	exit 1
fi
tail -n 3 build.log
python3 ./waf install > install.log 2>&1 || { tail -n 40 install.log; exit 1; }

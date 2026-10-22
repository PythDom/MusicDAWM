#!/bin/bash
# Phase 0 spike: native Windows x64 build of upstream Ardour in an MSYS2 shell (UCRT64).
# This is the fallback path. The official path (ardour-build-tools/win/x-mingw.sh,
# cross-compiling from Linux) is hosted on git.ardour.org, which our cloud dev
# environment cannot reach. See PHASE0.md section 2.2.
# Usage (inside an MSYS2 UCRT64 or MINGW64 shell): ci/windows-msys2-build.sh <ardour-src-dir>
set -uo pipefail

SRC=${1:?ardour source dir}
P=${MINGW_PACKAGE_PREFIX:?run inside an MSYS2 UCRT64 or MINGW64 shell}

PKGS="toolchain pkgconf python boost glibmm libsndfile curl libarchive liblo taglib
vamp-plugin-sdk rubberband fftw aubio libsamplerate lv2 serd sord sratom lilv suil
cairomm pangomm libwebsockets libxml2 cppunit portaudio libusb gtk2 libogg flac
libvorbis opus soundtouch fontconfig"

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
cd "$SRC"
# waf would otherwise pick up MSVC from the runner image.
export CC=gcc CXX=g++
# --no-lrdf: liblrdf (LADSPA metadata only) is not packaged for MSYS2.
./waf configure --dist-target=mingw --optimize --no-phone-home --no-lrdf \
	--with-backends=dummy,portaudio \
	--prefix="$MINGW_PREFIX" --libdir="$MINGW_PREFIX/lib"
./waf build -j"$(nproc)"

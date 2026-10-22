#!/bin/bash
# Phase 0 spike: native Windows x64 build of upstream Ardour in an MSYS2 MINGW64 shell.
# This is the fallback path. The official path (ardour-build-tools/win/x-mingw.sh,
# cross-compiling from Linux) is hosted on git.ardour.org, which our cloud dev
# environment cannot reach. See PHASE0.md section 2.2.
# Usage (inside the MINGW64 shell): ci/windows-msys2-build.sh <ardour-src-dir>
set -uo pipefail

SRC=${1:?ardour source dir}
P=mingw-w64-x86_64

PKGS="toolchain pkgconf python boost glibmm libsndfile curl libarchive liblo taglib
vamp-plugin-sdk rubberband fftw aubio libsamplerate lv2 serd sord sratom lilv suil
cairomm pangomm libwebsockets libxml2 cppunit portaudio libusb gtk2 libogg flac
libvorbis opus soundtouch fontconfig"

# Install each package on its own so the log shows exactly which ones MSYS2 lacks.
missing=""
for p in $PKGS; do
	if ! pacman -S --noconfirm --needed "$P-$p" >/dev/null 2>&1; then
		missing="$missing $p"
	fi
done
echo "MSYS2 packages not available:${missing:- none}" | tee msys2-missing.txt

set -e
cd "$SRC"
# --no-lrdf: liblrdf (LADSPA metadata only) is not packaged for MSYS2.
./waf configure --dist-target=mingw --optimize --no-phone-home --no-lrdf \
	--with-backends=dummy,portaudio \
	--prefix=/mingw64 --libdir=/mingw64/lib
./waf build -j"$(nproc)"

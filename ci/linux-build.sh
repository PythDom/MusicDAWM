#!/bin/bash
# Build MusicDAWM (this repository's Ardour tree) on Ubuntu 24.04, with unit tests.
# Usage: ci/linux-build.sh
set -euo pipefail

cd "$(dirname "$0")/.."
bash ci/ardour-tag.sh

sudo apt-get update -qq
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
	libboost-dev libglibmm-2.4-dev libsndfile1-dev libcurl4-openssl-dev \
	libarchive-dev liblo-dev libtag1-dev vamp-plugin-sdk librubberband-dev \
	libusb-1.0-0-dev libasound2-dev libpulse-dev libjack-jackd2-dev \
	libfftw3-dev libaubio-dev liblrdf0-dev libsamplerate0-dev lv2-dev \
	libserd-dev libsord-dev libsratom-dev liblilv-dev libsuil-dev \
	libcairomm-1.0-dev libpangomm-1.4-dev libwebsockets-dev libxml2-dev \
	libcppunit-dev libreadline-dev libgtk2.0-dev libudev-dev libdbus-1-dev \
	libopus-dev libogg-dev libflac-dev libvorbis-dev itstool gettext \
	python3-numpy ccache

# ccache (cache folder set by the workflow) makes rebuilds much faster.
export CC="ccache gcc" CXX="ccache g++"
ccache -z >/dev/null
./waf configure --program-name=MusicDAWM --optimize --no-phone-home --test --ara \
	--with-backends=jack,alsa,pulseaudio,dummy
./waf build -j"$(nproc)"
ccache -s | grep -iE "hits|misses|cache size" || true

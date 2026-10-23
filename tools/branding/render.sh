#!/bin/bash
# Render the MusicDAWM icon and splash (SVG sources in this folder) to the files
# Ardour's build looks up by program name: gtk2_ardour/resources/MusicDAWM-*.png
# and gtk2_ardour/icons/MusicDAWM.ico. Needs rsvg-convert and ImageMagick.
# The rendered files are committed, so builds do not need these tools.
set -euo pipefail
cd "$(dirname "$0")"
R=../../gtk2_ardour/resources
I=../../gtk2_ardour/icons
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT

for s in 16 22 32 48 256 512; do
	rsvg-convert -w $s -h $s musicdawm-icon.svg -o "$R/MusicDAWM-icon_${s}px.png"
done
rsvg-convert -w 160 -h 160 musicdawm-icon.svg -o "$T/icon-160.png"
cp musicdawm-splash.svg "$T/"
(cd "$T" && rsvg-convert musicdawm-splash.svg -o splash.png)
cp "$T/splash.png" "$R/MusicDAWM-splash.png"
convert "$T/splash.png" -resize 100x87 "$R/MusicDAWM-small-splash.png"
convert "$R"/MusicDAWM-icon_{16,22,32,48,256}px.png "$I/MusicDAWM.ico"
identify "$R"/MusicDAWM-* "$I/MusicDAWM.ico" | awk '{print $1, $3}'

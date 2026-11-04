#!/bin/bash
# Fetch the GeneralUser GS soundfont (General MIDI drum kits and basses for
# Drum Studio and Bass Studio, played by Ardour's ACE Fluid Synth; THIRD_PARTY.md)
# at a pinned commit, checked by its SHA-256, into <dest-dir> with its licence.
# Usage: ci/generaluser-gs-fetch.sh <dest-dir>
set -euo pipefail

DEST=${1:?dest dir}
COMMIT=684543d5e5efaef08d02be50dcda8d552478fa60
SHA256=9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe
SRC=build/generaluser-gs

cd "$(dirname "$0")/.."
if [ ! -d "$SRC/.git" ]; then
	git clone -q https://github.com/mrbumpy409/GeneralUser-GS.git "$SRC"
fi
git -C "$SRC" fetch -q origin
git -C "$SRC" checkout -q "$COMMIT"
echo "$SHA256  $SRC/GeneralUser-GS.sf2" | sha256sum -c -
mkdir -p "$DEST"
cp "$SRC/GeneralUser-GS.sf2" "$DEST/"
cp "$SRC/documentation/LICENSE.txt" "$DEST/LICENSE-GeneralUser-GS.txt"
ls -l "$DEST"

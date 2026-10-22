#!/usr/bin/env python3
"""Generate seven synthetic stand-ins for the "Mother" WAVs (Zoom R8 style).

Each file is 44.1 kHz / 24-bit mono, 341 s long (5:41), with a BWF `bext`
chunk. The vocal (track 7) starts 2 s later via its BWF TimeReference, so
BWF-aligned import can be checked. The audio is a quiet sine per track; it
only has to be valid audio, not music.

Usage: make_wavs.py <out-dir>
"""
import struct
import sys
from pathlib import Path

import numpy as np

RATE = 44100
SECONDS = 341
NAMES = ["01 Guitar L", "02 Guitar R", "03 Guitar DI", "04 Room", "05 Guitar 2", "06 Voice ref", "07 Vocal"]
OFFSETS = {"07 Vocal": 2 * RATE}  # BWF TimeReference in samples


def bext_chunk(time_reference: int) -> bytes:
    body = bytearray(602)
    body[0:16] = b"mother smoke    "
    struct.pack_into("<Q", body, 338, time_reference)  # TimeReference low/high
    struct.pack_into("<H", body, 346, 1)                # Version
    return b"bext" + struct.pack("<I", len(body)) + bytes(body)


def write_wav(path: Path, freq: float, time_reference: int) -> None:
    t = np.arange(RATE * SECONDS) / RATE
    x = (0.1 * np.sin(2 * np.pi * freq * t) * (2**23 - 1)).astype("<i4")
    pcm = x.view(np.uint8).reshape(-1, 4)[:, :3].tobytes()  # 24-bit little endian
    fmt = b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, RATE, RATE * 3, 3, 24)
    bext = bext_chunk(time_reference)
    data = b"data" + struct.pack("<I", len(pcm)) + pcm
    riff = b"WAVE" + bext + fmt + data
    path.write_bytes(b"RIFF" + struct.pack("<I", len(riff)) + riff)


def main() -> None:
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for i, name in enumerate(NAMES):
        write_wav(out / f"{name}.wav", 110.0 * (i + 1), OFFSETS.get(name, 0))
        print(out / f"{name}.wav")


if __name__ == "__main__":
    main()

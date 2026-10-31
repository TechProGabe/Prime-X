#!/usr/bin/env python3
"""xbox/assets/intro.mp3 -> the music the boot screen plays while the first
boot's disc check runs (docs/ui.md "Boot splash"), carried inside
default.xbe so a copy of the game can't leave it behind (the user, round
hw2).

  python tools/xbox/make_intro.py <default.xbe>

The song is IMA ADPCM, QuickTime packets (adpcm_ima_qt): 32 kHz stereo (the
rate the game's own audio runs at, so the AC97 path is never reconfigured),
the volume baked in at 75% (the user, 2026-10-27), 68 bytes for 64 frames
(34 a channel: a 2-byte header, 64 nibbles), 8.3 MB for the 4 minutes where
16-bit PCM is 31 MB. Decoding is a table lookup and a few adds a sample
(xbox/src/hw/xhw_intro.c).

It is appended after the XBE's last byte, not as a section: the kernel
loads sections and sizes the image from the header, and neither sees what
follows, so it costs no memory. A 16-byte footer ends the file:
"PXIN", the payload's file offset, its length, the format version (1).
Running this again replaces the payload.

The encode (ffmpeg, trellis 8, ~2 min) is cached as build-xbox/intro.qt.
Needs ffmpeg (on PATH, or FFMPEG=<path>); without it the XBE has no music
and the check runs silent.
"""
import os
import pathlib
import shutil
import struct
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / 'xbox/assets/intro.mp3'
CACHE = ROOT / 'build-xbox/intro.qt'
VOLUME = 0.75
RATE = 32000
MAGIC = b'PXIN'
VERSION = 1


def find_ffmpeg():
    for c in (os.environ.get('FFMPEG'), shutil.which('ffmpeg'), 'C:/ProgramData/chocolatey/bin/ffmpeg.exe',
              '/c/ProgramData/chocolatey/bin/ffmpeg.exe', '/usr/bin/ffmpeg'):
        if c and pathlib.Path(c).exists():
            return c
    return None


def encode():
    newest = max(SRC.stat().st_mtime, pathlib.Path(__file__).stat().st_mtime)
    if CACHE.exists() and CACHE.stat().st_mtime >= newest:
        return CACHE.read_bytes()
    ffmpeg = find_ffmpeg()
    if not ffmpeg:
        print('make_intro: no ffmpeg, no music in the XBE (the disc check runs silent)')
        return None
    CACHE.parent.mkdir(parents=True, exist_ok=True)
    tmp = CACHE.with_suffix('.tmp')
    subprocess.run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', '-i', str(SRC), '-map', '0:a',
                    '-af', f'volume={VOLUME}', '-ar', str(RATE), '-ac', '2', '-c:a', 'adpcm_ima_qt', '-trellis', '8',
                    '-f', 'data', str(tmp)], check=True)
    data = tmp.read_bytes()
    if not data or len(data) % 68:
        sys.exit(f'make_intro: {len(data)} bytes is not whole 68-byte stereo packets')
    tmp.replace(CACHE)
    return data


def strip(xbe):
    """the XBE without a payload this script appended before"""
    if len(xbe) >= 16 and xbe[-16:-12] == MAGIC:
        off, size, _ = struct.unpack_from('<III', xbe, len(xbe) - 12)
        if off + size + 16 == len(xbe):
            return xbe[:off]
    return xbe


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    xbe_path = pathlib.Path(sys.argv[1])
    data = encode()
    if data is None:
        return
    xbe = strip(xbe_path.read_bytes())
    off = (len(xbe) + 0xFFF) & ~0xFFF   # sector-aligned reads on the console
    out = xbe + bytes(off - len(xbe)) + data + MAGIC + struct.pack('<III', off, len(data), VERSION)
    xbe_path.write_bytes(out)
    print(f'make_intro: {xbe_path}: +{len(data) >> 10} KB of music '
          f'({len(data) // 68 * 64 // RATE} s, IMA ADPCM) at offset {off:#x}')


if __name__ == '__main__':
    main()

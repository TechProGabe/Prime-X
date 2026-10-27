#!/usr/bin/env python3
"""xbox/assets/intro.mp3 -> intro.pcm, the music the boot screen plays while
the first boot's disc check runs (docs/ui.md "Boot splash").

The console does no work on it but copying: 16-bit little-endian stereo at
32 kHz (the rate the game's own audio runs at, so the AC97 path is never
reconfigured), the volume baked in at 75% (the user, 2026-10-27).

  python tools/xbox/make_intro.py [out.pcm]      # default build-xbox/xbe/intro.pcm

Needs ffmpeg (on PATH, or FFMPEG=<path>); without it the build has no
intro.pcm and the check runs silent.
"""
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / 'xbox/assets/intro.mp3'
VOLUME = 0.75
RATE = 32000


def find_ffmpeg():
    for c in (os.environ.get('FFMPEG'), shutil.which('ffmpeg'), 'C:/ProgramData/chocolatey/bin/ffmpeg.exe',
              '/c/ProgramData/chocolatey/bin/ffmpeg.exe', '/usr/bin/ffmpeg'):
        if c and pathlib.Path(c).exists():
            return c
    return None


def main():
    out = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'build-xbox/xbe/intro.pcm'
    if out.exists() and out.stat().st_mtime >= max(SRC.stat().st_mtime, pathlib.Path(__file__).stat().st_mtime):
        return
    ffmpeg = find_ffmpeg()
    if not ffmpeg:
        print('make_intro: no ffmpeg, no intro.pcm (the disc check runs silent)')
        return
    out.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', '-i', str(SRC), '-af', f'volume={VOLUME}',
                    '-ar', str(RATE), '-ac', '2', '-f', 's16le', '-c:a', 'pcm_s16le', str(out)], check=True)
    print(f'make_intro: {out} ({out.stat().st_size >> 10} KB)')


if __name__ == '__main__':
    main()

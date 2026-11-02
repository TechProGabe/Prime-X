#!/usr/bin/env python3
"""Build and run tests/xbox/test_mixer.c on the host: px_mixer.h's 32-bit
mixer loops (xbox/src/port/include) against hw_pc.c's own (mixRampChannel
and the polyphase table builder, cut out of
extern/musyx/src/musyx/runtime/hw_pc.c); then tests/xbox/test_reverb.c:
creverb.c's PortHandleReverbChannel against its HandleReverbChannel. Every
result must be bit-exact.

    PATH=/c/msys64/mingw64/bin:$PATH CC=gcc python tools/xbox/test_mixer.py"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

PRELUDE = """#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "px_mixer.h"
typedef int16_t s16;
typedef int32_t s32;
typedef uint32_t u32;
typedef uint8_t u8;
#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#define SDL_fabs fabs
#define SDL_sin sin
#define SDL_cos cos
#define SDL_lround lround
#define SDL_PI_D 3.141592653589793238462643383279502884
#define SDL_arraysize(a) (sizeof(a) / sizeof((a)[0]))
#define PX_MIXER_FAST 0
#define SAL_SAMPLES_PER_FRAME 160
#define POLYPHASE_PHASES 128
#define POLYPHASE_TAPS 4
#define SINC_PHASES 512
#define SINC_TAPS 8
#define SINC_HALF_TAPS (SINC_TAPS / 2)
static s16 polyphaseTable[POLYPHASE_PHASES][POLYPHASE_TAPS];
static s16 sincTable[SINC_PHASES][SINC_TAPS];
static u8 resampleTablesInitialized = 0;
"""


def cut(src, start, end):
    a = src.index(start)
    return src[a:src.index(end, a)]


def main():
    src = open(os.path.join(ROOT, "extern/musyx/src/musyx/runtime/hw_pc.c")).read()
    parts = [
        cut(src, "static inline s16 clamp16(s32 v)", "static inline u16 applyDeltaToLastVol"),
        cut(src, "static inline void mixRampChannel", "static inline void addThreeChannelBuffer"),
    ]
    test = open(os.path.join(ROOT, "tests/xbox/test_mixer.c")).read()
    cc = os.environ.get("CC", "cc")
    with tempfile.TemporaryDirectory() as tmp:
        c = os.path.join(tmp, "t.c")
        exe = os.path.join(tmp, "test_mixer")
        with open(c, "w") as f:
            f.write(PRELUDE + "\n".join(parts) + test)
        r = subprocess.run([cc, "-O2", "-w", "-std=c99", "-I", os.path.join(ROOT, "xbox/src/port/include"),
                            "-I", os.path.join(ROOT, "extern/musyx/include"), "-o", exe, c, "-lm"])
        if r.returncode:
            return r.returncode
        return subprocess.run([exe]).returncode


REVERB_PRELUDE = """#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int32_t s32;
typedef float f32;
typedef struct _SND_REVSTD_DELAYLINE {
  s32 inPoint;
  s32 outPoint;
  s32 length;
  f32* inputs;
  f32 lastOutput;
} _SND_REVSTD_DELAYLINE;
typedef struct _SND_REVSTD_WORK {
  _SND_REVSTD_DELAYLINE AP[6];
  _SND_REVSTD_DELAYLINE C[6];
  f32 allPassCoeff;
  f32 combCoef[6];
  f32 lpLastout[3];
  f32 level;
  f32 damping;
  s32 preDelayTime;
  f32* preDelayLine[3];
  f32* preDelayPtr[3];
} _SND_REVSTD_WORK;
"""


def reverb():
    src = open(os.path.join(ROOT, "extern/musyx/src/musyx/runtime/CheapReverb/creverb.c")).read()
    part = cut(src, "static f32 DLreadSample(_SND_REVSTD_DELAYLINE* dl)", "static void HandleReverb(s32* sptr")
    test = open(os.path.join(ROOT, "tests/xbox/test_reverb.c")).read()
    cc = os.environ.get("CC", "cc")
    with tempfile.TemporaryDirectory() as tmp:
        c = os.path.join(tmp, "r.c")
        exe = os.path.join(tmp, "test_reverb")
        with open(c, "w") as f:
            f.write(REVERB_PRELUDE + part + test)
        # SSE scalar floats, no contraction: as the console build
        r = subprocess.run([cc, "-O2", "-w", "-std=c99", "-DTARGET_XBOX", "-msse2", "-mfpmath=sse",
                            "-ffp-contract=off", "-o", exe, c, "-lm"])
        if r.returncode:
            return r.returncode
        return subprocess.run([exe]).returncode


if __name__ == "__main__":
    sys.exit(main() or reverb())

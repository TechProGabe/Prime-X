#!/usr/bin/env python3
"""Build and run tests/xbox/test_pad.c on the host: xbox/src/sdk/pad.c's two
fixed button layouts (Dual Stick: A or RT fires; GameCube: RT is Free Aim)
as the game's PADRead sees them.

    PATH=/c/msys64/mingw64/bin:$PATH CC=gcc python tools/xbox/test_pad.py
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def main():
    cc = os.environ.get("CC", "cc")
    inc = ["-I" + os.path.join(ROOT, d) for d in ("xbox/src/sdk", "xbox/include", "extern/aurora/include")]
    with tempfile.TemporaryDirectory() as tmp:
        exe = os.path.join(tmp, "test_pad")
        r = subprocess.run([cc, "-O1", "-w", "-DXHW_TEST_BUILD=0", "-DTARGET_PC", *inc, "-o", exe,
                            os.path.join(ROOT, "tests/xbox/test_pad.c"), "-lm"])
        if r.returncode:
            return r.returncode
        return subprocess.run([exe]).returncode


if __name__ == "__main__":
    sys.exit(main())

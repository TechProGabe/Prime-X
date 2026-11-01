#!/usr/bin/env python3
"""Build and run tests/xbox/test_settings.c on the host: xbox/src/sdk/settings.c
reading hand-edited settings.ini files (typos, unknown keys, out-of-range
numbers, layouts with no Fire, a BOM, UTF-16), and its save/read-back."""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def main():
    cc = os.environ.get("CC", "cc")
    inc = ["-I" + os.path.join(ROOT, d) for d in ("xbox/src/sdk", "xbox/include", "extern/aurora/include")]
    with tempfile.TemporaryDirectory() as tmp:
        exe = os.path.join(tmp, "test_settings")
        r = subprocess.run([cc, "-O1", "-w", "-DXHW_TEST_BUILD=0", "-DTARGET_PC", *inc, "-o", exe,
                            os.path.join(ROOT, "tests/xbox/test_settings.c")])
        if r.returncode:
            return r.returncode
        work = os.path.join(tmp, "save")
        os.makedirs(work)
        return subprocess.run([exe, work]).returncode


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Build and run tests/xbox/bench_menu.c on the host: the Xbox Port Options
card (xbox/src/hw/xhw_menu.c) drawn at 480 and 720p, with the time each
frame takes while a change is drawn. The font comes out of xhw_splash.c.

    PATH=/c/msys64/mingw64/bin:$PATH CC=gcc python tools/xbox/bench_menu.py [out_prefix]

out_prefix: also write the last frame of each mode as <out_prefix>_<h>.ppm
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def main():
    splash = open(os.path.join(ROOT, "xbox/src/hw/xhw_splash.c")).read()
    a = splash.index("const unsigned char xhw_font16[256 * 16]")
    b = splash.index("};", a) + 2
    cc = os.environ.get("CC", "cc")
    with tempfile.TemporaryDirectory() as tmp:
        font = os.path.join(tmp, "font.c")
        with open(font, "w") as f:
            f.write(splash[a:b] + "\n")
        exe = os.path.join(tmp, "bench_menu")
        nxdk = os.environ.get("NXDK_DIR", "/c/xdev/nxdk" if os.name != "nt" else "C:/xdev/nxdk")
        inc = ["-I" + os.path.join(ROOT, d) for d in ("xbox/src/hw", "xbox/include")] + ["-I" + os.path.join(nxdk, "lib")]
        r = subprocess.run([cc, "-O2", "-w", *inc, "-o", exe, os.path.join(ROOT, "tests/xbox/bench_menu.c"), font])
        if r.returncode:
            return r.returncode
        return subprocess.run([exe] + sys.argv[1:]).returncode


if __name__ == "__main__":
    sys.exit(main())

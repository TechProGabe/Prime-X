#!/usr/bin/env python3
"""Zip a release build for the Releases page.

    tools/xbox/package_release.py <name>      # e.g. rc1 -> dist/Prime-X-rc1.zip

From build-xbox-lto-pgo/xbe: build first with XBOX_LTO=1
XBOX_PGO=xbox/prime.profdata and no XBOX_CFLAGS (a release build has no
profiler or autopad; docs/pgo.md). The
zip holds what README.md describes:

    Prime-X/default.xbe     the game
    Prime-X/default.tbn     dashboard icon (XBMC-style dashboards)
    tools/make-xiso         packs it with your disc image into a burnable ISO
    README.md, LICENSE.md
"""
import pathlib
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    build = ROOT / 'build-xbox-lto-pgo'
    xbe = build / 'xbe'
    for need in ('default.xbe', 'default.tbn'):
        if not (xbe / need).exists():
            sys.exit(f'no {xbe / need}: build first (XBOX_LTO=1 XBOX_PGO=xbox/prime.profdata tools/xbox/msys/build.sh)')
    cache = (build / 'CMakeCache.txt').read_bytes()

    def cached(name):
        key = name.encode() + b'='
        return cache.split(key)[1].split(b'\n')[0].strip() if key in cache else b''

    if cached('CMAKE_C_FLAGS:STRING'):
        sys.exit('this build has XBOX_CFLAGS set: rebuild without them for a release')
    # releases are ThinLTO + PGO with the committed profile (docs/pgo.md)
    if cached('PX_LTO:BOOL') != b'ON' or not cached('PX_PGO:STRING').endswith(b'prime.profdata'):
        sys.exit('a release is built with XBOX_LTO=1 XBOX_PGO=xbox/prime.profdata')
    out = ROOT / 'dist' / f'Prime-X-{sys.argv[1]}.zip'
    out.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        z.write(xbe / 'default.xbe', 'Prime-X/default.xbe')
        z.write(xbe / 'default.tbn', 'Prime-X/default.tbn')
        info = zipfile.ZipInfo('tools/make-xiso')
        info.external_attr = 0o100755 << 16   # executable when unzipped on Linux/macOS
        z.writestr(info, (ROOT / 'tools' / 'make-xiso').read_bytes(), zipfile.ZIP_DEFLATED)
        z.write(ROOT / 'README.md', 'README.md')
        z.write(ROOT / 'LICENSE.md', 'LICENSE.md')
    print(f'wrote {out} ({out.stat().st_size // 1024} KB)')


if __name__ == '__main__':
    main()

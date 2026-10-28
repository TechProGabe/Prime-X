#!/usr/bin/env python3
"""A link order for the code from the PGO profile (docs/pgo.md, docs/plan.md
phase 6 "Compiler optimizations").

  tools/xbox/make_order.py --map build-xbox-lto-pgo/prime_x.map xbox/prime.profdata -o xbox/order.txt

With -ffunction-sections every function is its own COMDAT section and
lld-link lays .text out in the /order file's order. This writes: the
functions the training scenarios ran, by entry count (hottest first), so the
code a frame runs sits together in the Pentium III's 128 KB L2 and fewer
4 KB pages; then every other function in the map's current order (cold code
keeps today's layout). Melee-X's profile-ordered link was worth +13%; its
make_order.py works from whole-match sampling profiles, which Prime's
profiler doesn't log, so this one takes the PGO counts (llvm-profdata show).

Names are the map's (decorated) symbols. The profile names a static
function "<file>;<name>": the part after the ';' is used. A name the link
doesn't have is skipped by lld-link (-ignore:4037)."""
import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys

TEXT = re.compile(r'^\s*0001:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{16})\s+(?:f\s+)?(?:i\s+)?(\S+)\s*$')


def text_functions(path):
    """the map's .text symbols in address order: [(va, name)]"""
    out = []
    with open(path, errors='replace') as f:
        for line in f:
            m = TEXT.match(line)
            if m:
                out.append((int(m.group(2), 16), m.group(1)))
    out.sort()
    return out


def profdata_tool():
    for c in (os.environ.get('LLVM_PROFDATA'), shutil.which('llvm-profdata'),
              '/c/msys64/mingw64/bin/llvm-profdata.exe', 'C:/msys64/mingw64/bin/llvm-profdata.exe'):
        if c and pathlib.Path(c).exists():
            return c
    sys.exit('no llvm-profdata (set LLVM_PROFDATA)')


def entry_counts(profdata):
    """{name: function entry count} for every function the profile has"""
    out = subprocess.run([profdata_tool(), 'show', '--all-functions', '--counts', profdata],
                         capture_output=True, text=True, check=True).stdout
    counts, name = {}, None
    for line in out.splitlines():
        s = line.strip()
        if s.endswith(':') and not s.startswith(('Hash', 'Counters', 'Function count', 'Block counts')) \
                and line.startswith('  ') and not line.startswith('    '):
            name = s[:-1]
            continue
        # the first counter is the function's entry count
        m = re.match(r'(?:Function count: |Block counts: \[)(\d+)', s)
        if m and name:
            n = name.split(';')[-1]
            counts[n] = max(counts.get(n, 0), int(m[1]))
            name = None
    return counts


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('profdata')
    ap.add_argument('--map', required=True, help='a map of the build the order is for')
    ap.add_argument('-o', '--out', required=True)
    a = ap.parse_args()
    texts = text_functions(a.map)
    if not texts:
        sys.exit(f'{a.map}: no .text symbols')
    in_map = {name for _, name in texts}
    size = {name: (texts[i + 1][0] - va if i + 1 < len(texts) else 0) for i, (va, name) in enumerate(texts)}
    counts = entry_counts(a.profdata)
    # the map's i386 C names carry an underscore the profile's don't
    def map_name(n):
        return n if n in in_map else '_' + n if '_' + n in in_map else None
    hot = sorted(((c, map_name(n)) for n, c in counts.items() if c > 0 and map_name(n)), key=lambda t: -t[0])
    order, seen = [], set()
    for _, n in hot:
        if n not in seen:
            seen.add(n)
            order.append(n)
    nhot, hot_kb = len(order), sum(size.get(n, 0) for n in order) // 1024
    for _, n in texts:
        if n not in seen:
            seen.add(n)
            order.append(n)
    # C names as written: lld-link puts the i386 underscore back on. LF only.
    undecorate = lambda n: n[1:] if n.startswith('_') else n
    pathlib.Path(a.out).write_bytes(''.join(undecorate(n) + '\n' for n in order).encode())
    print(f'{a.out}: {nhot} functions the profile ran ({hot_kb} KB in this map), then {len(order) - nhot} '
          f'in map order ({len(counts)} profiled functions)')


if __name__ == '__main__':
    main()

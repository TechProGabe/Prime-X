#!/usr/bin/env python3
"""List Metroid Prime's worlds and rooms from your own disc image, with the ids
MP_BOOT_WORLD takes (docs/testing.md "Jumping to a room").

  python tools/xbox/prime_rooms.py <GM8E01 image.iso>             # every world and room
  python tools/xbox/prime_rooms.py <image> --find "landing site"   # rooms whose name matches
  python tools/xbox/prime_rooms.py <image> --env "landing site"    # the autopad.txt line for the first match

Reads the FST, each Metroid*.pak's resource table, every MLVL (the world:
its rooms' MREA ids) and the STRGs that name them. Nothing is written.
"""
import argparse
import struct
import sys
import zlib


def be32(b, o):
    return struct.unpack_from('>I', b, o)[0]


class Disc:
    def __init__(self, path):
        self.f = open(path, 'rb')
        hdr = self.read(0, 0x440)
        if hdr[:6] != b'GM8E01':
            sys.exit(f'not Metroid Prime NTSC-U: {hdr[:6]!r}')
        fst = self.read(be32(hdr, 0x424), be32(hdr, 0x428))
        n = be32(fst, 8)
        names = n * 12
        self.files = {}
        stack = [('', n)]
        i = 1
        while i < n:
            while stack and i >= stack[-1][1]:
                stack.pop()
            flags_name, off, size = struct.unpack_from('>III', fst, i * 12)
            end = fst.index(b'\0', names + (flags_name & 0xFFFFFF))
            name = fst[names + (flags_name & 0xFFFFFF):end].decode('ascii', 'replace')
            path = stack[-1][0] + name if stack else name
            if flags_name >> 24:
                stack.append((path + '/', size))
            else:
                self.files[path] = (off, size)
            i += 1

    def read(self, off, size):
        self.f.seek(off)
        return self.f.read(size)


class Pak:
    def __init__(self, data):
        self.data = data
        o = 8
        n = be32(data, o)
        o += 4
        for _ in range(n):
            o += 8
            o += 4 + be32(data, o)
        n = be32(data, o)
        o += 4
        self.res = {}
        for _ in range(n):
            comp, typ, rid, size, off = struct.unpack_from('>I4sIII', data, o)
            o += 20
            self.res[(typ.decode(), rid)] = (comp, off, size)

    def get(self, typ, rid):
        r = self.res.get((typ, rid))
        if not r:
            return None
        comp, off, size = r
        raw = self.data[off:off + size]
        return zlib.decompress(raw[4:]) if comp else raw


def strg_text(b):
    if b is None or be32(b, 0) != 0x87654321:
        return None
    langs, count = be32(b, 8), be32(b, 12)
    if not count:
        return ''
    o = 16
    english = None
    for i in range(langs):
        lang, loff = b[o:o + 4], be32(b, o + 4)
        o += 8
        if english is None or lang == b'ENGL':
            english = loff
    base = 16 + langs * 8 + english
    s0 = base + 4 + be32(b, base + 4)
    end = s0
    while b[end:end + 2] != b'\0\0':
        end += 2
    return b[s0:end].decode('utf-16-be', 'replace')


def mlvl_areas(b):
    """-> (world name STRG id, [(area name STRG id, MREA id)])"""
    if be32(b, 0) != 0xDEAFBABE:
        return None
    o = 8
    world_name = be32(b, o)
    o += 12                                   # name, save world, skybox
    relays = be32(b, o)
    o += 4 + relays * 11
    count = be32(b, o)
    o += 8                                    # area count, unknown
    areas = []
    for _ in range(count):
        name = be32(b, o)
        o += 4 + 12 * 4 + 6 * 4               # transform, bounds
        mrea = be32(b, o)
        o += 8                                # MREA, internal id
        attached = be32(b, o)
        o += 4 + attached * 2 + 4             # attached areas, a zero
        deps = be32(b, o)
        o += 4 + deps * 8
        layers = be32(b, o)
        o += 4 + layers * 4
        docks = be32(b, o)
        o += 4
        for _ in range(docks):
            conn = be32(b, o)
            o += 4 + conn * 8
            coords = be32(b, o)
            o += 4 + coords * 12
        areas.append((name, mrea))
    return world_name, areas


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('image')
    ap.add_argument('--find')
    ap.add_argument('--env')
    args = ap.parse_args()
    disc = Disc(args.image)
    paks = {n: Pak(disc.read(*disc.files[n])) for n in sorted(disc.files) if n.lower().endswith('.pak')}

    def find(typ, rid):
        for p in paks.values():
            d = p.get(typ, rid)
            if d is not None:
                return d
        return None

    rows = []
    for pname, pak in paks.items():
        for (typ, rid) in pak.res:
            if typ != 'MLVL':
                continue
            parsed = mlvl_areas(pak.get(typ, rid))
            if not parsed:
                continue
            wname, areas = parsed
            world = strg_text(find('STRG', wname)) or pname
            for idx, (aname, mrea) in enumerate(areas):
                rows.append((world, rid, idx, mrea, strg_text(find('STRG', aname)) or f'area {idx}'))
    want = (args.find or args.env or '').lower()
    picked = [r for r in rows if want in r[4].lower() or want in r[0].lower()] if want else rows
    if args.env:
        if not picked:
            sys.exit(f'no room matches {args.env!r}')
        w, wid, idx, mrea, name = picked[0]
        print(f'env MP_BOOT_WORLD={wid:08X}:{mrea:08X}    # {w}: {name}')
        return
    last = None
    for w, wid, idx, mrea, name in picked:
        if w != last:
            print(f'\n{w}  MLVL {wid:08X}')
            last = w
        print(f'  {idx:3}  {mrea:08X}  {name}')


if __name__ == '__main__':
    main()

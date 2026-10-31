#!/usr/bin/env python3
"""Check a Metroid Prime image on the PC, file by file, against what a clean
USA 1.00 (GM8E01 rev 0) holds (xbox/src/sdk/disc_table.inc): the same check
the console makes on an image's first boot from the hard drive
(docs/boot-safety.md). Run it before burning a disc: from a DVD the console
only makes the quick check (size, file table, main.dol).

  python tools/xbox/verify_image.py "<image.iso>"     # exit 0: clean
"""
import pathlib
import re
import struct
import sys
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
TABLE = (ROOT / 'xbox/src/sdk/disc_table.inc').read_text()


def define(name):
    return int(re.search(rf'#define {name} (0x[0-9A-F]+|\d+)u', TABLE).group(1), 0)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    path = pathlib.Path(sys.argv[1])
    f = path.open('rb')
    hdr = f.read(0x440)
    if hdr[:6] != b'GM8E01' or hdr[7] != 0:
        sys.exit(f'not Metroid Prime USA 1.00: {hdr[:6].decode(errors="replace")} rev {hdr[7]}')
    size = path.stat().st_size
    if size != define('DISC_IMAGE_SIZE'):
        sys.exit(f'changed or trimmed: {size} bytes, a clean image is {define("DISC_IMAGE_SIZE")}')
    fst_off, fst_len = struct.unpack_from('>II', hdr, 0x424)
    f.seek(fst_off)
    if zlib.crc32(f.read(fst_len)) & 0xFFFFFFFF != define('DISC_FST_CRC'):
        sys.exit('changed: the file table differs')
    files = re.findall(r'\{ "([^"]+)", (\d+)u, (\d+)u, 0x([0-9A-F]+)u \}', TABLE)
    bad = 0
    for i, (name, off, n, crc) in enumerate(files, 1):
        f.seek(int(off))
        c = 0
        left = int(n)
        while left:
            b = f.read(min(left, 1 << 22))
            c = zlib.crc32(b, c)
            left -= len(b)
        ok = c & 0xFFFFFFFF == int(crc, 16)
        bad += not ok
        print(f'\r{i:3}/{len(files)} {"ok " if ok else "BAD"} {name[:50]:50}', end='' if ok else '\n', flush=True)
    print()
    if bad:
        sys.exit(f'{bad} files differ from a clean dump')
    print('clean: every file matches USA 1.00')


if __name__ == '__main__':
    main()

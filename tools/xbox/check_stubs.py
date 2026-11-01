#!/usr/bin/env python3
"""Fail the build when the link pulled in one of nxdk's "Not implemented" stubs.

nxdk's pdclib defines some libm functions as `assert(0); // Not implemented`.
The game never calls them by name, but clang turns some calls into them
(powf(2.f, x) becomes exp2f), and the first call is an "Assertion failed"
screen in the middle of play (exp2f when the Frigate escape timer ended,
2026-11-01). Implement the function in xbox/src/sdk/libm_fill.c instead.

usage: check_stubs.py <prime_x.map> <NXDK_DIR>
"""
import os, re, sys

def stub_objects(nxdk):
    base = os.path.join(nxdk, "lib", "pdclib", "platform", "xbox", "functions")
    out = set()
    for dirpath, _, files in os.walk(base):
        for f in files:
            if not f.endswith(".c"):
                continue
            with open(os.path.join(dirpath, f), encoding="utf-8", errors="replace") as fh:
                if "Not implemented" in fh.read():
                    out.add(f[:-2] + ".obj")
    return out

def main():
    mapfile, nxdk = sys.argv[1], sys.argv[2]
    stubs = stub_objects(nxdk)
    hits = {}
    with open(mapfile, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = re.search(r"\s(\S+)\s+[0-9a-f]{8,}\s+(?:\S+:)?libpdclib:(\S+\.obj)\s*$", line)
            if m and m.group(2) in stubs and not m.group(1).startswith("??_C@"):
                hits.setdefault(m.group(2), []).append(m.group(1))
    if hits:
        for obj, syms in sorted(hits.items()):
            print(f"check_stubs: nxdk stub {obj} linked ({', '.join(syms)}): "
                  "it asserts when called; define it in xbox/src/sdk/libm_fill.c", file=sys.stderr)
        sys.exit(1)
    print(f"check_stubs: none of nxdk's {len(stubs)} stub objects linked")

if __name__ == "__main__":
    main()

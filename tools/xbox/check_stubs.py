#!/usr/bin/env python3
"""Fail the build when the image can reach one of nxdk's "Not implemented" stubs.

nxdk defines some C library functions as `assert(0)` (pdclib's exp2, cbrt,
log2...; xboxrt's strtod, wcstod, mbrtowc, _snwprintf...). The game never
calls most of them by name, but clang turns some calls into them (powf(2.f,
x) becomes exp2f), and the first call is an "Assertion failed" screen in the
middle of play: exp2f from MusyX when the Frigate escape timer ended
(2026-11-01). Give the function a real body in xbox/src/sdk/libm_fill.c or
libc_fill.c instead.

How: every function in nxdk's sources whose body starts with assert(0) /
assert(false) is a stub. For each stub in the link map, the image is
searched for references to its address: direct calls and jumps (E8/E9
rel32) and the address as a 32-bit value (function pointers, tables). A
reference counts if the function or data holding it is itself reachable,
followed up to a few levels (libc++'s std::stod holds a pointer to strtod,
but nothing calls std::stod: not a problem). Anything not proven dead is
reported. Byte patterns can match by accident, so a hit names the chain to
look at.

usage: check_stubs.py <prime_x.map> <NXDK_DIR> [<prime_x.exe>]
"""
import bisect
import os
import re
import struct
import sys

ASSERT_RE = re.compile(r"assert\s*\(\s*(?:0|false)\s*\)")
HEAD_RE = re.compile(r"\b(\w+)\s*\([^;{}()]*(?:\([^()]*\)[^;{}()]*)*\)\s*$")
COMMENT_RE = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
DEPTH = 4
# reachable but harmless, with why
ALLOW = {
    # only through std::terminate, which ends the program anyway; the assert
    # (libc_fill.c's _xbox_assert) puts up the error screen instead
    "_get_terminate": "std::terminate's handler",
}
# where nxdk's C library and runtime live (not SDL, the samples or the tests)
SCAN = ("pdclib/platform", "xboxrt", "winapi", "hal", "nxdk", "libcxx/src", "xboxkrnl")


def stub_names(nxdk):
    """functions whose body begins with assert(0): name -> source file"""
    names = {}
    base = os.path.join(nxdk, "lib")
    for sub in SCAN:
        for dirpath, _, files in os.walk(os.path.join(base, sub)):
            if "test" in dirpath.lower():
                continue
            for f in files:
                if not f.endswith((".c", ".cpp")):
                    continue
                path = os.path.join(dirpath, f)
                with open(path, encoding="utf-8", errors="replace") as fh:
                    text = fh.read()
                if not ASSERT_RE.search(text):
                    continue
                for m in ASSERT_RE.finditer(text):
                    # the text before it, back to the last '{', must be blank
                    # or comments, and the '{' must close a function's head
                    brace = text.rfind("{", 0, m.start())
                    if brace < 0 or COMMENT_RE.sub("", text[brace + 1:m.start()]).strip():
                        continue
                    head = text[max(0, text.rfind(";", 0, brace), text.rfind("}", 0, brace)) + 1:brace]
                    h = HEAD_RE.search(COMMENT_RE.sub("", head).strip())
                    if h and h.group(1) not in ("if", "while", "for", "switch"):
                        names[h.group(1)] = os.path.relpath(path, base)
    return names


def read_map(mapfile):
    """symbol -> (address, defining object), and the sorted (address, symbol) list"""
    syms = {}
    rows = []
    with open(mapfile, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = re.match(r"\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8,16})\s+(?:f\s+)?(?:i\s+)?(\S+)", line)
            if not m:
                continue
            addr = int(m.group(2), 16)
            if addr == 0:
                continue
            syms.setdefault(m.group(1), (addr, m.group(3)))
            rows.append((addr, m.group(1)))
    rows.sort()
    return syms, rows


def read_pe(path):
    """(image base, [(va, bytes, executable)])"""
    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsz = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    base = struct.unpack_from("<I", data, opt + 28)[0]
    secs = []
    for i in range(nsec):
        s = opt + optsz + i * 40
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, s + 8)
        chars = struct.unpack_from("<I", data, s + 36)[0]
        secs.append((base + va, data[raw:raw + min(vsize, rsize) if vsize else rsize], bool(chars & 0x20000000)))
    return base, secs


class ByteRefs:
    """where each address is referenced from, by bytes: calls/jumps (E8/E9
    rel32) and the address as a 32-bit value. Quick, but an E8 inside another
    instruction can match by accident."""

    def __init__(self, secs):
        self.secs = secs
        self.calls = {}   # target -> [site va]
        self.indexed = set()

    def sites(self, addr):
        if addr not in self.indexed:
            self.index_calls({addr})
            self.indexed.add(addr)
        return self.calls.get(addr, []) + self.values(addr)

    def index_calls(self, targets):
        for va, b, ex in self.secs:
            if not ex:
                continue
            for op in (b"\xe8", b"\xe9"):
                i = b.find(op)
                while i >= 0 and i + 5 <= len(b):
                    t = (va + i + 5 + struct.unpack_from("<i", b, i + 1)[0]) & 0xFFFFFFFF
                    if t in targets:
                        self.calls.setdefault(t, []).append(va + i)
                    i = b.find(op, i + 1)

    def values(self, addr, data_only=False):
        pat = struct.pack("<I", addr)
        out = []
        for va, b, ex in self.secs:
            if data_only and ex:
                continue
            i = b.find(pat)
            while i >= 0:
                out.append(va + i)
                i = b.find(pat, i + 1)
        return out


class DisasmRefs:
    """references from llvm-objdump's disassembly (calls, jumps, immediates,
    displacements naming an address), plus pointers stored in data sections"""

    def __init__(self, by_target, byte_refs):
        self.by_target = by_target
        self.bytes = byte_refs

    def sites(self, addr):
        return self.by_target.get(addr, []) + self.bytes.values(addr, data_only=True)


def disasm_refs(exe, secs, known):
    import shutil
    import subprocess
    tool = None
    for cand in (os.path.join(os.environ.get("LLVM", ""), "bin", "llvm-objdump"), "llvm-objdump"):
        tool = shutil.which(cand) or shutil.which(cand + ".exe")
        if tool:
            break
    if not tool:
        return None
    out = subprocess.run([tool, "-d", "--no-show-raw-insn", exe], capture_output=True, text=True).stdout
    by_target = {}
    line_re = re.compile(r"^\s*([0-9a-f]+):\s+\S+\s*(.*)$")
    num_re = re.compile(r"0x([0-9a-f]{5,8})(?![0-9a-f])")
    for line in out.splitlines():
        m = line_re.match(line)
        if not m or "0x" not in m.group(2):
            continue
        site = int(m.group(1), 16)
        for n in num_re.findall(m.group(2).split("<")[0]):
            t = int(n, 16)
            if t in known:
                by_target.setdefault(t, []).append(site)
    return DisasmRefs(by_target, ByteRefs(secs))


def analyse(linked, rows, addrs, refs):
    """[(name, source, chain)] for each stub not proven dead"""

    def referrers(addr):
        out = {}
        for s in refs.sites(addr):
            a, sym = owner(rows, addrs, s)
            if a != addr:
                out[a] = sym
        return out

    memo = {}

    def live(addr, depth, chain):
        """a chain of referrers up to something not proven dead, or None"""
        if (addr, depth) in memo:
            c = memo[(addr, depth)]
            return None if c is None else chain + c
        r = referrers(addr)
        found = None
        if r and depth == 0:
            found = [next(iter(r.values()))]
        else:
            for a, sym in r.items():
                if a in linked:
                    continue
                c = live(a, depth - 1, [sym])
                if c is not None:
                    found = c
                    break
        memo[(addr, depth)] = found
        return None if found is None else chain + found

    bad = []
    for addr, (name, src) in sorted(linked.items()):
        if name in ALLOW:
            continue
        c = live(addr, DEPTH, [])
        if c is not None:
            bad.append((name, src, c))
    return bad


def owner(rows, addrs, va):
    i = bisect.bisect_right(addrs, va) - 1
    return rows[i] if i >= 0 else (0, "?")


def main():
    mapfile, nxdk = sys.argv[1], sys.argv[2]
    exe = sys.argv[3] if len(sys.argv) > 3 else os.path.splitext(mapfile)[0] + ".exe"
    stubs = stub_names(nxdk)
    syms, rows = read_map(mapfile)
    addrs = [a for a, _ in rows]
    linked = {}
    for name, src in stubs.items():
        # only nxdk's own definition (its source file's object): a body of
        # ours in libm_fill.c / libc_fill.c replaces it
        obj = os.path.splitext(os.path.basename(src))[0].lower() + "."
        for dec in ("_" + name, name):
            if dec in syms and obj in syms[dec][1].lower().split(":")[-1]:
                linked[syms[dec][0]] = (name, src)
                break
    if not linked:
        print(f"check_stubs: none of nxdk's {len(stubs)} stub functions linked")
        return 0
    _, secs = read_pe(exe)
    bad = analyse(linked, rows, addrs, ByteRefs(secs))
    if bad:
        # a byte pattern can match by accident; the disassembly decides
        dis = disasm_refs(exe, secs, set(addrs))
        if dis is not None:
            bad = analyse(linked, rows, addrs, dis)
        else:
            print("check_stubs: no llvm-objdump to confirm with; reporting the byte scan's findings", file=sys.stderr)
    if bad:
        for name, src, c in bad:
            print(f"check_stubs: {name} (nxdk {src}) is assert(0) and reachable: "
                  f"{' <- '.join(c)}; give it a body in xbox/src/sdk/libm_fill.c or libc_fill.c",
                  file=sys.stderr)
        return 1
    print(f"check_stubs: {len(linked)} of nxdk's {len(stubs)} stub functions linked, none reachable")
    return 0


if __name__ == "__main__":
    sys.exit(main())

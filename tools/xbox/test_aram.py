#!/usr/bin/env python3
"""Build and run tests/xbox/test_aram.c on the host: ARAM's disc-backed
pages and swap file (xbox/src/sdk/ar.c) under random writes, reads, faults
and reclaims, against a shadow copy. Usage: test_aram.py [operations]"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def main():
    cc = os.environ.get("CC", "cc")
    with tempfile.TemporaryDirectory() as tmp:
        exe = os.path.join(tmp, "test_aram")
        cmd = [cc, "-O1", "-Wall", "-Wno-unused-function", "-Wno-int-conversion", "-DXHW_TEST_BUILD=1", "-o", exe,
               os.path.join(ROOT, "tests/xbox/test_aram.c"),
               "-I" + os.path.join(ROOT, "tests/xbox/aram_stub"), "-I" + os.path.join(ROOT, "extern/aurora/include"),
               "-I" + os.path.join(ROOT, "xbox/include"), "-I" + os.path.join(ROOT, "xbox/src/sdk")]
        r = subprocess.run(cmd)
        if r.returncode:
            return r.returncode
        return subprocess.run([exe] + sys.argv[1:]).returncode


if __name__ == "__main__":
    sys.exit(main())

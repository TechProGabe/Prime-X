#!/usr/bin/env python3
"""Phase 0.2 spike: compile every MetroidPrimePort game unit on one triple and
count what fails. Throwaway (docs/plan.md 0.2).
  python spikes/cxx/compile_all.py <msvc|gnu> [--jobs N] [--only substr]
"""
import concurrent.futures, os, pathlib, re, subprocess, sys, json, collections

ROOT = pathlib.Path(__file__).resolve().parents[2]
MPP = ROOT / 'ref/MetroidPrimePort'
NXDK = pathlib.Path(os.environ.get('NXDK_DIR', 'C:/xdev/nxdk'))
CLANG = os.environ.get('CLANG', 'C:/msys64/mingw64/bin/clang.exe')
triple = sys.argv[1]
only = sys.argv[sys.argv.index('--only') + 1] if '--only' in sys.argv else None
OUT = ROOT / f'build-spike/{triple}'

srcs = re.findall(r'^\s+(src/\S+\.(?:cpp|c))\s*$', (MPP / 'files.cmake').read_text(), re.M)
srcs = [s for s in srcs if 'NESemu' not in s and 'TypesMatch' not in s] + ['src/MetroidPrime/main.cpp']
if only:
    srcs = [s for s in srcs if only in s]

common = ['-march=pentium3', '-msse', '-mfpmath=sse', '-ffreestanding', '-nostdlib', '-fno-builtin',
          '-DNXDK', '-D__STDC__=1', '-U__STDC_NO_THREADS__', '-DTARGET_PC=1', '-DTARGET_XBOX=1', '-DMUSY_TARGET=0',
          '-fsigned-char', '-fno-exceptions', '-fno-rtti', '-O1',
          '-Wno-everything', '-ferror-limit=0',
          f'-I{MPP}/platform/include', (f'-I{ROOT}/build-spike/inc-sizes' if os.environ.get('SIZES') else f'-I{MPP}/include'), f'-I{MPP}/extern/aurora/include',
          f'-I{MPP}/extern/musyx/include']
if triple == 'msvc':
    common = ['-target', 'i386-pc-win32', *common, '-Xclang', '-fms-memptr-rep=virtual',
              f'-I{NXDK}/lib/libcxx/include', f'-I{NXDK}/lib', f'-I{NXDK}/lib/xboxrt/libc_extensions',
              '-isystem', f'{NXDK}/lib/pdclib/include', f'-I{NXDK}/lib/pdclib/platform/xbox/include',
              f'-I{NXDK}/lib/winapi', f'-I{NXDK}/lib/xboxrt/vcruntime', '-Wno-builtin-macro-redefined']
else:
    common = ['--target=i686-pc-windows-gnu', '-mno-ms-bitfields', *common, '-nostdlibinc',
              '-U_WIN32', '-U__MINGW32__', '-U__MINGW64__', '-UWIN32', '-U__WIN32', '-U__WIN32__',
              f'-I{NXDK}/lib/libcxx/include', '-isystem', f'{NXDK}/lib/pdclib/include',
              f'-isystem{NXDK}/lib/pdclib/platform/xbox/include', f'-isystem{NXDK}/lib/xboxrt/libc_extensions']

def build(s):
    out = OUT / (s + '.obj'); out.parent.mkdir(parents=True, exist_ok=True)
    lang = ['-x', 'c++', '-std=c++20'] if s.endswith('.cpp') else ['-x', 'c', '-std=c2x']
    cmd = [CLANG, *common, *lang, '-include', str(ROOT/'spikes/cxx/prelude.h'), '-include', f'{MPP}/platform/compat.h', '-c', str(MPP / s), '-o', str(out)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    errs = [l for l in r.stderr.splitlines() if ' error: ' in l or 'fatal error' in l or 'evaluates to' in l]
    return s, r.returncode, errs

res = []
with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as ex:
    for s, rc, errs in ex.map(build, srcs):
        res.append((s, rc, errs))
fails = [r for r in res if r[1]]
print(f'{len(res) - len(fails)} / {len(res)} compiled on {triple}')
kinds = collections.Counter()
for s, rc, errs in fails:
    for e in errs[:3]:
        kinds[re.sub(r'^.*?error: ', '', e)[:110]] += 1
for k, n in kinds.most_common(60):
    print(f'{n:4} {k}')
(OUT / 'fails.json').write_text(json.dumps([[s, e] for s, rc, e in fails], indent=1))

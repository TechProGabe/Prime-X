# ThinLTO and PGO

Releases are built with ThinLTO and profile-guided optimization (the user,
2026-10-27: "this needs PGO LTO"; `docs/plan.md` phase 6). Both come from
Melee-X (`ref/Melee-X/docs/pgo.md`), simplified by the one-triple build:
lld-link runs the ThinLTO backends itself, so Melee-X's hand-distributed
`thinlto_link.py` isn't needed.

| knob (`tools/xbox/msys/build.sh`) | build folder | what |
|---|---|---|
| `XBOX_LTO=1` | `build-xbox-lto` | game, port and sdk code as bitcode; ThinLTO in lld-link on every core, modules cached in `lto-cache/` |
| `XBOX_PGO=gen` | `build-xbox-pgogen` | the same code instrumented (`-fprofile-generate`), for training |
| `XBOX_PGO=xbox/prime.profdata` | `build-xbox-pgo` | optimized with the profile |
| both | `build-xbox-lto-pgo` | the release build |

Each kind has its own folder (`tools/xbox/build_dir.sh`), so switching
never recompiles another's objects. hw (`xbox/src/hw`) and pbkit are never
bitcode or instrumented: the code that dumps the counters must not count
itself, and they hold the kernel glue.

## ThinLTO

- Functions the code generator calls on its own must stay native objects:
  `_CxxThrowException` (`px_eh.cpp`, a `throw`), and the libc/maths
  fill-ins (`libc_fill.c`, `libm_fill.c`). In bitcode LTO drops them
  before codegen asks for them (`undefined symbol: __CxxThrowException@8`).
  `xbox/CMakeLists.txt` compiles them with `-fno-lto`.
- Size: with lld's default import limit (100 instructions) `.text` grows
  from 4.76 MB to 6.5 MB, which the 64 MB console can't spare (hw1: 3.7 MB
  free at worst). `-mllvm:-import-instr-limit=10` gives 4.67 MB, 40 gives
  5.63 MB. With the profile, cold call sites import nothing and hot ones
  ten times the limit: LTO + PGO is 5.32 MB at the default and 5.07 MB at
  30, the default here (`PX_LTO_LINK_FLAGS`).

## PGO

The instrumented image has no compiler-rt. `xbox/src/hw/xhw_pgo.c` defines
`__llvm_profile_runtime` and writes the counter section (`.lprfc`) to the
log as `[PGOC]` lines every 1800 frames (`vi.c`, cumulative since boot).
`tools/xbox/pgo_raw.py` rebuilds a raw profile (version 10, 32-bit) from
the last complete dump plus the constant sections of the linked
`prime_x.exe` (records, names, version word). The link drops clang's
`clang_rt.profile.lib` default library and keeps
`___llvm_profile_raw_version`.

Training (`tools/xbox/pgo_train.sh`): the instrumented build plays
`tools/xbox/scenarios/pgo/{tallon,frigate,chozo,phendrana,magmoor}` in
xemu (walk, turn, fire, jump, morph, scan, lock on; two dumps each), up to
three xemus at once (`PX_RUN_2`/`_3`, `PX_XEMU_ARGS_2`/`_3`), then
`llvm-profdata merge` writes `xbox/prime.profdata` and
`xbox/prime.profdata.txt` (commit, scenarios, LLVM version, totals). Branch
counts don't depend on xemu's speed. The instrumented image is ~4 MB
bigger and ran out of memory in Main Plaza, so the training xemus are
configured with 128 MB and an `XHW_PGO` build doesn't hold the upper 64 MB
back (`xhw_mem_hold_upper`).

What must match between training and use, as on Melee-X: the LLVM version
(21.1.8, MSYS2), `-mllvm -static-func-full-module-prefix=false` on both
sides (static functions named by file name alone, the folders differ),
value profiling off in the instrumented build (`-mllvm -disable-vp`), and
otherwise the same compile flags. A stale profile only costs speed:
functions whose control flow changed lose their counts and compile as if
unprofiled. Regenerate after big changes to hot code and before releases.

The profile holds function names (as in `src/`), control-flow hashes and
execution counts of our own build: no game data. It is committed like
Melee-X's.

## Link order

`xbox/order.txt` (`tools/xbox/make_order.py`, written by `pgo_train.sh`)
lays `.text` out with the functions the training ran first, by entry count,
then everything else in link order, so a frame's code shares fewer L2 lines
and 4 KB pages (Melee-X's ordered link: +13%). Melee-X builds its order from
whole-match sampling profiles; Prime's profiler doesn't log those, so this
one takes the PGO counts. Linked only into builds that use the profile
(`-order:@xbox/order.txt`, unknown names skipped). The first one: 4542
functions, 1.67 MB of the LTO + PGO build.

A new `xbox/prime.profdata` restarts the `-pgo` build folder
(`xbox/build.sh` keeps the profile's checksum in it): objects compiled
with two different profiles don't link ("linking module flags
'ProfileSummary': IDs have conflicting values").

# Prime-X

Native original-Xbox port of Metroid Prime (NTSC-U 1.00, `GM8E01` rev 0),
to be built with nxdk and LLVM 21. Not an emulator: the decompiled game
(PrimeDecomp via MetroidPrimePort) is compiled for the Pentium III and drawn
on the NV2A by Melee-X's GX layer.

**State: in development.** The game boots and plays in xemu and on the red
console (round hw5: ~35-45 fps at 480 in most rooms). The work is laid out
as phases with gates in `docs/plan.md`; `docs/roadmap.md` says how far each
got. Start with `docs/handoff.md` (where things stand, what to do next),
then `docs/plan.md`, `docs/decisions.md`, `docs/sources.md`.

## Read first

| doc | why |
|---|---|
| `docs/handoff.md` | current state and the next task; update it at the end of every session |
| `docs/roadmap.md` | each phase's gate and how far it got, known issues, console history |
| `docs/plan.md` | phases 0-7, each with tasks and a gate |
| `docs/decisions.md` | what the user decided and why; scope; edits to imported code |
| `docs/sources.md` | every reference repo, its pinned commit, what is taken from it |
| `docs/reuse-map.md` | Melee-X and MetroidPrimePort file by file: copy, adapt, rewrite or drop |
| `docs/architecture.md` | layers, the C++ question, memory budget |
| `docs/boot-safety.md` | disc validation, 128 MB consoles held to 64 MB, upgraded/overclocked CPUs |
| `docs/ui.md` | boot splash and the in-game settings rows (user-specified look) |
| `docs/performance.md` | 60 fps at 480, 30+ at 720p: targets, benchmark rooms, levers |

## Reference code

`tools/fetch_refs.sh` clones MetroidPrimePort (pinned), Melee-X,
OpenCrossing-Xbox, aurora and nxdk into `ref/` (gitignored). Melee-X is the
template for everything outside the game code: its `CLAUDE.md`, `docs/` and
`tools/xbox/` are the house style. Read them before writing the matching
piece here. Never edit anything under `ref/`; import into this tree instead.

## Planned layout

| path | what | triple (to be confirmed by phase 0's C++ spike) |
|---|---|---|
| `src/`, `include/` | imported game code (MetroidPrimePort `src/`, `include/` at the pinned commit, trimmed) | game |
| `extern/musyx` | MusyX audio engine (MetroidPrimePort's vendored copy) | game |
| `extern/aurora/include` | Dolphin SDK headers the game compiles against | both |
| `xbox/src/sdk` (+ `gx/`) | Dolphin SDK on the Xbox (from Melee-X) | game |
| `xbox/src/hw` | kernel, pbkit, NV2A back end, AC97, USB, log, watchdog, profiler (from Melee-X) | nxdk `i386-pc-win32` |
| `xbox/src/port` | Xbox versions of the `Port*` hooks the game calls (retail default, plus the light extras) | game |
| `xbox/include/xgx.h`, `xhw.h` | the only structs crossing the two triples | both |
| `tools/xbox` | build, xemu runner, symbolizers, console tools, host tests (from Melee-X) | host |

## Rules

- Never commit game data (disc images, DOLs, PAKs, extracted assets, BIOS,
  saves). Commit only when asked. Work on `dev`; `main` is the release branch.
- Every edit to imported game code (`src/`, `include/`, `extern/musyx`) is
  wrapped in `#ifdef TARGET_XBOX` (or reuses MetroidPrimePort's `TARGET_PC`
  path), gets a `PORT_XBOX:` comment, and a line in `docs/decisions.md`
  ("Edits to imported code"). Prefer a one-line hook into `xbox/src/port`
  over inline logic, as MetroidPrimePort does with `platform/`.
- Retail behaviour is the default. Extras (cutscene skip, widescreen HUD,
  FOV) are settings that default to retail where that's the GameCube look.
- New build switches or behaviour changes: update the doc that owns them
  (`docs/plan.md` lists which) and `docs/decisions.md`.
- Don't capture the user's desktop; look at frames through `[FBDUMP]`
  screenshots, as on Melee-X.
- xemu is slow and differs from hardware in timing, audio and memory: frame
  rates are only measured on the consoles.
- Code style: match the surrounding code. Game code is C++ in the decomp's
  style; Xbox layer is C in Melee-X's style. `xgx.h`/`xhw.h` structs must lay
  out identically on both triples.
- Keep `docs/handoff.md` current: what was done, what's next, what's broken.

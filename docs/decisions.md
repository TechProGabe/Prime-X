# Decisions

The choices the port rests on and why. When one changes, update this file.
Same shape as Melee-X's `docs/decisions.md`; its technical decisions carry
over unless a line here says otherwise.

## Scope, agreed at the start (2026-10-27)

| question | decision |
|---|---|
| Code base | MetroidPrimePort (Odrannnn), trimmed: PrimeDecomp's code with its little-endian fixes. Not raw PrimeDecomp (unreachable today, and every PC fix would be redone), not Metaforce (a reimplementation, not the game's code) |
| Disc | USA 1.00 only (`GM8E01`, revision 0), the decomp's primary target. Other revisions and PAL later, if ever |
| Scope | the retail game plus light extras: cutscene skip, edge-to-edge widescreen HUD, FOV. No randomizer, Archipelago, mods, texture packs, Remastered or Dread imports, savestates, third person |
| Video | 480i, 480p, 720p from the dashboard's allowed modes; 16:9 widescreen expected at 480 and at 720 (hor+) |
| Frame rate | 60 fps at 480, 30+ at 720p, as high as possible; the game always ticks at 60 (`docs/performance.md`) |
| Settings | inside the game's own Options screens (title and pause), looking built in like OpenCrossing-Xbox's (`docs/ui.md`) |
| Splash | Melee-X's "TechProGabe Presents..." card remixed as a scan-visor readout (`docs/ui.md`) |
| Disc validation | thorough at boot: header, revision, FST and `main.dol` every boot; every file's CRC on a new image (`docs/boot-safety.md`) |
| 128 MB consoles | always run in 64 MB, as Melee-X |
| Upgraded CPUs | supported: Melee-X's code repair, no timing from the TSC, plain Pentium III code |
| Repo | `TechProGabe/Prime-X`, private for now; `main` releases, `dev` work |
| Title ID | `50580001` ("PX"), so data lives in `E:\UDATA\50580001\` |
| Controller layout | GameCube by position, as Melee-X: A=A, X=B, B=X, Y=Y, White/Black=Z, triggers=analog L/R, right stick=C-stick, remappable per port |
| Players | one (port 1 plays; other ports' pads ignored unless remapped) |

## Technical decisions

Pending from phase 0 (`docs/plan.md`), each to be filled in with its evidence:

- **C++ build model** (0.2): which triple compiles the game's C++, which
  C++ runtime, what crosses to nxdk code.
- **How `TARGET_PC` code is carried** (0.4): all blocks kept with Xbox
  `Port*` implementations, or dropped features stripped at import.
- **Memory model** (0.6): game `new` into `CGameAllocator` in a 24 MB MEM1, as
  on the GameCube.

Carried over from Melee-X as they are (see its `docs/decisions.md`):
LLVM 21.1.8 and nxdk at its pinned commit; compiler builtins for `memcpy`
and co.; frame boundary at `GXCopyDisp`/`VIWaitForRetrace`; alarms and
interrupts on the game thread; MEM1 and ARAM reserved at fixed VAs and
committed on demand; disc-backed ARAM pages; our own `mem*` and kernel disc
reads; big-endian memory-card files; logs that survive long sessions; our
own GX on the NV2A with generated vertex programs and TEV on the register
combiners; ThinLTO + PGO for releases.

Not carried over: `disc_lower` and the `DISC_STRUCT` lowering. Melee keeps
disc data big-endian in memory and melee-pc marks those structs; Prime reads
almost everything through `CInputStream`, which MetroidPrimePort makes read
big-endian, so there is nothing to lower. Data the GPU reads straight from
the disc (display lists, vertex arrays) stays big-endian and the GX layer
swaps it, as Melee-X's does.

## Edits to imported code

One line per edit to `src/`, `include/` or `extern/musyx`: file, what,
why. None yet.

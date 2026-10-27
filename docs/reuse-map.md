# Reuse map

What each piece of Melee-X becomes in Prime-X, and how much of
MetroidPrimePort's port code the game drags along. "Copy" = take as is and
rename; "adapt" = same design, game-specific parts redone; "new" = no
counterpart. Paths are relative to each repo (`ref/<repo>/...`).

## Melee-X `xbox/src/hw` (nxdk triple, ~game-agnostic)

| file | verdict | note |
|---|---|---|
| `nv2a.c`, `nv2a_vp.c`, `nv2a_vpmem.c`, `nv2a_rc.c`, `nv2a_fog.c` | copy | the NV2A back end; Prime extends it (`docs/renderer.md`) |
| `xhw_main.c` | adapt | boot order, Prime's disc screens, `[CPU]` line (`docs/boot-safety.md`) |
| `xhw_splash.c` | adapt | the Prime remix (`docs/ui.md`) |
| `xhw_code_repair.c` | copy | upgraded-CPU BIOS patches |
| `xhw_sys.c` | copy + | memory (fixed-VA lazy commit, 128 MB hold), ACPI timer; add the TSC-rate measurement |
| `xhw_video.c` | copy | dashboard rules, 480i/480p/720p, safe video |
| `xhw_audio.c` | copy | AC97, 32028.5 Hz in |
| `xhw_pad.c`, `xhw_autopad.c` | copy | pads, rumble; scripted input for tests |
| `xhw_crash.c`, `xhw_watchdog.c` | copy | crash and hang reporters; C++ symbol names in the map need demangling (`tools/xbox/sym.py`) |
| `xhw_fbdump.c`, `xhw_overlay.c` | copy | screenshots, FPS counter overlay |
| `xhw_prof.c`, `xhw_perf.c`, `xhw_pmc.c`, `xhw_pgo.c` | copy | profiler, `[PERF]`, PGO |
| `xhw_led.c` | adapt | LED effects on Prime events (energy low, boss defeated) or just off |
| `xhw_string.c` | copy | `mem*` |

## Melee-X `xbox/src/sdk` (game triple)

| file | verdict | note |
|---|---|---|
| `gx/gx_state.c`, `gx_vtx.c`, `gx_tex.c`, `gx_copy.c` | copy + | GX front end; add what phase 0.5 finds missing (aurora's `GXSetArray` size/little-endian flag, draw-sync tokens that really track the FIFO) |
| `os.c` | copy + | alarms, interrupts lock, arenas; `OSGetTime` from the ACPI timer; scaled TSC throttle |
| `dvd.c` | adapt | readers kept; the disc check becomes `docs/boot-safety.md`'s |
| `ar.c` | copy | ARAM, disc-backed pages; plus `ARQ` as aurora's `arq.h` declares it |
| `pad.c`, `settings.c` | adapt | Prime's rows (`docs/ui.md`) |
| `vi.c` | copy | |
| `card.c` | adapt | Prime's save file name and size; byte order is already the game's job (MetroidPrimePort writes the retail format) |
| `card_endian.c` | drop | Melee's field tables |
| `menu.c` | drop | Melee's own settings page; Prime's rows live in the game's Options |
| `features.c` | drop | Melee feature switches |
| `thp.c` | copy / compare | Melee-X's THP vs MetroidPrimePort's `extern/sdk/dolphin/thp`: keep the faster |
| `sdl3_audio.c` | adapt | the glue from the game's audio to `xhw_audio.c`; Prime's comes from MusyX through AI DMA |
| `boot.c`, `log.c`, `stubs.c`, `libm_fill.c`, `simhash.c` | copy | |

## Melee-X tools and build

| path | verdict |
|---|---|
| `tools/xbox/setup.sh`, `docker/`, `msys/`, `xbox/build.sh`, `xbox/CMakeLists.txt` | adapt (C++ game sources, the triple from 0.2) |
| `tools/xbox/compile_game.py` | adapt (C++ flags, force-include) |
| `tools/xbox/xemu_run.sh`, `console.py`, `console_round.py`, `fbdump_to_png.py`, `sym.py`, `prof_report.py`, `census_report.py`, `simh_diff.py`, `pgo_*`, `make_order.py`, `thinlto_link.py`, `package_release.py`, `xbe_title_image.py` | copy (rename `MX_` to `PX_`, `melee_x` to `prime_x`) |
| `tools/xbox/test_*.py` + `tests/xbox/` | copy the GX/NV2A ones; drop `test_card_endian`, `test_anim_mtx`, `test_pobj_mtx` (Melee code) |
| `tools/xbox/scenarios/` | new for Prime (autopad scripts per benchmark room) |
| `tools/lower/` | drop (no `DISC_STRUCT` in Prime) |
| `tools/make-xiso` | copy |
| `.github/workflows/build.yml` | adapt (phase 7) |

## MetroidPrimePort's port code the game calls

344 game files have `#ifdef TARGET_PC` code, referencing ~1400 `Port*`
names. The largest: `PortDebug` (404 references: settings and debug flags
read all over), `PortEntitySchema` (156), `PortParticleMath` (113),
`PortRoomEnv` (87), `PortPointerMenu` (77), `PortDisc` (53),
`PortRemastered` (43), `PortGuid` (38), `PortIsRemastered` (37),
`PortSaveFormat` (36). Phase 0.3 classifies every block (keep, replace,
extra-keep, drop) with `tools/port_blocks.py` and writes the full table to
`docs/port-blocks.md`; this section then gets the summary and the list of
`Port*` headers `xbox/src/port` has to provide.

## Missing SDK functions

Filled in by phase 0.5: every SDK symbol the game needs that Melee-X's
`xbox/src/sdk` doesn't define, with a size estimate.

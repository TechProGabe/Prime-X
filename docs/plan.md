# Plan

Prime-X puts Metroid Prime (GameCube, NTSC-U 1.00, `GM8E01` rev 0) on an
original Xbox the way Melee-X put Melee there: the decompiled game compiled
for the Pentium III, the Dolphin SDK reimplemented on the Xbox, GX drawn on
the NV2A. Most of the Xbox side exists already in Melee-X. The new work is
the game code (C++, not C), Prime's heavier renderer features, MusyX audio,
and performance.

Written 2026-10-27 from MetroidPrimePort `673eacaf` and Melee-X `9860e6e`.
Each phase ends in a **gate**: a check that has to pass before the next
phase starts. Record the result in `docs/handoff.md` and tick
`docs/roadmap.md`.

## Targets (the user's, 2026-10-27)

| target | value |
|---|---|
| Disc | USA 1.00 (`GM8E01`, revision 0) only for v1 |
| Code base | MetroidPrimePort, trimmed (PrimeDecomp's code with its little-endian fixes) |
| Scope | the retail game start to credits, plus light extras: cutscene skip, edge-to-edge widescreen HUD, FOV |
| Video | 480i, 480p, 720p; **16:9 widescreen at 480 and at 720** |
| Frame rate | **60 fps at 480, 30+ fps at 720p**, as high as possible everywhere; the game ticks at 60 always |
| Settings | inside the game's own Options screens (title and pause), looking built in, as OpenCrossing-Xbox does (`docs/ui.md`) |
| Boot splash | Melee-X's "TechProGabe Presents..." card remixed in Prime's visor style (`docs/ui.md`) |
| Hardware | stock 64 MB console; the user's two test consoles (red, gold) as on Melee-X |
| Disc validation | thorough, at boot: wrong, modified or damaged images are named and stopped (`docs/boot-safety.md`) |
| 128 MB consoles | always run in 64 MB, as on Melee-X |
| Upgraded / overclocked CPUs | supported gracefully: BIOS code patches repaired, time never from the TSC |

## Phase 0: groundwork and spikes

Answer the questions that could change the architecture before importing
anything. Each spike is small, throwaway code in `spikes/` (deleted or
folded in after).

0.1 **Toolchain.** Reuse Melee-X's: `tools/xbox/setup.sh`, the Docker image,
    the MSYS2 build, nxdk at Melee-X's `NXDK_SHA`, built with
    `NXDK_CXX=y` (it already is), LLVM 21.1.8. Copy the scripts, rename
    `melee` to `prime`, and build Melee-X's empty-XBE smoke test to prove the
    setup. (`docs/toolchain.md`)

0.2 **The C++ spike (the big one).** Melee's game code is C, so Melee-X
    compiles it with a second triple (`i686-pc-windows-gnu
    -mno-ms-bitfields`) for GameCube struct layout and calls nxdk code through
    plain C structs. Prime's game code is C++ with vtables, static
    constructors, `new`/`delete`, templates and some `std::`. Find out which
    of these works, with a slice of real game code (`Kyoto/Math`, `rstl`,
    `CInputStream`, one `CEntity` subclass with virtuals, a static
    constructor, a `std::vector`):
    - (a) game C++ on the game triple (Itanium C++ ABI), `-fno-exceptions
      -fno-rtti`, no libc++ (game `std::` uses replaced), linked with nxdk;
    - (b) game C++ on nxdk's `i386-pc-win32` triple (MSVC C++ ABI) with nxdk's
      libc++, checking whether `-mno-ms-bitfields` or the decomp's layout
      survives;
    - (c) (a) plus a libc++ built for the game triple.
    For each: does an XBE link, do static constructors run (nxdk's
    `.CRT$XCU`), do virtual calls and `operator new` work in xemu, and do the
    decomp's 598 `CHECK_SIZEOF` asserts pass (on a 32-bit target they should
    match the GameCube sizes: turn them on, they are the layout oracle).
    Pick one and record it in `docs/decisions.md` and `docs/architecture.md`.

0.3 **Inventory the `TARGET_PC` blocks.** 344 game files have `#ifdef
    TARGET_PC` code and they call ~1400 `Port*` names in ~40 subsystems
    (PortDebug 404, PortEntitySchema 156, PortParticleMath 113, PortRoomEnv 87,
    ...). Write `tools/port_blocks.py` to list every block with its file,
    line, the `Port*` names it uses and a class:
    - **keep**: portability (endianness, 64-bit-safe pointer handling, signed
      char, real array sizes for GX, bug fixes such as PortParticleMath,
      PortFloat28, PortTickGeneration, PortGuid, the AGSC sample directory);
    - **replace**: PC platform (SDL, files, threads, ImGui, Aurora internals):
      an Xbox implementation in `xbox/src/port`;
    - **extra-keep**: the light extras the user wants (PortSkipCutscenes,
      widescreen HUD, FOV, aspect);
    - **drop**: everything else (Remastered, Dread Samus, randomizer,
      Archipelago, mods, savestates, third person, free cam, photo mode,
      touch, mouse, gyro, Discord, texture packs, debug console).
    Output `docs/port-blocks.md` (generated table) and a summary in
    `docs/reuse-map.md`. Also check whether `decomp/` and the matching build
    (`TARGET_PC` off) are still clean; they are the retail reference.

0.4 **Import strategy.** From 0.3, choose between:
    - define `TARGET_PC` and `TARGET_XBOX`, keep all blocks, and give the Xbox
      a slim implementation of every `Port*` header the game includes
      (dropped features return "off"), or
    - strip dropped blocks out of the imported sources with an unifdef-style
      pass driven by a feature list (`MP_FEATURE_REMASTERED=0`, ...), so the
      imported tree carries only what Prime-X uses.
    The first keeps future MetroidPrimePort syncs mechanical; the second keeps
    the Xbox code small. Recommendation going in: the first, with the
    `Port*` surface for dropped features reduced to inline stubs in one
    header. Record the choice.

0.5 **SDK surface.** Build the game objects (any spike triple) and list
    every undefined SDK symbol (GX, OS, DVD, ARQ/AR, AI, DSP, PAD, SI, VI,
    CARD, THP, MTX). Diff that against what Melee-X's `xbox/src/sdk`
    implements and against its `extern/aurora/include` snapshot (MetroidPrimePort's
    aurora headers add `arq.h`, `gba.h` and some GX extensions, such as
    `GXSetArray` with a byte size and a little-endian flag). Output: the
    missing-function list, sized, in `docs/reuse-map.md`.

0.6 **Memory model.** On the GameCube, Prime's heap is `CGameAllocator` in
    24 MB of MEM1 and `new` goes there. MetroidPrimePort makes `rs_new` host
    `new` and raises MEM1 for its extras. On the Xbox, route game `new`
    back into `CGameAllocator` (32-bit pointers mean GameCube-sized objects,
    so the retail 24 MB should hold) and keep MEM1 and ARAM reserved at fixed
    addresses and committed on demand, as Melee-X does. Write the budget into
    `docs/architecture.md` and confirm it in phase 2.

**Gate 0:** decisions for 0.2, 0.4 and 0.6 are recorded with the evidence;
the spike XBE with real game classes runs in xemu and prints the
`CHECK_SIZEOF` result; the toolchain builds on this PC (MSYS2) and in Docker.

## Phase 1: import and compile

1.1 Repo layout per `CLAUDE.md`: `src/`, `include/`, `extern/musyx`,
    `extern/aurora/include` from MetroidPrimePort at the pinned commit
    (`src/UPSTREAM_COMMIT`); `xbox/` and `tools/xbox/` from Melee-X at the
    commit in `docs/sources.md`; `COPYING` (GPL-3.0) from Melee-X.
1.2 Rename the Melee-X layer: `melee_x` to `prime_x`, title ID `4d580001`
    to `50580001` ("PX"), title name, log tags kept. The game-specific parts of
    Melee-X's SDK (`card_endian.c` field tables for Melee's save, Melee's
    scenarios, `features.c`, `menu.c`) are set aside, not ported: their
    Prime counterparts come later.
1.3 Game build: the trimmed source list (from MetroidPrimePort's
    `files.cmake` minus dropped files) compiled by `tools/xbox/compile_game.py`
    on the triple chosen in 0.2, with `platform/compat.h`'s Xbox twin
    force-included.
1.4 `xbox/src/port`: the `Port*` implementations from 0.4 (retail defaults).
1.5 Link with stubs for every missing SDK function from 0.5 (each stub logs
    once and returns a safe value), so the XBE links before the SDK is done.

**Gate 1:** every game translation unit compiles; `default.xbe` links; the
`CHECK_SIZEOF` asserts are on and pass; host tests copied from Melee-X
(`test_tex_convert`, `test_vp_encoder`, `test_rc`, `test_fog`, `test_pool`)
pass in this tree.

## Phase 2: boot to the title screen

2.1 Boot: `xhw_main` (Melee-X) to the game's `main` (`CMain`), the splash
    (`docs/ui.md`), logs in `E:\UDATA\50580001\`, crash and hang reporters,
    `[MEM]` lines.
2.2 Memory: MEM1 24 MB and ARAM 16 MB at fixed VAs, lazily committed;
    `OSAllocFromArenaLo` and the arena bounds as on the GameCube;
    128 MB consoles held to 64 MB.
2.3 DVD: Melee-X's `dvd.c` (.iso, .gcm, .ciso next to `default.xbe`, kernel
    reads); disc check for `GM8E01` rev 0 with a clear on-screen message for
    any other disc; `CDvdFile` async reads on a worker with completions on
    the game thread, as Melee-X does. The two default-font resources come from
    the retail `main.dol` on the disc at the addresses in
    `decomp/config/GM8E01_00/symbols.txt` (MetroidPrimePort's way).
2.4 OS: alarms, interrupts as a recursive lock, threads (Prime has few: DVD,
    audio), `OSGetTime`/ticks, `OSReport` to the log.
2.5a **Boot safety (the user's requirements, `docs/boot-safety.md`):**
    - **Disc validation.** A quick check on every boot (header, revision,
      FST against a committed table of every file's offset, size and CRC32)
      that refuses any other disc or a modified, trimmed or truncated image
      with a screen that says which; a full CRC check of every file on the
      first boot of each new image, shown as the splash's scan bar, cached,
      re-runnable from Options; the result and the Redump match in
      `boot.log`.
    - **128 MB consoles run in 64 MB**, always, no setting (Melee-X's
      `xhw_mem_hold_upper`, first thing at boot).
    - **Upgraded and overclocked CPUs.** Melee-X's code repair (BIOS
      `rdtsc` rewrites put back before anything reads the TSC), game time
      from the ACPI timer and VBlank, never the TSC; the TSC rate measured
      at boot and every TSC constant scaled by it; built for a plain
      Pentium III (SSE1, no SSE2); a `[CPU]` line with what was found.
2.5 VI and video modes: Melee-X's `vi.c`/`xhw_video.c` (dashboard rules, BACK
    held for safe video, 720p R5G6B5 + Z24S8).
2.6 GX: Melee-X's GX front end and NV2A back end, plus whatever the title
    screen needs from 0.5's missing list.

**Gate 2:** in xemu, boot through the splash to the "Press Start" title with
its 3D background and the file select. Then the same on the red console from
`E:\Applications\Prime-X\`, `boot.log` clean, `[MEM]` within budget. The
boot-safety checks pass their own tests: every rejection screen in
`docs/boot-safety.md` shown by a doctored image in xemu, the full check
matching the Redump hash on a good image, `[MEM]` showing 64 MB on a
128 MB BIOS in xemu, and `XHW_CODE_REPAIR_TEST` repairing its own patch.

## Phase 3: renderer parity

Prime uses GX far harder than Melee. Work through `docs/renderer.md`'s
feature list, each item compared with Dolphin screenshots of the same spot
(`[FBDUMP]` shots vs Dolphin frame dumps):

3.1 World geometry: `CCubeModel`/`CCubeSurface` display lists (big-endian, from
    the disc), lightmaps, up to 8 TEV stages per material, `CCubeMaterial`
    blend modes, reflection (env) maps, alpha compare and punch-through.
3.2 Skinned models: Prime skins on the CPU into host-endian workspace arrays
    (`GXSetArray` little-endian flag, draw-sync tokens free the workspace).
3.3 Fog: room fog and `CFogVolume`s (they use the depth buffer: Melee-X's Z
    textures).
3.4 EFB copies: visor and camera filters (`CCameraFilter`, `CCameraBlurPass`),
    the pause-screen blur, world and projected shadows (`CWorldShadow`,
    `CProjectedShadow`, `CMorphBallShadow`), the space-warp and heat effects,
    `CWorldTransManager` (elevator and door transitions).
3.5 Visors: thermal (I8 EFB copy looked up through a palette), X-ray,
    scan visor, combat visor helmet and HUD.
3.6 Indirect texturing: heat haze, water and fluid surfaces
    (`CFluidPlaneCPU`), the Phazon suit effect.
3.7 Particles: `CElementGen` (immediate mode, many small draws: Melee-X's draw
    merging), electric and swoosh effects.
3.8 Movies: THP (`CCubeMoviePlayer`, attract and credits) on Melee-X's `thp.c`.
3.9 Widescreen: hor+ at 16:9 for 480 and 720 (the camera's aspect, lock-on
    zone scaling, MetroidPrimePort's aspect fix), plus the edge-to-edge HUD
    extra.

**Gate 3:** the benchmark rooms in `docs/performance.md` render like Dolphin
in all four visors, at 480p 4:3, 480p 16:9 and 720p, in xemu and on the red
console.

## Phase 4: audio, input, saves

4.1 Audio: MusyX's software synth and Prime's streamed music (`CDSPStream` /
    `CStreamAudioManager`, ADPCM from the disc) into Melee-X's AC97 driver
    (`xhw_audio.c`: 32028.5 Hz in, resampled to the AC97's 48 kHz). AI DMA
    callbacks on the game thread (MetroidPrimePort's rule). ARAM sample groups.
4.2 PAD: Melee-X's pad layer, Prime's mapping (README table), analog
    triggers with a click threshold (lock-on is the L click, free aim the
    R click), rumble, per-port remapping in `settings.ini`.
4.3 CARD: `.gci` in `E:\UDATA\50580001\card_a\`, written to a temp file and
    swapped; Prime's save stays big-endian on the card (MetroidPrimePort
    already writes the retail CRC and MSB-first bit fields); Dolphin and real
    card saves load, and ours load in Dolphin.

**Gate 4:** on the red console, from a new file: the Frigate Orpheon to the
Tallon landing site with music and sound, save at the ship, power cycle,
load; a Dolphin `.gci` loads; our save loads in Dolphin.

## Phase 5: settings, splash, extras

5.1 Settings rows in the game's own Options screens, title and pause
    (`docs/ui.md`): video output, widescreen, widescreen HUD, FOV, rumble,
    dead zones, trigger threshold, buttons, FPS counter, screenshots, front
    LED, skip cutscenes. `settings.ini` as on Melee-X.
5.2 The Prime-style splash (`docs/ui.md`).
5.3 The extras: cutscene skip (MetroidPrimePort's `PortSkipCutscenes`),
    widescreen HUD, FOV.
5.4 Dashboard icon (`xbox/assets/logo.png` from `tools/xbox/make_logo.py`,
    `default.tbn`, `TitleImage.xbx`).

**Gate 5:** every row changes what it says on the console, survives a
restart, and the title screen's rows are reachable without a save file.

## Phase 6: performance

`docs/performance.md` has the targets, rooms and levers. Port Melee-X's
measuring kit first (`[PERF]`, `[NV2A]`, the profiler, console rounds,
`simh`), then work the biggest costs per room until the targets hold.

**ThinLTO + PGO from the start of this phase** (the user, 2026-10-27:
"this needs PGO LTO"): `XBOX_LTO=1` (ThinLTO in lld-link; Melee-X's
hand-distributed `thinlto_link.py` isn't needed on one triple) and
`XBOX_PGO=gen|<profdata>` (`xhw_pgo.c`, `tools/xbox/pgo_train.sh`,
`pgo_raw.py`) ported to the one-triple build (`docs/pgo.md`), the profile trained on the
benchmark rooms (`docs/performance.md`) by autopad scenarios, so every
measurement in this phase is of the build that ships. Round hw1 (red) found
the game CPU-bound with the GPU idle: Tallon ~10 fps, Chozo's Main Plaza ~5.

**Compiler optimizations: what to expect** (the user, 2026-10-27: "add
compiler optimizations to plan expectations"). Each is measured by itself on
a console round before the next; the gains below are expectations to check,
from Melee-X's history and the usual range for branchy game code on a
Pentium III (16 KB L1 code cache, 128 KB L2), not promises.

| step | what | expected | state |
|---|---|---|---|
| builtins back | nxdk's wrapper passes `-ffreestanding -fno-builtin`: in game code `fabsf`, `sqrtf`, `floorf` and fixed-size `memcpy`/`memset`/`memcmp` were library calls (`fabsf` alone ~1% of the Landing Site's samples). `-fhosted -fbuiltin -fno-math-errno` after the wrapper's flags makes them `andps`, `sqrtss` and inline moves; results don't change (both are correctly rounded) | 3-6% of game CPU | in progress |
| ThinLTO | cross-module inlining of the decomp's many small accessors (`rstl`, `CVector3f`, `CTransform4f`, `CGX` state) | 5-10% | built (`docs/pgo.md`) |
| PGO | branch layout, hot/cold splitting, inlining by measured counts; trained in xemu on the benchmark rooms | 10-20% | first profile (Tallon, Frigate, Phendrana) |
| import limit with PGO | lld's default import limit grows `.text` 1.75 MB; 30 with the profile keeps it at +0.3 MB | keeps memory | default |
| profile-ordered link | hot functions packed together (`xbox/order.txt` from the PGO counts, `/order`): fewer L2 and TLB misses with 5 MB of code | ~10% (Melee-X +13%) | next |
| frame pointers | already omitted at `-O2` on this triple (checked) | - | done |
| `-fno-strict-aliasing`, `-fwrapv`, `-ffp-contract=off` | kept: the decomp's type punning, and Gekko-like float results | - | kept |
| SSE intrinsics by hand | `CTransform4f`/`CMatrix4f` products, skinning (`BuildBoneRange`), particle updates; SSE1 only | per hot spot, measured | later levers (`docs/performance.md`) |
| identical code folding | stays off (`-opt:noicf`): the game compares function pointers | - | off |

Together the compiler steps should be worth roughly 25-40% of CPU time; the
rest of the gap to 60 fps (Tallon was ~10 fps on hw1) has to come from the
levers in `docs/performance.md` (draw batching, GPU skinning, the
immediate-mode path), which is why they run in parallel with these.

**Gate 6:** on the red and gold consoles, every benchmark room holds 60 fps
at 480p (4:3 and 16:9) and at least 30 at 720p, with the game at full speed,
measured on ThinLTO + PGO builds (as releases are).

## Phase 7: whole game and release

7.1 A full playthrough on hardware (100% items, every boss), saving at every
    station; scenario scripts for each world (`tools/xbox/scenarios/`).
7.2 Burn test: hours in the heaviest rooms, memory steady (`[MEM]`), no hangs.
7.3 Release: GitHub Actions build (Melee-X's workflow), `package_release.py`,
    `tools/make-xiso`, release notes, README finished from the planning version.

**Gate 7:** the playthrough and burn test pass on both consoles; v1 tagged
on `main`.

## Risks

| risk | impact | what to do |
|---|---|---|
| C++ ABI across the two triples (0.2) | could force a different build model | the spike decides before any import; worst case, the game and its libc++ on one triple, with C-only crossings (`xgx.h`/`xhw.h`) as on Melee-X |
| `TARGET_PC` code assumes a PC (threads, `std::filesystem`, exceptions, SDL) | compile and runtime failures | 0.3 classifies every block; `-fno-exceptions`; replace, don't emulate |
| 64 MB | Prime's 24 MB MEM1 + 16 MB ARAM + 720p framebuffers + texture pool is tight | commit on demand, disc-backed ARAM pages (Melee-X `ar.c`), measure in phase 2 |
| CPU: skinning, particles, collision, MusyX mixing, ADPCM on a 733 MHz P3 | below 60 fps at 480 | phase 6 levers; SSE paths; skinning and mixing are the first suspects |
| EFB-copy-heavy effects (visors, shadows, blur) | correctness and GPU time | Melee-X has GPU EFB copies and Z textures; extend, measure per effect |
| MetroidPrimePort moves fast (v0.34 on 2026-10-26) | sync cost | pin a commit; sync deliberately with `tools/port_blocks.py` re-run |
| PrimeDecomp upstream unreachable (GitHub 404, axiodl 502 on 2026-10-27) | none for now | MetroidPrimePort carries the decomp; retry when needed |
| Legal | | no game data in the repo, ever; same stance as Melee-X |

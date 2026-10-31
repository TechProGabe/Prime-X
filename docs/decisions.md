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

- **C++ build model** (0.2, 2026-10-27): **one triple for everything,
  nxdk's `i386-pc-win32` (MSVC C++ ABI) with nxdk's libc++.** Spike
  (`spikes/cxx`, throwaway): all 633 game units compile on it (622 at once,
  the rest PC-platform code); the spike XBE ran in xemu with static
  constructors, virtual calls, `new`, `std::map`/`string`/`recursive_mutex`/
  `chrono`, `CMemoryInStream` big-endian reads and `CTransform4f` maths all
  correct. Melee-X's game triple (`i686-pc-windows-gnu`) fails outright for
  C++: nxdk's libc++ has no thread API or object format there, and a libc++
  would have to be built for it. MetroidPrimePort already builds with MSVC,
  so the MSVC ABI is proven for this code; its `/vmg` becomes
  `-Xclang -fms-memptr-rep=virtual` (CAi's member-function pointers).
  Layout: `CHECK_SIZEOF` passes 540 of 598 on this triple; the 58 that
  differ are MetroidPrimePort's own `TARGET_PC` members (CModelData +148,
  CElementGen +712, ...) and the 16-byte general PTMF (CAiState +4), none a
  correctness problem (MetroidPrimePort runs with 64-bit-sized structs), so
  the asserts stay off and the spike's count is the record.
  Exceptions: `-Xclang -fcxx-exceptions` without `-fexceptions`: `throw` and
  `try` compile unchanged, no unwind tables, catch arms are dead, and
  `_CxxThrowException` (`xbox/src/port/px_eh.cpp`) logs the type and
  `what()` and stops with an error screen. Every MetroidPrimePort throw is a
  broken-data condition. No RTTI (`-fno-rtti`; nxdk has no
  `__RTDynamicCast`): the 5 `dynamic_cast`s are replaced (edits below).
  The SDK and hardware layers are C on the same triple; `xgx.h`/`xhw.h`
  stay the boundary as on Melee-X.
- **How `TARGET_PC` code is carried** (0.4, 2026-10-27): **all blocks kept**,
  `TARGET_PC` and `TARGET_XBOX` both defined. `tools/port_blocks.py`
  (`docs/port-blocks.md`): 1718 blocks in 342 files, keep 1405, replace 125,
  drop 160, extra 28. MetroidPrimePort's `platform/include` headers are
  imported as they are into `xbox/src/port/include`; a header that can't
  compile on nxdk gets a slim twin there (`port_map_side.h`,
  `port_paths.h`). The hooks are, in order of preference: MetroidPrimePort's
  own portable file (`disc.cpp`, the THP decoder), a hand-written Xbox
  version (`port_debug.cpp`: every value is MetroidPrimePort's "Original
  experience" value, i.e. retail; `port_gci.cpp`), or a generated "off"
  definition (`tools/xbox/gen_port_stubs.py` from the link's undefined list
  -> `port_stubs_gen.cpp`, ~320 hooks; each logs `[STUB] name` once when the
  game reaches it). Regenerate after a sync; review any whose zero is not
  "off".
- **Test hooks are kept** (user, 2026-10-27: "make sure we can easily level
  skip... any useful port debug should be added"): MetroidPrimePort's
  `MP_BOOT_WORLD=<MLVL>[:<MREA>]`, `MP_FAST_BOOT`, `MP_SKIP_CUTSCENES`,
  `MP_CUTSCENE_SPEED`, `MP_INVULNERABLE` and the teleport requests answer on
  the Xbox, set by `env` lines in a test build's `autopad.txt` (xemu and
  console alike). `docs/testing.md` "Jumping to a room".
- **Saves never stall a frame** (user, 2026-10-27): Melee-X's background
  card writer (`card.c`, its v63 START-hitch fix) is kept as is; check the
  `[CARD] saved ... (N ms)` lines and `[PERFW]` around a save on hardware.
- **The hard drive stays off the game thread** (2026-10-31, the user: gold
  has a stock-like drive, "what probably most people use, and there's not
  a lot of free space. So if that's a common issue, we should address
  it"). Round hw17 on gold (E: 456 MB free of 4881): a 900 KB screenshot
  took 26-49 s to write, the game stalled ~1.3 s every 600 frames (the
  stats lines written and flushed on the game thread), froze ~6 s at room
  loads (ARAM pages read back from the disc image inside ARQPostRequest),
  and ~1.5-3 s at memory reclaims (swap-file writes). Now: boot.log goes
  through a 256 KB queue and a writer thread (`xhw_sys.c`; crash, hang,
  fatal and exit paths still drain it to disk before going on, and so does
  every line of the first 600 frames, the boot; a queue the drive can't
  keep up with drops lines, counted in a `[LOG]` line). The DVD read
  queue holds 1024 reads, waiting and done (`dvd.c`): at 64 a room load's
  hundreds of async resource reads (`CResLoader::LoadResourceAsync`,
  `CGameArea::StartStreamIn` streaming the next rooms) left the game
  thread waiting in `enqueue` for the drive (xemu, every image read
  slowed to 40 ms: two 17 s freezes while Main Plaza streamed the rooms
  around -> none; the room sweep peaks at 671 reads). ARQ completions are
  a list through the requests' `next` (the GameCube's queue has no bound:
  a 128-entry array overflowed with slow reads). Tried in round hw18 and
  backed out for the release candidate: ARQ copies on a worker thread
  (`XSDK_ARQ_ASYNC`, now 0: gold froze twice at 720p with them on; red
  ran clean), and the reclaimer's swap-file writes on that worker (the
  game thread, not seeing the memory come back that frame, asked again
  every frame and the worker went on to the hot chunks, which came
  straight back: gold churned its drive and hung for minutes). Test
  builds: `env PX_IMG_DELAY_MS=n` (each image read waits n ms), `env
  PX_LOG_DELAY_MS=n` (each boot.log write), `env PX_ARQ_ASYNC=1`. The
  first load from the menu into a world waits for its reads, as on the
  GameCube.
- **No boot.log in release builds unless asked** (2026-10-31, the user:
  "It's possible logging is adding to hdd problems, let's make sure on
  official release logging is on port settings with default off"):
  settings.ini `[system] log_file` (default 0), the title screen's Port
  Options > System > Log File (Off / On, at once). Until settings.ini is
  read the lines wait in memory, then go to boot.log or are dropped. Test
  builds always write it (the console rounds read it). Crash and hang
  reports (crash.log, hang.log) are still written: only after a fault.
- **Frames** (2026-10-27): MetroidPrimePort's `CGraphics::EndScene` closes
  "the aurora frame" after `GXCopyDisp`; `aurora_end_frame` is the Xbox frame
  boundary (`vi.c`: 60 Hz pacing, alarms, retrace callbacks), and
  `GXEnableBreakPt` completes the GameCube's XFB swap handshake at once
  (`prime_sdk.c`). `OSYieldThread` delivers pending DVD/ARAM/card
  completions, as their interrupts did. Draw-sync tokens read back as soon
  as they are set (the GX front end reads vertex data at draw time).
- **Build layout**: the top-level `CMakeLists.txt` only includes
  `xbox/CMakeLists.txt`, so object paths are short; each part is a static
  library linked with `-wholearchive`, the game split in two (Windows'
  32 KB command line).
- **Memory model** (0.6, done 2026-10-28 after round hw5 ran out of
  memory): game `new` (MetroidPrimePort's `rs_new` is plain `new`) goes into
  `CGameAllocator` in MEM1 once the heap is up, malloc when it is full or
  not up yet (`px_new.cpp`); a block is freed by the heap whose range holds
  it. MEM1's arena is reserved 32 MB (to ARAM's VA), the game told 24 MB;
  only touched 64 KB chunks cost RAM, and a freed block gives its whole
  chunks back. Before: two heaps, each keeping its own high-water mark
  (the Landing Site: `new` 15.5 MB in malloc, MEM1 ~8 MB); after: one heap
  of ~20 MB live there, malloc 2.5 MB.

Carried over from Melee-X as they are (see its `docs/decisions.md`):
LLVM 21.1.8 and nxdk at its pinned commit; compiler builtins for `memcpy`
and co.; frame boundary at `GXCopyDisp`/`VIWaitForRetrace`; alarms and
interrupts on the game thread; MEM1 and ARAM reserved at fixed VAs and
committed on demand; disc-backed ARAM pages; our own `mem*` and kernel disc
reads; big-endian memory-card files; logs that survive long sessions; our
own GX on the NV2A with generated vertex programs and TEV on the register
combiners; ThinLTO + PGO for releases.

- **Game Timing: Auto** (2026-10-30, the user's policy in
  `docs/fps-plan.md`): the only mode. 60 game updates a second while the
  frame's CPU time fits in 16 ms; when 3 of 4 frames don't, 30 a second,
  drawn at 60 between the last two (MetroidPrimePort's frame interpolation:
  actors, skinned poses, particles, swooshes, beams, the camera); back to 60
  when a 60 Hz frame (a tick's measured cost plus the frame's draw work)
  has been under 14 ms for 30 frames, after at least a second at 30
  (`port_debug.cpp`, `px_sim_frame` from vi.c's frame boundary). At 60 Hz it
  is Fixed exactly. At 720p it stays at 30 a second (2026-10-31, the user's
  call: "at 720p keep the frame tick logic at 30 fps", "at 480 variable is
  still preferred"): gold's 720p play ran 22-50 fps and Auto switched back
  and forth, each switch a stutter. The Port Options row (Auto / Fixed / Flex) was removed
  the same day, the user's call: "Auto approved, make it default and remove
  options from settings". Fixed (retail: 60 a second, a slow frame runs
  more) stutters in fights; Flex (one update a frame, dt = the frame) moves
  physics and timers off retail. Both remain only as test-build hooks for
  A/B rounds (below). Under Auto at
  30 Hz the world is drawn up to a tick (33 ms) behind the simulation and the
  pad is read at 30 Hz; `pad.c` latches presses seen at a frame boundary
  no read followed, so a tap isn't lost between two reads. `SimRate`/
  `SimPeriod` stay 60: the projectile weapons step in 1/60 s substeps at
  any tick. `CStateManager::xf80_hudMessageFrameCount` (the plan's HUD memo
  worry) counts unpauses, not ticks: nothing to scale. settings.ini's
  `timing` and the older `game_timing` are ignored (and no longer written).
  Test builds: `env PX_SIM_AUTO=0|1`, `PX_SIM_RATE=30|60` (held),
  `PX_INTERP=0|1`, `PX_SIM_ADAPTIVE=0|1`; lockstep runs hold 60 Hz unless
  `PX_SIM_RATE` says otherwise.

- **FPS plan, GX layer and back end** (2026-10-30, `docs/fps-plan.md`;
  each with its switch):
  - `GXSetArray` of the array already set returns before closing the
    batch or re-signing the arrays (M3.4): the same state either way.
  - The pushbuffer kick (M3.5): 32768 words while the GPU was idle at the
    last 16 frame starts, 8192 (as before) otherwise (`XGX_PB_KICK_IDLE`,
    `-DXGX_PB_KICK_IDLE=8192` turns it off). Judged on red only.
  - GPU skinning's vertex memory (M4): its lists may hold 1 MB of the
    vertex pool (`XGX_GPU_SKIN_KB`) and never its last 512 KB; the room's
    cached lists evict them first when they need room. A model that falls
    back to the CPU four times (texgens the slots can't serve) skins on
    the CPU from then on: it was drawing half its lists on the GPU and then
    skinning anyway, every frame (round hw12, Main Plaza). When the vertex
    pool is short (Main Plaza: the room's lists fill it, every skin went to
    the CPU in hw13) the lists go to the texture pool above the 2 MB its
    textures keep, as spilled display lists do, and textures needing the
    room take it back (`xgx_vbuf_alloc_spill`, `gx_dlc_evict_spilled`).
  - Draw merging (M3.1, `-DXGX_MERGE=0`): a draw that changes no state
    joins the one before into its BEGIN/END when the primitive (lists,
    quads, lines, points; strips stitched with degenerate triangles),
    layout and vertex window match and nothing else went into the
    pushbuffer between. xemu's Main Plaza: 16% of the unchanged draws join
    (3% of all); most of the rest follow a draw of another primitive
    (`[NV2A] unchanged draws not joined:`). Turning strips into lists would
    join more but triples their vertex memory.
  - `-O3` for the game, the SDK and the LTO backends (`PX_OPT_LEVEL`,
    default 3 since round hw13): A/B on red, same code, same scenes:
    pirates 32.2 -> 33.4 fps, Main Plaza 36.3 -> 36.7, Lava Lake 40.9 ->
    41.6, Landing Site 35.8 -> 36.7; no fault in either image.
  - Texture-only draws (`XGX_MAPS_FAST`, `-DXGX_MAPS_FAST=0`): when only
    the bound textures changed and the texture units stayed the same, the
    vertex program's key and the texgen rows are not worked out again (the
    rows are when a unit's linear texture changed size). The same constants
    and program either way.
  - Static memory (M3.3, `-DXGX_STATIC_LISTS=0`): a cached display list
    read wholly from registered static memory (a room's geometry, while
    `CCubeRenderer` has it) skips its content checks; unregistering drops
    those lists before the memory is freed or moved.
  - Texture lookups by object (`XGX_TEX_MEMO`, `-DXGX_TEX_MEMO=0`;
    `-DXGX_TEX_MEMO_VERIFY=1` checks every hit against the full lookup):
    the same GXTexObj (address and contents) finds the same cache entry and
    mip count until any entry is added, dropped or moved. Revalidation of
    the texels runs as before. xemu's Main Plaza: ~6 million hits, none
    differing from the full lookup.
  - [PERF]'s buckets: a cached list's draws stay in "dlist" (no nested
    "draw" bracket, two `rdtsc` and their bookkeeping a draw less); "draw"
    is the immediate-mode draws' back end. Frame totals and Auto's
    decisions are unchanged.
  - Vertex programs that cover (`XGX_VP_COVER`, `-DXGX_VP_COVER=0`): a
    draw may run a resident program that is its own with more texture units
    fed (same key otherwise; the extra texgens write outputs no stage
    samples) instead of loading its exact one. xemu's pirate fight: loads
    25 -> 12 a frame, instructions loaded 1084 -> 464, switches 46 -> 28.
  - The rain's lines (M5): `GXPortRainLines` keeps its batch's box, and
    `clip_lines` tests the box's corners once; a batch wholly inside skips
    the per-line test (`-DXGX_LINE_BOX=0`). Same vertices drawn. The test
    itself runs four 16-byte vertices at a time in SSE (`clip_codes4`,
    `-DXGX_LINE_SSE=0`), the same operations in the same order: a check
    build compared its codes with the scalar ones on ~24 million lines of
    xemu's rainy fight, none different.
  - The visor passes' GPU time (M5.6's measurement; test builds' `env
    PX_GPU_TIME=1`): the back end waits for the GPU before each EFB copy
    and at the present and logs `[GPUT]` per segment of the frame (the
    wait, the CPU time before it). It serializes the frame, so only its
    per-segment waits mean anything; off unless asked for.
  - Ship builds read no clock around a display-list call or an
    immediate draw (`xgx_draw`'s "draw" bucket the same way)
    (`GXCallDisplayList`): test builds keep "dlist" in [PERF] (two `rdtsc`
    a call, ~1500 calls a frame in Main Plaza); a ship build only tags the
    bucket so the list's draws stay unbracketed, and the list's time lands
    in render or dlist, whose sum is what Game Timing: Auto reads. A ship
    build's [PERF] split between the two is therefore not meaningful.
  - 720p's vertex ring in the video library's framebuffer
    (`XGX_RING_IN_VFB`, 2026-10-31, for the user's 720p hitching):
    XVideoSetMode's 1800 KB buffer is idle once pbkit draws into its own;
    only the crash, hang and fatal-error screens show it, and those end
    the game. The 1.5 MB ring now lives there at 720p (480's buffer is
    too small). Gold's 720p play had ~3.8 MB less free than 480 and
    swapped ARAM to the disk throughout (432 chunks out, 346 read back),
    each read a stall. 24-bit depth and triple buffering stay (visuals,
    frame rate); the pushbuffer stays 1 MB (Melee-X hung the GPU when it
    ran off a short one). Round hw17 (gold, 720p): swapping down to 0-32
    chunks out and none read back a scene, but Main Plaza ran 13-15 fps:
    the 6 MB texture pool left the display lists no room to spill into
    once the vertex pool was full, ~120 lists a frame decoded every call
    (37-49 ms). 720p's texture pool is 7 MB now, as 480's (1 MB of the
    1.5 MB the ring gave back); xemu's Main Plaza at 720p then decodes
    as few as at 480.
  - Rain on the GPU (M5.1, approved by the user 2026-10-30;
    `-DXGX_GPU_RAIN=0`, test builds' `env PX_GPU_RAIN=0`): each rain grid's
    streaks sit in a vertex buffer of their own (`GXPortRainGrid`), written
    when the grid's particles change and appended to when they grow. The
    fall is a per-grid sum (`CEnvFxManagerGrid::xPortRainZ`) the vertex
    program adds (z = (z + D) mod 0x4000, exact in floats), and the program
    clips each streak itself as `clip_lines` did (near plane and guard
    band, Liang-Barsky: both ends know the whole streak, so each works out
    its own clipped end; a streak wholly outside goes to one point off the
    screen). The user had approved dropping near-plane streaks; with the
    clip in the program none are dropped. Host check
    (`tools/xbox/test_vp_opt.py`): ~50000 random streaks against the CPU
    clipper, same drops, same ends within 1e-3, and the optimized rain
    programs against their unoptimized ones bit for bit. Vertex pool cost:
    ~6 KB a grid, 64 grids (~400 KB) while it rains, freed 600 frames
    after the last draw. Round hw16 on red: the CPU's render + draw at
    the Landing Site 15 -> 10 ms, but the GPU's wait 1-3 -> 7 ms (the
    clip is 46 instructions a vertex, ~18000 vertices a frame), so the
    same frame rate. Since then a grid's streaks are kept sorted into 4 x 4
    columns (`rain_sort`, rewritten whenever the count changes: the GPU
    has finished the last frame at `frame_open`), and each column's box
    (its streaks' x and y, the whole wrap in z) is tested against the
    planes on the CPU: wholly inside, a program without the clip
    (`VPRAIN_IN`, 8 instructions); wholly outside one plane, not drawn
    (every streak would be dropped); neighbouring columns of one kind are
    one draw. The order doesn't show: the rain is added to the frame
    (`SRCALPHA, ONE`) without depth writes. A whole grid's box alone let
    only 3-40% of xemu's Landing Site grids skip the clip. Host check:
    ~10000 streaks inside, the short program's ends equal the clipping
    one's.

- **Boot music inside the XBE** (round hw2, 2026-10-27): the first boot's
  music is appended to `default.xbe` (IMA ADPCM, 8.3 MB, footer "PXIN"),
  not shipped as `intro.pcm` beside it: "so people can't miss it". After
  the last section, so the kernel never loads it; ADPCM rather than 31 MB
  of PCM, for ~0.1% of the CPU (`tools/xbox/make_intro.py`).
- **L + R + Y at boot forces the full disc check** (round hw2): a debug
  combo, any build, no on-screen hint (`docs/boot-safety.md`).

- **Skip Cutscenes** (Port Options > Gameplay, pause > Visor): MetroidPrimePort's
  randomprime-derived patches, imported unchanged (`xbox/src/port/port_skip_cutscenes_ops.cpp`,
  `port_skip_streams.cpp`, `port_skip_cutscenes_data.inc` at the pinned commit)
  with an Xbox `port_skip_cutscenes.cpp` that keeps only the plain path (no
  randomizer, no Archipelago). A room's script is patched as it loads; one that
  doesn't match its stream is left alone (`[SKIP]` in the log).
- **Streamed music**: the AI DMA path as MetroidPrimePort drives it
  (`prime_sdk.c`), mixed into MusyX's output (`px_sdl3.cpp`).

- **The picture's aspect** (2026-10-28): the NV2A back end's content
  aspect was Melee-X's 73:60, which squeezed Prime's 4:3 picture by 9%
  (black side bars at 480) and its 16:9 into a 4:3-wide strip at 720p. The
  back end now takes the game's EFB width from `VIConfigure` (640, or 854
  when MetroidPrimePort's widescreen widens the framebuffer) and the aspect
  from it (`xgx_set_efb_width`).
- **720p in xemu**: slot 3 (`/c/xemu/px/slot3`) is an HD slot: HDTV AV pack
  and an EEPROM copy with 480p, 720p and widescreen allowed
  (`tools/xbox/eeprom_video.py`, from Melee-X); `env PX_VIDEO=720` in a
  scenario then runs 1280x720.

Not carried over: `disc_lower` and the `DISC_STRUCT` lowering. Melee keeps
disc data big-endian in memory and melee-pc marks those structs; Prime reads
almost everything through `CInputStream`, which MetroidPrimePort makes read
big-endian, so there is nothing to lower. Data the GPU reads straight from
the disc (display lists, vertex arrays) stays big-endian and the GX layer
swaps it, as Melee-X's does.

## Edits to imported code

One line per edit to `src/`, `include/`, `extern/musyx` or
`extern/aurora/include`: file, what, why. All under `TARGET_XBOX` with a
`PORT_XBOX:` comment.

| file | what | why |
|---|---|---|
| `src/MetroidPrime/CStateManager.cpp` | `dynamic_cast` to `CScriptStreamedMusic`/`CScriptMidi` -> vftable compare (`px_rtti.h`) | no RTTI; neither class has subclasses |
| `src/MetroidPrime/CTargetReticles.cpp` | `dynamic_cast<const CAi*>` -> `TCastToConstPtr` of `CPatterned` or `CDestroyableRock` | no RTTI; CAi's only subclasses |
| `src/Kyoto/Text/CStringTable.cpp` | `std::equal` over `data()` pointers | nxdk's libc++ has no `iterator_traits` for rstl iterators |
| `src/Kyoto/Animation/DolphinCSkinnedModel.cpp` | skinning ring back to retail's 512 KB (from 32 MB) | memory; tokens free at once here |
| `src/Kyoto/Input/CDolphinController.cpp` | input always focused | no window; SDL focus is always null |
| `src/Kyoto/Graphics/CCubeMoviePlayer.cpp` | movies always take the deinterlaced (whole-frame) path | the field path draws every other line for an interlaced TV; Xbox frames are whole |
| `extern/aurora/include/dolphin/ppc_math.h` | `_BitScanReverse64` only on x64 | nxdk's clang defines `_MSC_VER` on 32-bit |
| `src/MetroidPrime/CPauseScreenFrame.cpp` | (2026-10-31: Log File in the title screen's System rows) Prime-X's Options rows (`port_xbox_options.h`) in place of MetroidPrimePort's PC rows: tables, titles, get/set, triple labels, Restore Defaults, save on close; the title-screen frame reads its tables through `GetOptionCategory` so "Port Options" can swap them | the user's settings (docs/ui.md); the PC rows drive hooks the Xbox doesn't have |
| `src/MetroidPrime/CFrontEndUI.cpp` | the main menu's "Metroid Fusion Connection Bonuses" row reads "Port Options" and opens them | the user: reachable without a New Game slot; the bonuses need a GBA link an Xbox can't have |
| `src/MetroidPrime/CFrontEndUI.cpp` | the New Game popup's third slot (Hard's, unused before Normal is beaten) shows "Port Options" and opens the Options frame with Prime-X's tables | the user's main-menu entry (docs/ui.md) |
| `src/MetroidPrime/main.cpp` | `UpdateTicks` reports the updates it ran to `xhw_perf_ticks` | `[PERF]`'s "ticks per render" was always 0 (Melee-X's hook, never called): Fixed's catch-up shows now |
| `src/Kyoto/Animation/CHierarchyPoseBuilder.cpp`, `include/Kyoto/Animation/CHierarchyPoseBuilder.hpp` | `BuildTransform` keeps each link's running state by builder, tree version (bumped by `Insert`) and segment, and resumes a walk from the deepest one kept | speed: every locator query walked its chain from the root (0.8% of xemu's pirate room); same operations in the same order, so the same bits |
| `src/Kyoto/Particles/CParticleGlobals.cpp` | `PortLifetimeTween` keeps the reciprocal of the last lifetime | speed: the emulated FRECPE and two fma steps (x87 library calls here) ran per particle; same value |
| `include/Kyoto/Particles/PortParticleMath.hpp` | `SqrtF`/`InvSqrtF` take `max` through `PortFmax` (nxdk `fmaxf`'s answers, bit tests instead of x87 `fxam` calls) | speed: per particle; same results |
| `src/Kyoto/Animation/CHierarchyPoseBuilder.cpp` | `RecursivelyBuild` makes the children's rotation matrix once, not per child (at scale 1 the node's own) | speed; same values |
| `src/Kyoto/Math/CFrustumPlanes.cpp` | `BoxInFrustumPlanes` tests the plane that rejected the last box first | speed: neighbouring boxes fail the same side; every plane must pass either way, same answer |
| `src/Collision/CollisionUtil.cpp` | `TriBoxOverlap` tests the box against the triangle's bounds before the nine edge axes (retail: after) | speed: most candidate triangles fail the bounds test; every test only rejects, so the result is the same |
| `src/Kyoto/Animation/DolphinCSkinRules.cpp` | `BuildBoneRange` positions and plain normals in SSE1, the three rows at once | speed (2.3% in xemu); same operations in the same order, no FMA: the same bits |
| `src/Kyoto/Animation/DolphinCSkinnedModel.cpp`, `include/Kyoto/Animation/CSkinnedModel.hpp` | `Calculate` skips the skinning when its workspace already holds the same virtual-bone transforms over the same source arrays (a hash kept per model; not for owned workspaces or morphs) | speed: an actor drawn twice a frame (body and projected shadow, unsorted and sorted parts) was skinned twice; the skin is 3-5% of the CPU with one actor on screen |
| `src/Kyoto/Animation/DolphinCSkinnedModel.cpp` | `Calculate` tells the GX layer when it writes a workspace (`GXPortSkinWritten`; the shared ring's and a caller's for that frame only, `GXPortSkinWrittenThisFrame`), the destructor and `CalculateDefault` that it goes (`GXPortSkinForget`) | the dynamic display lists reuse a same-frame copy of vertices only from buffers known unwritten since (two calls in three in Main Plaza) |
| `src/Kyoto/Animation/DolphinCSkinnedModel.cpp`, `include/Kyoto/Animation/CSkinnedModel.hpp` | an owned model's `AllocateStorage` asks once for a workspace of its own (`GXPortSkinKeep`; `XGX_SKIN_KEEP_KB`, 1 MB in all, only while 6 MB stays free) and keeps it across draws (`PostDrawFunc` leaves it; the destructor, `CalculateDefault` and, after memory ran short, `Calculate` give it back: `GXPortSkinKeepEpoch`); the skin-skip hash covers kept workspaces | speed: every character skinned into the ring's first bytes (tokens free at once here), so a model drawn again in a frame (shadow, thermal, sorted or multipass parts) was skinned and fetched again because another model used the ring in between: the pirate fights |
| `src/Kyoto/Animation/DolphinCSkinRules.cpp` | `BuildBoneRange`'s SSE loops prefetch the source and output ~8 vertices ahead | speed: no hardware prefetcher on the Pentium III, so each 32-byte line of the streams was a stall (round hw9: 3-4% of the CPU in that loop); hints only |
| `src/MetroidPrime/CMainFlow.cpp` | `env PX_ALL_ITEMS=1` with `MP_BOOT_WORLD` gives every item (test builds) | rendering the visors, beams and suits in xemu |
| `src/MetroidPrime/CMainFlow.cpp` | `env PX_CARD_FILE=<n>` with `MP_BOOT_WORLD` makes the game file n of the memory card (test builds) | a save station only offers to save a game the file select made; the save round trip in xemu |
| `src/GuiSys/CGuiObject.cpp`, `include/GuiSys/CGuiObject.hpp` | moving one widget recomputes its own subtree, not every later sibling's too (`RecalculateSubtree`) | speed: n^2 matrix products a frame for the HUD (3.5% of the Landing Site in xemu); same results |
| `src/Kyoto/Alloc/CMemory.cpp` | `PortXboxHeapAlloc`/`Free`/`FreeBytes` for operator new (no throw, null when the heap is down or full); `HeapMutex` built in static storage | plan 0.6: game `new` in `CGameAllocator` as on the GameCube; the mutex's own `new` would recurse |
| `src/Kyoto/Alloc/CGameAllocator.cpp` | no OOM callback or dump while operator new asks (`gPortXboxQuietOom`); a freed block decommits the whole 64 KB chunks inside it (`xhw_lazy_decommit`) | operator new falls back to malloc; freed load buffers stop costing Xbox RAM (round hw5's out of memory) |
| `src/MetroidPrime/main.cpp` | the GX FIFO 32 bytes (128 KB claimed for `InitGraphicsFifo`'s arithmetic), not 384 KB | our GX never reads or writes it; image memory (.bss is committed at load) |
| `include/Kyoto/Graphics/CGX.hpp` | `SGXState::x4c_flags`' bitfields in reverse order | x86 fills bitfields from the low bit: `CGX::SetNumChans` set bit 7 while `FlushState` tests bit 0, so the channel count never reached GX (the white arm cannon in Chozo and Magmoor: its shadow pass lost COLOR1; the scene's look changing after Morph Ball, whose shadow sets the count directly) |
| `src/MetroidPrime/CPauseScreenFrame.cpp` | a Game Timing row in the pause Display rows and the title's Port Options Gameplay rows (Fixed / Flex, then Auto / Fixed / Flex; removed 2026-10-30, Auto always) | `PortDebug::SimAdaptive` (MetroidPrimePort's adaptive ticks): at ~40 fps the fixed 60 Hz ticks ran ~1.5 game updates a drawn frame on a CPU-bound console; Auto the default since 2026-10-30 ("Game Timing: Auto" above) |
| `extern/musyx/src/musyx/runtime/hw_pc.c` | `salAudioThreadFunc` brackets each mixed frame with `px_mixer_frame_begin`/`end` (`px_sdl3.cpp`) | [PERF]'s "audio" share was a hook nothing called; the mixer's own time, waits on the hw lock taken out (docs/fps-plan.md M0.1) |
| `extern/musyx/src/musyx/runtime/hw_pc.c` | the voice mix (`renderVoiceSegment`'s nine ramps) and the polyphase resampler's tap sum through `px_mixer.h` (32-bit arithmetic where the operands' bounds allow it, the envelope once a subframe; `-DPX_MIXER_FAST=0` for the old loops); no per-voice RMS (it fed `MP_LOG_VOICES`' trace only) | speed: two 64-bit products and clamps a sample a buffer on the CPU the GameCube's DSP did this on (M2); bit-exact, `tools/xbox/test_mixer.py` |
| `src/MetroidPrime/main.cpp` | the game's draw (`CIOWinManager::Draw`) in [PERF]'s render bucket (`xsdk_perf_draw_begin`, vi.c) | "sim" held the ticks and the world's traversal together; now "sim" / ticks is a tick's cost, and [PROFS] a tick-only profile (M0.3) |
| `src/Kyoto/Particles/CElementGen.cpp`, `include/Kyoto/Particles/CElementGen.hpp`, `src/platform/port_element_gen.cpp` | a system that stepped two frames in a tick (a 30 Hz tick) is drawn between its states too: `xPortStepFrames`, and `PortBeginPresent` draws back along the last frame's motion (`x80_timeDeltaScale` = 1 - frames x (1 - t), down to -1) | Game Timing: Auto's 30 Hz ticks: only one-frame steps were blended, so every particle stepped at 30 Hz while the rest moved at 60; a particle's two frames are close to a straight line |
| `src/MetroidPrime/CMapArea.cpp` | `CMapAreaSurface::Draw` hands each strip's and outline's indices over in one `GXPortPositions1x8` call (`port_xbox_gx.h`), the per-vertex calls as the fallback | speed: the HUD minimap's per-vertex `GXPosition1x8` calls were 5.8% of a fight's draw time (round hw12's [PROFR]); the same vertices |
| `extern/musyx/src/musyx/runtime/CheapReverb/creverb.c` | `PortHandleReverbChannel`: the reverb's loop with its coefficients and delay-line state in locals | speed: 18% of the mixer thread in hw11's fight, every value reloaded through pointers a sample; bit-exact (`test_mixer.py`) |
| `extern/musyx/src/musyx/runtime/hw_pc.c` | DSP-ADPCM decoded in 32-bit arithmetic when the voice's coefficients bound the sum (`pxAdpcmSample32`, `px_mixer.h`) | speed: three 64-bit products a sample of every voice; bit-exact (`test_mixer.py`) |
| `extern/musyx/src/musyx/runtime/hw_pc.c` | `resampleVoice` (mode 0, taps that fit 32 bits) runs `pxMixResample4` (`px_mixer.h`): the same loop with the position, read index and four-sample history in locals | speed: the original kept the history in a ring indexed modulo 4 and stored `curPos`/`srcConsumed` through the state every sample; the voice mix was ~2/3 of the mixer thread in a fight (round hw15). Bit-exact (`test_mixer.py`: 100000 random voices) |
| `src/Kyoto/Animation/DolphinCSkinnedModel.cpp` | `Calculate` hands the virtual bones' matrices to the GX layer (`PortSkinOnGpu` -> `GXPortSkinGpu`) and skips the CPU skin when it takes them (not for morphs, point generators, a caller's workspace, NBT normals) | GPU skinning (docs/fps-plan.md M4): the skin and the dynamic lists' per-call fetch were the fights' biggest costs; `-DXGX_GPU_SKIN=0` or test builds' `env PX_GPU_SKIN=0` skin as before |
| `src/MetroidPrime/main.cpp` | `UpdateTicks` takes its fixed step from `PortDebug::XboxTickPeriod` (1/60, or 1/30 under Auto); the blend between ticks follows `XboxPresentBlend` instead of the frame limiter; `TickRenderTimings` gets 1/60 s steps (`XboxRenderTimingSteps`), the render clock's present lag a tick's period instead of 1/60 | Game Timing: Auto (M1); at 30 Hz ticks the textures would scroll at half speed; under Flex they ran slow below 60 fps too |
| `src/MetaRender/CCubeRenderer.cpp` | `PostRenderFogs` draws no fog volumes | the pass needs the depth itself from its Z16 copies (density ramp offsets); ours are in-front masks, so a volume drew as nothing or a solid fog-coloured box (Twin Fires Tunnel) |
| `src/Kyoto/Graphics/CCubeMaterial.cpp` | `SetCurrent` doesn't read a PBR/WRAP record (the maps repeat) | the disc's materials have none (they come with Remastered conversions); the read parsed the material table on every material set, ~1% of the console's CPU |
| `src/MetroidPrime/CPauseScreenFrame.cpp` | the title screen's Options/Port Options put the value tables at the selected right row's own offset, not selection times the left table's pitch, measured from the second row (the first worker's idle position is off the rows' spacing) | round hw6: "the divider sits on the text" on the lower Video rows (Widescreen HUD, Field of View, FPS counter); 2026-10-29, the user: the middle value table two rows low on every row past the first |
| `src/MetroidPrime/CPauseScreenFrame.cpp` | Port Options' value cells on a fixed grid: every cell worker 0's box, centred both ways, evenly spaced around a fixed centre between the columns, the divider models hidden; the shared frame's retail rows get their cells back | round hw8, the user: still misaligned on the console, "remove the bar and make it centred and well spaced" |
| `include/GuiSys/CGuiTextSupport.hpp` | `PortGetProps()`: the text properties read back | the Port Options grid restores the retail cells' justification |
| `src/MetroidPrime/Cameras/CCameraFilter.cpp` | the random-static buffer (338 KB) and the pause blur's persistent copy (280 KB) are game-heap blocks taken at first use, not `.bss` | image memory for the whole session for effects most rooms never show; round hw6's out of memory |
| `src/MetroidPrime/CEnvFxManager.cpp` | `RenderRainParticles` writes its streaks with one `GXPortRainLines` call (`port_xbox_gx.h`), the four calls a particle as the fallback | speed: ~10% of a rainy frame on the console went to the per-vertex calls (round hw5); the same vertices |
| `src/MetroidPrime/HUD/CHudThreatInterface.cpp` | the threat distance digits formatted with `xsdk_snprintf` | nxdk's pdclib printf has no `%f`: the Threat Assessment readout came out empty |
| `src/MetroidPrime/CEnvFxManager.cpp` | `CEnvFxManagerGrid::Render` skips a rain/snow grid whose box is wholly outside the view (`GXPortBoxCulled`, `port_xbox_gx.h`) | speed: every particle is a vertex call each frame, about half of them off screen; the GPU would clip them all anyway |
| `src/MetroidPrime/CEnvFxManager.cpp` | `RenderSnowParticles` and `RenderUnderwaterParticles` write their quads with one `GXPortSnowQuads` call (`port_xbox_gx.h`), the eight calls a flake as the fallback; drawn between ticks (Auto at 30 Hz) the snow's flakes are stepped back into a scratch copy first (the fallback's `PortStepBack`, same force index) for the same one call | speed: Phendrana's snow was ~10% of the console's CPU in those calls (round hw9: `GXTexCoord2u8` 7%, `GXPosition3s16` 3%); the same vertices |
| `src/Kyoto/Graphics/CCubeModel.cpp` | `DrawSurfaces`, `DrawNormalSurfaces`, `DrawAlphaSurfaces` prefetch the next surface's header, its material's first lines and the header after it | speed: `DrawSurface` waited on each header's line (~1.4% of Main Plaza's CPU, round hw9), it and `SetCurrent` on the material (~13% of Main Plaza's draw, round hw15); hints only |
| `src/MetroidPrime/CEnvFxManager.cpp`, `include/MetroidPrime/CEnvFxManager.hpp` | Rain on the GPU: `UpdateRainParticles` adds each tick's fall to the grid (`xPortRainZ`) instead of to every particle; new particles are stored before it, a grid's particles get a new `xPortRainGen` when they change other than by growing, any other effect folds the fall back into them (`PortRainFold`); `RenderRainParticles` draws through `GXPortRainGrid`, with `GXPortRainLines` (fall added in a scratch copy) and the per-vertex calls as fallbacks; `PortRenderEnvFx` adds the fall | speed: the rain's per-frame vertex writes, clip and copy (~10% of a rainy frame, round hw11) and its per-tick particle loop; the same streaks (fps plan M5.1) |
| `src/MetroidPrime/CEnvFxManager.cpp` | `UpdateSnowParticles`, `UpdateRainParticles`: the same sums through local pointers, prefetching ~20 particles ahead | speed: the grids' arrays are rewritten whole every tick, a miss per line and the vector's pointer reloaded per particle (~3% of a rainy or snowy frame, round hw9) |
| `src/GuiSys/CGuiFrame.cpp` | `PortHasHudInterference`'s answer kept per frame once its models are loaded (forgotten when the frame goes) | speed: asked three times a frame, it read every HUD model's materials (~0.4%, round hw9) |
| `src/Kyoto/Particles/CElementGen.cpp` | `UpdateExistingParticles` prefetches the particle two ahead | speed: a fight's effects walk thousands of particles a tick, a miss per line; a hint only |
| `src/MetaRender/CCubeRenderer.cpp` | `DrawUnsortedGeometry` prefetches the box and flags of the model four ahead | speed: the loop over every world model waits on each model's lines (~3% of Main Plaza's CPU, round hw9); a hint only |
| `include/MetroidPrime/CObjectList.hpp`, `src/MetroidPrime/CStateManager.cpp` | `CObjectList::PortPrefetchAhead`: the walks in `PreThinkObjects`, `Think`, `UpdateSortedLists`, `PreRender` and `DrawWorld` prefetch the entity three links ahead | speed: each walk waits on every entity's lines in turn (~4% of Main Plaza's CPU, round hw9); a hint only |
| `src/Kyoto/Particles/CElementGen.cpp` | `RenderParticles`' textured quads (unrotated and rotated, and both again in the motion-blur loop) go through one `GXPortVertices3fC2f` call each (`port_xbox_gx.h`), with the calls' own sums; the twelve calls as the fallback | speed: twelve GX calls a particle through the immediate-mode slots; a check build compared ~131000 quads of xemu's fight with the calls' bytes, none different |
| `src/MetroidPrime/CParticleDatabase.cpp`, `src/MetroidPrime/CParticleGenInfo.cpp`, `include/MetroidPrime/CParticleGenInfo.hpp` | `UpdateParticleGenDB` takes a generator's locator segment from `CParticleGenInfo::PortLocatorSeg`, which looks the (never changing) locator name up once per layout instead of every tick | speed: a string-map lookup per attached particle generator per tick (`GetSegIdFromString` and the map's tree walk, ~0.5% of Lava Lake, hw15); the same segment |
| `src/Kyoto/Particles/CElementGen.cpp` | The untextured quads (`RenderBasicParticles*`, and `RenderParticles`' plain, rotated, oriented and motion-blur ones: nine places) collect their four positions with the calls' own statements (`PortQ`) and go through one `GXPortVertices3fC1s16` call (`PortBasicQuad`), which makes the twelve calls itself when the batch isn't that layout; the original calls under `#else` | speed: Lava Lake's particles were ~4% of the console's CPU in their per-vertex calls (`GXTexCoord1s16` 1.8%, `GXColor1u32` 0.9%, `GXPosition3f32` 0.7%, round hw15); same bytes (`-DXGX_DYN_VERIFY=1` compares them) |
| `src/Kyoto/Alloc/CGameAllocator.cpp`, `src/Kyoto/Particles/CElementGen.cpp`, `src/Kyoto/Particles/CParticleSwoosh.cpp` | the clock reads that only feed write-only timing stats are left out: `gAllocatorTime` (two a game allocation), `CElementGen`'s `x2cc`/`x2d0` and `CParticleSwoosh`'s `x1c4_`/`x1c8_` (two per system update and render) | speed: `xhw_time_ns` was ~1% of the fight's CPU (round hw13), most of it from these; nothing reads the stats |
| `src/MetaRender/CCubeRenderer.cpp` | `AddStaticGeometry` registers the room's geometry memory (the hull of its arrays and display lists) with `GXPortStaticAdd`; `RemoveStaticGeometry` unregisters it (`GXPortStaticRemove`, which drops the lists cached from it) | speed: the content checks of Main Plaza's ~1600 cached world lists were ~5% of the console's CPU (round hw15); the disc's geometry never changes in place, and the game removes it before freeing or moving it (heap compaction does remove/add) |
| `src/Kyoto/Alloc/CMemory.cpp` | `CMemory::Alloc`/`Free` take only `HeapMutex`, not `OSDisableInterrupts` as well | a deadlock on red (free play, a Map Station's jingle ending): the mixer held the MusyX lock and freed a one-shot stream's buffer (`StopStream`), waiting for the interrupt lock, while the game thread held the interrupt lock in `CDSPStreamManager::IsStreamAvailable` and waited for the MusyX lock. `OSDisableInterrupts` is a no-op on PC, a real lock on the Xbox (os.c); `HeapMutex` alone serialises the heap |
| `include/MetroidPrime/CSortedLists.hpp`, `src/MetroidPrime/CSortedLists.cpp` | `CSortedListManager` keeps each sorted list's coordinates in list order (`xPortKey`, 24 KB), moved with the ids; the binary searches and `MoveInList` read them | speed: every near-list query runs twelve binary searches over 44-byte nodes scattered through 45 KB (`BuildNearList` ~3% of a fight's ticks, round hw12); the same comparisons on the same values |

Imported unchanged but added to the game build (MetroidPrimePort compiles
them as game code): `src/platform/port_raster_font.cpp`,
`port_morph_ball.cpp`, `port_automapper.cpp`, `port_particle_vfx_read.cpp`,
`port_element_gen.cpp`, and `src/MetroidPrime/CPortNestedLights.cpp`.

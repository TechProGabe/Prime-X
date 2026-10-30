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
  `docs/fps-plan.md`): the default. 60 game updates a second while the
  frame's CPU time fits in 16 ms; when 3 of 4 frames don't, 30 a second,
  drawn at 60 between the last two (MetroidPrimePort's frame interpolation:
  actors, skinned poses, particles, swooshes, beams, the camera); back to 60
  when a 60 Hz frame (a tick's measured cost plus the frame's draw work)
  has been under 14 ms for 30 frames, after at least a second at 30
  (`port_debug.cpp`, `px_sim_frame` from vi.c's frame boundary). At 60 Hz it
  is Fixed exactly. Fixed (retail: 60 a second, a slow frame runs more) and
  Flex (one update a frame, dt = the frame) stay as choices. Under Auto at
  30 Hz the world is drawn up to a tick (33 ms) behind the simulation and the
  pad is read at 30 Hz; `pad.c` latches presses seen at a frame boundary
  no read followed, so a tap isn't lost between two reads. `SimRate`/
  `SimPeriod` stay 60: the projectile weapons step in 1/60 s substeps at
  any tick. `CStateManager::xf80_hudMessageFrameCount` (the plan's HUD memo
  worry) counts unpauses, not ticks: nothing to scale. settings.ini's
  `timing = auto|fixed|flex` (the old `game_timing = 1` reads as Flex).
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
  - The rain's lines (M5): `GXPortRainLines` keeps its batch's box, and
    `clip_lines` tests the box's corners once; a batch wholly inside skips
    the per-line test (`-DXGX_LINE_BOX=0`). Same vertices drawn.

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
| `src/MetroidPrime/CPauseScreenFrame.cpp` | Prime-X's Options rows (`port_xbox_options.h`) in place of MetroidPrimePort's PC rows: tables, titles, get/set, triple labels, Restore Defaults, save on close; the title-screen frame reads its tables through `GetOptionCategory` so "Port Options" can swap them | the user's settings (docs/ui.md); the PC rows drive hooks the Xbox doesn't have |
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
| `src/MetroidPrime/CPauseScreenFrame.cpp` | a Game Timing row (Auto / Fixed / Flex; Fixed / Flex until 2026-10-30) in the pause Display rows and the title's Port Options Gameplay rows | `PortDebug::SimAdaptive` (MetroidPrimePort's adaptive ticks): at ~40 fps the fixed 60 Hz ticks ran ~1.5 game updates a drawn frame on a CPU-bound console; Auto the default since 2026-10-30 ("Game Timing: Auto" above) |
| `extern/musyx/src/musyx/runtime/hw_pc.c` | `salAudioThreadFunc` brackets each mixed frame with `px_mixer_frame_begin`/`end` (`px_sdl3.cpp`) | [PERF]'s "audio" share was a hook nothing called; the mixer's own time, waits on the hw lock taken out (docs/fps-plan.md M0.1) |
| `extern/musyx/src/musyx/runtime/hw_pc.c` | the voice mix (`renderVoiceSegment`'s nine ramps) and the polyphase resampler's tap sum through `px_mixer.h` (32-bit arithmetic where the operands' bounds allow it, the envelope once a subframe; `-DPX_MIXER_FAST=0` for the old loops); no per-voice RMS (it fed `MP_LOG_VOICES`' trace only) | speed: two 64-bit products and clamps a sample a buffer on the CPU the GameCube's DSP did this on (M2); bit-exact, `tools/xbox/test_mixer.py` |
| `src/MetroidPrime/main.cpp` | the game's draw (`CIOWinManager::Draw`) in [PERF]'s render bucket (`xsdk_perf_draw_begin`, vi.c) | "sim" held the ticks and the world's traversal together; now "sim" / ticks is a tick's cost, and [PROFS] a tick-only profile (M0.3) |
| `src/Kyoto/Particles/CElementGen.cpp`, `include/Kyoto/Particles/CElementGen.hpp`, `src/platform/port_element_gen.cpp` | a system that stepped two frames in a tick (a 30 Hz tick) is drawn between its states too: `xPortStepFrames`, and `PortBeginPresent` draws back along the last frame's motion (`x80_timeDeltaScale` = 1 - frames x (1 - t), down to -1) | Game Timing: Auto's 30 Hz ticks: only one-frame steps were blended, so every particle stepped at 30 Hz while the rest moved at 60; a particle's two frames are close to a straight line |
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
| `src/MetroidPrime/CEnvFxManager.cpp` | `RenderSnowParticles` and `RenderUnderwaterParticles` write their quads with one `GXPortSnowQuads` call (`port_xbox_gx.h`), the eight calls a flake as the fallback (and while the snow is drawn between ticks) | speed: Phendrana's snow was ~10% of the console's CPU in those calls (round hw9: `GXTexCoord2u8` 7%, `GXPosition3s16` 3%); the same vertices |
| `src/Kyoto/Graphics/CCubeModel.cpp` | `DrawSurfaces`, `DrawNormalSurfaces`, `DrawAlphaSurfaces` prefetch the next surface's header | speed: `DrawSurface` waited on each header's line (~1.4% of Main Plaza's CPU, round hw9); a hint only |
| `src/MetroidPrime/CEnvFxManager.cpp` | `UpdateSnowParticles`, `UpdateRainParticles`: the same sums through local pointers, prefetching ~20 particles ahead | speed: the grids' arrays are rewritten whole every tick, a miss per line and the vector's pointer reloaded per particle (~3% of a rainy or snowy frame, round hw9) |
| `src/GuiSys/CGuiFrame.cpp` | `PortHasHudInterference`'s answer kept per frame once its models are loaded (forgotten when the frame goes) | speed: asked three times a frame, it read every HUD model's materials (~0.4%, round hw9) |
| `src/Kyoto/Particles/CElementGen.cpp` | `UpdateExistingParticles` prefetches the particle two ahead | speed: a fight's effects walk thousands of particles a tick, a miss per line; a hint only |
| `src/MetaRender/CCubeRenderer.cpp` | `DrawUnsortedGeometry` prefetches the box and flags of the model four ahead | speed: the loop over every world model waits on each model's lines (~3% of Main Plaza's CPU, round hw9); a hint only |
| `include/MetroidPrime/CObjectList.hpp`, `src/MetroidPrime/CStateManager.cpp` | `CObjectList::PortPrefetchAhead`: the walks in `PreThinkObjects`, `Think`, `UpdateSortedLists`, `PreRender` and `DrawWorld` prefetch the entity three links ahead | speed: each walk waits on every entity's lines in turn (~4% of Main Plaza's CPU, round hw9); a hint only |

Imported unchanged but added to the game build (MetroidPrimePort compiles
them as game code): `src/platform/port_raster_font.cpp`,
`port_morph_ball.cpp`, `port_automapper.cpp`, `port_particle_vfx_read.cpp`,
`port_element_gen.cpp`, and `src/MetroidPrime/CPortNestedLights.cpp`.

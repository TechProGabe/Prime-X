# Performance

## Targets (the user's, 2026-10-27)

| output | target |
|---|---|
| 480i / 480p, 4:3 and 16:9 | **60 fps** |
| 720p (16:9) | **30 fps or more**, as high as it goes |
| everywhere | as high as possible; the game itself always runs at full speed (60 ticks a second) whatever the frame rate |

Measured on the consoles only (xemu's timing is not the console's). A room
"holds" a target when its 5th-percentile frame rate over a scripted run is at
or above it. Hardware: a stock 733 MHz console is the bar; upgraded CPUs
just get more frames (`docs/boot-safety.md`).

## Where it starts

Melee-X on the same hardware: menus at 60, 4-player matches 30-60 at 720p
(Fountain of Dreams 4-CPU 720p went 32.0 to 39.9 fps with the frame-rate
work, `Melee-X docs/fps-plan.md`). Prime is a heavier game: bigger rooms, CPU
skinning, many particles, collision against room geometry, EFB-copy effects,
music streamed and decoded in software, and a 3D HUD. Expect the first boot
of a room to be well under target; the work in phase 6 closes the gap.

## Benchmark rooms

Scripted runs (`tools/xbox/scenarios/`, Melee-X's autopad scripts: load a
save, walk a fixed path, switch visors, fire, `[PERF]` per period) through:

| # | room | why |
|---|---|---|
| 1 | Frigate Orpheon, Main Deck and Reactor Core (Parasite Queen) | first area; fire, sparks, many lights |
| 2 | Tallon Overworld, Landing Site | big open room, rain, water, the ship |
| 3 | Chozo Ruins, Main Plaza and Sunchamber (Flaahgra) | big room, many actors, a boss |
| 4 | Magmoor Caverns, Lava Lake and Twin Fires Tunnel | heat haze (indirect), lava, thermal visor |
| 5 | Phendrana Drifts, Phendrana Shorelines | huge room, snow particles, ice |
| 6 | Phendrana research labs, thermal visor in the dark | EFB copy + palette lookup |
| 7 | Phazon Mines, Elite Quarters (Omega Pirate) | X-ray and thermal, many enemies |
| 8 | Impact Crater, Metroid Prime Lair | the heaviest effects |
| 9 | Morph Ball through Tallon tunnels | ball shadow, camera |
| 10 | pause screen, map and logbook | blur, 3D map, menus must be 60 everywhere |

Each run at 480p 4:3, 480p 16:9 and 720p. Results go in a table in
`docs/roadmap.md` per build, as Melee-X's console history does.

## Measuring kit (from Melee-X, phase 6 first step)

- `[PERF]` lines: fps median, min and 5th percentile per period; CPU ms
  split into game update, render (game side), GX front end, NV2A back end,
  audio; GPU wait.
- `[NV2A]` frame census: draws, vertices, texture uploads, EFB copies,
  program loads per frame (`tools/xbox/census_report.py`).
- The sampling profiler (`-DXHW_PROF=1`, `tools/xbox/prof_report.py`) and
  instruction counts in xemu (`-icount`).
- Console rounds (`tools/xbox/console_round.py`): many A/B builds in one
  launch on the real console.
- `simh` lockstep hashes (`tools/xbox/simh_diff.py`): an optimisation must
  not change the simulation.
- Releases: ThinLTO + PGO (`docs/pgo.md`: `XBOX_LTO=1`,
  `XBOX_PGO=xbox/prime.profdata`) and later the profile-ordered link
  (`xbox/order.txt`), which gave Melee-X +13%.
- `[DLC] immediate batches begun from:` the return addresses of the game's
  `GXBegin` calls with their counts per period (symbolize with
  `tools/xbox/sym.py`): who makes the immediate-mode draws.

## Measured so far (xemu, Landing Site, 2026-10-27)

xemu's CPU is faster than the console's and its GPU slower, so only the
ratios mean anything; console numbers come from console rounds. Same frames
on both sides (`env PX_LOCKSTEP=1`, `/c/xemu/px/scen-perf`), averages of
ten `[PERF]` periods:

| build | draws/frame | game ms | display lists ms |
|---|---|---|---|
| hw1's code (profile run) | 2900-4300 | 46-54 | 19-27 |
| + strip/fan joining, GUI subtree, builtins | ~410 | 21.6 | 5.4 |
| + ThinLTO, PGO, link order | ~375 | 17.8 | 2.7 |

2026-10-28, LTO + PGO, same scene: the line clipper (`gx_vtx.c`
`clip_lines`, needed for the rain's lines that cross the camera) first
cost ~12% of the CPU in xemu, in `gx_vtx_flush`. xemu runs the guest's
float maths through softfloat, so float-heavy code looks several times
dearer there than on a Pentium III. It now tests outcodes first and
divides only for the few lines that cross a plane (~2% of them), and the
rain and snow grids wholly outside the view are skipped before their
vertices are built (`CEnvFxManagerGrid::Render`, `GXPortBoxCulled`): xemu
~27 -> ~30 fps. Most grids touch the view (the camera is inside the rain
volume), so about half of the rain's lines are still built and then
dropped as off-screen.

The profile of the last (LTO + PGO) build: `GXCallDisplayList` 16.6%
(skinned models' lists decoded on every call; the dynamic-list budget is
full) and 5% more in its copies, the rain's immediate-mode calls ~9% (it
draws now: `GXTexCoord1s16` was its colour), `BuildBoneRange` 2.3%,
`pb_busy` 15% (xemu's GPU). Next: the decoder's fast path per format
(`gx_vtx.c` `store_fast`, done), then GPU or SSE skinning.

Console (round hw4, Landing Site in rain): hw1 ~10 fps; plain build with
the batching ~24-31; ThinLTO + PGO ~27-41.

Console (round hw5, profiler builds, ThinLTO + PGO, 480i): front end 32-45,
Landing Site 37-46, Main Plaza 21-46, Frigate 26-43, Phendrana 36-43. The
console's profile of the Landing Site: `GXCallDisplayList` 13%, `memcpy`
10%, the rain's immediate-mode vertices ~11%, `CEnvFxManager::Update`
3.5% (the rain's simulation, on the CPU), `BuildBoneRange` 3.3%. Main
Plaza: display lists 13%, `xgx_draw` 8%. Next: the skinned models' display
lists (decoded on every call), the memcpy callers, and the rain.

2026-10-28 (night), from round hw5's Main Plaza profile read against the
profiler build's code (`stage-hw5/prof`: the XBE's bytes disassembled at
the hot 64-byte buckets): `GXCallDisplayList`'s time is cache misses on
the cache entries (`DlEntry`, 184 bytes with the chain's `next` at the
end: four 32-byte lines a call) and on each list's batch table. Done, not
yet measured on a console: the entry's hot fields in its first 64 bytes,
32-byte aligned; `xhw_time_ns` without an I/O-port read per call (~3%);
`xhw_perf_enter/leave` inline (~1.2%); no PBR-record parse per material
set (`CCubeMaterial::SetCurrent`, ~1%); 48-byte `VpKey` compares inline;
normals as CMP (4 bytes: 13% more of Main Plaza's lists fit the vertex
pool); only lists unused for 3 frames evicted (hw6's 34 ms of display
lists with the pool full were LRU turning the whole cache over). Left:
the batch table (a separate allocation per list), the dynamic lists'
template copies, `CStateManager::Update`'s inlined callees.

2026-10-29, from the same hw5 logs (`[PROFL]`: memcpy's callers):
`GXCallDisplayList` made half of memcpy's 10% in the Landing Site, where
the dynamic (skinned) lists hold ~800 KB of templates and fetch positions
and normals on every call. Each batch is now staged through a 16 KB
scratch buffer (template read once, fetched there, copied to the ring)
instead of fetched into the template and the whole template copied after:
about half the bus traffic. `-DXGX_DYN_VERIFY=1` makes every dynamic draw
the old way too and compares. A list of one draw (most of them, after
merging) keeps that draw in its entry's hot 64 bytes with the layout in
a shared table: no batch table, so no miss for it on the call. Not yet
measured on a console.

The front end (title, file select) runs over a 640x480 THP movie: its
decode was 60% of a frame (float AAN IDCT with a double-precision FMA on an
SSE1 CPU) and the planes' GX-tile conversion 18%. An integer IDCT
(libjpeg's jidctfst arrangement, `THPDec.cpp`) and whole-tile I8 copies
(`gx_tex.c`) halved it in xemu: 68 -> 33 ms a frame (11.8 -> 26 fps).

Chozo Ruins' Main Plaza (hw1: ~5 fps on the console, 7000 draws): its
cached display lists need ~6 MB and the vertex pool is 4 MB, so ~130 lists
were evicted and rebuilt every frame (display lists 53-58 ms in xemu). The
texture pool had ~5.6 MB idle there: display lists now spill into it,
keeping 2 MB free for textures, and the texture allocator gives spilled
lists back before it drops a texture (`nv2a.c` `xgx_vbuf_alloc`,
`gx_dlc_evict_spilled`). Main Plaza in xemu: display lists ~3 ms, game
~20 ms, 7 -> ~29 fps.

The Frigate's opening: 58 skinned lists went volatile (decoded on every
call) because the same list is drawn from the CPU-skinned workspace and
from the disc's arrays in one frame, and the cache key ignored the arrays'
strides and byte orders. With them in the key (`fmt_key`): 7 volatile,
display lists ~10.5 -> ~8.3 ms in xemu. Still the heaviest scene measured
(2000+ draws, game ~35 ms in xemu); the dynamic-list budget (1 MB, full)
is the next limit, and memory is tight for raising it (Main Plaza: ~2.6 MB
free).

2026-10-29 (evening), from round hw9 (profiler chain r9a-r9e and the
user's ship-build free play through the Frigate to Parasite Queen); none
of it measured on a console yet (no more console rounds, the user's call):

- **The game clock** (`OSGetTime`, `os.c`): whole seconds plus the rest at
  40 ticks a microsecond (`OS_TIMER_CLOCK / 1000000`, 40.5 truncated), so
  it ran 1.2% slow through each second and jumped 12.3 ms at the next. The
  game's tick clock (`CStopwatch`, `UpdateTicks`) took the jump as an extra
  simulation tick once a second: a periodic hitch. Now one 32.32 fixed-point
  multiply (no `__aulldiv`); lockstep frames are exactly 675000 ticks.
- **memcpy's small copies**: Main Plaza had 8% of the CPU on the `rep
  movs` of copies under 2 KB (three microcode starts a call). Under 64
  bytes it is a loop now; zero-length `rep`s are skipped; 256 B-2 KB
  copies prefetch their source first. `copy_prim`/`seg_list` copy vertices
  as words (three memcpy calls a quad before: every particle and flake).
- **Dynamic (skinned) lists' fetch**: positions and normals are native
  floats from the skinning; a loop each (12-byte move, CMP pack) instead
  of `store()`'s per-element switch through the stack (~5% in Main
  Plaza). `-DXGX_DYN_VERIFY=1`: 0 of 125k draws differ (Landing Site,
  Main Plaza, Phendrana).
- **Index-only dynamic lists**: a skinned list that doesn't fit the
  dynamic budget kept no dynamic form and was parsed and decoded from the
  list on every call (free play: up to 16 such lists in the Frigate's
  fights, display lists 15-28 ms, 10-15 fps). It now keeps its indices and
  plans without a template (every array fetched each call, a few bytes a
  vertex; their own 256 KB allowance past the budget, and batches over
  the 16 KB scratch go through a buffer reserved up to 128 KB).
  `-DXGX_DYN_INDEX_ONLY=0` restores the old behaviour. A volatile list's
  log line now says which limit refused it.
- **Failed builds back off**: with the vertex pool full, a list that found
  no room scanned, decoded and tried evictions on every call, then was
  decoded again to draw. It tries again 30 frames on (`DLC_BUILD_RETRY`).
- `derive_vk`'s scan of the eight lights' attenuation (48 float compares a
  draw, xgx_draw's busiest spot) runs only after a light changed.
- A dynamic list's checks (its own bytes once a frame, each array not yet
  seen changing on every call: 64 sampled words each, cache misses, ~50
  a frame in Main Plaza) follow the cached lists' policy: after 120
  passes, every eighth frame with 16 samples. `fmt_key` (two hash mixes a
  call) is cached until the descriptor, a format or an array's stride
  changes; `GXColor1u32` (every particle corner) takes `GXColor4u8`'s
  fast path.
- **Snow** (Phendrana ~10%): `GXPortSnowQuads`, one call for a grid's
  flakes; `GXTexCoord2u8` & co. take a fast path when the next slot is a
  direct texture coordinate.
- **Cache misses in the game's walks**: prefetches four world models
  ahead in `DrawUnsortedGeometry`'s cull loop (~3%), three entities ahead
  in the state manager's object-list walks (~4%), and ~8 vertices ahead
  in `BuildBoneRange`'s streams (the Pentium III has no hardware
  prefetcher).
- More misses taken off: the next surface's header (`CCubeModel`'s draw
  loops), the particle two ahead (`UpdateExistingParticles`), the rain and
  snow grids' arrays (rewritten whole every tick; the loops also reloaded
  the vector's pointer per particle: ~3% of a rainy or snowy frame).
  `PortHasHudInterference` (every HUD model's materials, three times a
  frame) is kept per frame; the vertex constants' and programs' method
  headers are written inline (pbkit's `pb_push` is two unoptimised calls).
- Found, not changed: the HUD minimap (`CMapWorld::Draw`) is 150-300
  immediate draws every frame of play (each surface's fill and outline,
  alternating triangles and lines, a `GXSetTevKColor` between each).
  pbkit is built without optimisation (as on Melee-X); its hot spots are
  waits, and `-O2` would turn `while (pb_BackBufferbReady[...])` into a
  hang.

## Levers, roughly in the order to try them

| lever | where | note |
|---|---|---|
| everything Melee-X already has | GX/NV2A layer | display-list vertex cache, fewer bigger draws, generated and cached vertex programs, native texture formats, word-wide `memcpy`, kernel disc reads |
| **skinning on the GPU** | `CSkinnedModel`/`CSkinRules` | Prime skins every animated model on the CPU into workspace arrays (Flipper had no skinning). Melee-X already skins in NV2A vertex programs (matrix palette, `a0` indexing): feed Prime's bone matrices to the same path and skip the CPU pass. Likely the biggest CPU win |
| SSE (SSE1 only) for what stays on the CPU | skinning fallback, `CElementGen` particle updates, `Kyoto/Math` (`CMatrix4f`, `CTransform4f`, vector code), the audio mixer | `-march=pentium3`; no SSE2 (boot-safety) |
| particle batching | `CElementGen`, `CParticleSwoosh`, `CParticleElectric` | immediate-mode `GXBegin` per particle: merge into one draw per emitter (Melee-X's merge). First step done: strips, fans and line strips of up to 64 vertices become lists and join (`gx_vtx.c` `seg_list`): every text glyph was its own 4-vertex strip, one draw each. The Landing Site had ~705 immediate draws a frame, none joined |
| audio mixing cost | MusyX synth and the DSP-ADPCM music streams | was the GameCube DSP's job; mix at 32 kHz with SSE, cap voices if needed, measure first |
| EFB copies | visors, blur, shadows, transitions | copy only the rectangle used, at the size used; reuse targets; skip the pause blur's copies when nothing moved |
| culling | `CCubeRenderer` area octree, actors | check the game's culling isn't defeated by the widescreen frustum; Melee-X's off-screen DL culling |
| game-tick catch-up | `CMain` frame loop | when a frame takes longer than 1/60 s, run the missed game ticks before the next draw (capped), so the game never slows down; check MetroidPrimePort's `PortTickGeneration` and frame interpolation notes (`docs/FRAME_INTERPOLATION.md` there) |
| 720p fill rate | NV2A | R5G6B5 + Z24S8 as Melee-X; fewer full-screen passes; if 720p still can't hold 30, a render scale (3D at 960x540, HUD at 720p) is the fallback, the user's call |
| frame pacing at 720p | VI | default: as fast as it goes, synced to VBlank; if it judders between 30 and 60, offer "720p: steady 30" as a setting (user's call when measured) |

Every lever gets a kill switch (`-DXGX_...=0`) and a line in `docs/decisions.md`
with its measured effect, as on Melee-X.

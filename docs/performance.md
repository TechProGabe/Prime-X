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

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
- Releases: ThinLTO + PGO and the profile-ordered link (`xbox/order.txt`),
  which gave Melee-X +13%.

## Levers, roughly in the order to try them

| lever | where | note |
|---|---|---|
| everything Melee-X already has | GX/NV2A layer | display-list vertex cache, fewer bigger draws, generated and cached vertex programs, native texture formats, word-wide `memcpy`, kernel disc reads |
| **skinning on the GPU** | `CSkinnedModel`/`CSkinRules` | Prime skins every animated model on the CPU into workspace arrays (Flipper had no skinning). Melee-X already skins in NV2A vertex programs (matrix palette, `a0` indexing): feed Prime's bone matrices to the same path and skip the CPU pass. Likely the biggest CPU win |
| SSE (SSE1 only) for what stays on the CPU | skinning fallback, `CElementGen` particle updates, `Kyoto/Math` (`CMatrix4f`, `CTransform4f`, vector code), the audio mixer | `-march=pentium3`; no SSE2 (boot-safety) |
| particle batching | `CElementGen`, `CParticleSwoosh`, `CParticleElectric` | immediate-mode `GXBegin` per particle: merge into one draw per emitter (Melee-X's merge) |
| audio mixing cost | MusyX synth and the DSP-ADPCM music streams | was the GameCube DSP's job; mix at 32 kHz with SSE, cap voices if needed, measure first |
| EFB copies | visors, blur, shadows, transitions | copy only the rectangle used, at the size used; reuse targets; skip the pause blur's copies when nothing moved |
| culling | `CCubeRenderer` area octree, actors | check the game's culling isn't defeated by the widescreen frustum; Melee-X's off-screen DL culling |
| game-tick catch-up | `CMain` frame loop | when a frame takes longer than 1/60 s, run the missed game ticks before the next draw (capped), so the game never slows down; check MetroidPrimePort's `PortTickGeneration` and frame interpolation notes (`docs/FRAME_INTERPOLATION.md` there) |
| 720p fill rate | NV2A | R5G6B5 + Z24S8 as Melee-X; fewer full-screen passes; if 720p still can't hold 30, a render scale (3D at 960x540, HUD at 720p) is the fallback, the user's call |
| frame pacing at 720p | VI | default: as fast as it goes, synced to VBlank; if it judders between 30 and 60, offer "720p: steady 30" as a setting (user's call when measured) |

Every lever gets a kill switch (`-DXGX_...=0`) and a line in `docs/decisions.md`
with its measured effect, as on Melee-X.

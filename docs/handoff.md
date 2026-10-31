# Handoff: state and working notes

Update this at the end of every session: what was done, what's next,
what's broken, anything the next agent would otherwise rediscover.

## Where it stands (2026-10-30, end of day)

The game plays through every world in xemu (room sweep: 14 rooms, no
failure, plain and LTO + PGO) and on the red console (~35-45 fps in most
rooms at 480i, fights 17-18 at hw10). Everything is committed and pushed on
`dev`. **Round hw10 ran on red** (below, "Console round hw10"): the
landing glow box and the visors are fixed on hardware, Landing Site and
Main Plaza are faster than hw9, fights are still 17-18 fps (Fixed's
catch-up), and the audio got stuck from the third scene of the chain on.
The round is closed (no free play: the user). No round is planned; the
next is the user's call.

Every sweep room matches Dolphin (`tools/xbox/dolphin_ref.py`) except
Magmoor's fog volumes, which the port doesn't draw (Next 3). Verify builds
(`-DXGX_DYN_VERIFY=1`) check every new GX path against the old one draw by
draw in xemu; xemu itself is a bit non-deterministic in float, so a verify
run shows a few one-bit normals: compare against that, not zero.

Ship builds are `XBOX_LTO=1 XBOX_PGO=xbox/prime.profdata` (`docs/pgo.md`);
deploy with `tools/xbox/console.py` (console FTP 192.168.158.113,
`PX_HW=/c/xemu/hw/prime`; back up `E:\UDATA\50580001` first). The profile
was trained on 452e820 (the morning of 2026-10-30); the renderer changed a
little since: retrain before a release. Console rounds and their files:
`C:\xemu\hw\prime\ROUNDS.md`; older sessions and rounds (hw1-hw9, and the
night of 2026-10-30): `docs/history.md`.

## FPS plan under way (2026-10-30, afternoon)

The user: "execute the fps plan". `docs/fps-plan.md` section 0 lists what
landed and the rounds' numbers (hw11, hw12). Built and on red:

- M0: the mixer's share in [PERF] and [PROFA], the game's draw apart from
  the ticks ("render", `ms per tick`, [PROFR]), stats at scene ends.
- M1: **Game Timing: Auto is the default** (30 Hz ticks drawn at 60 with
  MetroidPrimePort's interpolation when the frame doesn't fit; particles
  blended over two-frame steps). Since 2026-10-30 the only mode (the
  user removed the Port Options row; Fixed/Flex as test hooks only).
- M2: the mixer's ramps, resampler, ADPCM and reverb, bit-exact
  (`tools/xbox/test_mixer.py`).
- M4: **GPU skinning** (gx_vtx.c `gs_*`, `GXPortSkinGpu`): characters
  drawn from their bind pose with a matrix per bone, reflections through
  per-bone texture matrices, a 1 MB vertex-pool budget, CPU fallback.
- M3/M5 pieces: draw merging, `GXSetArray` no-op, adaptive pushbuffer
  kick, rain grids tested once, the minimap's positions in one call,
  `XboxPresentBlend` once a frame, the sorted lists' coordinates in list
  order (`BuildNearList`).
- Compiler: **`-O3` by default** (hw13: 1-4% ahead of -O2 in every scene).

Red, Auto: fight 17-18 (hw10, Fixed) -> 30-32 fps (hw12), pause screen
46.5 -> 58-60, Lava Lake 28 -> 40, Main Plaza 35 -> 36-37 (the draw work
there is the limit). No audio fault in 21 chained scenes (hw11, hw12).

## Next session, in order

0. **Round hw16, waiting for the user** (no hardware tests until they say;
   2026-10-30 evening). Staged in `tools/xbox/scenarios/hw16` and
   `C:\xemu\hw\prime\stage-hw16` but NOT uploaded; rebuild before the round,
   since more landed after it was staged, all checked in xemu only:
   - a room's geometry as static memory (no content checks on its lists),
   - the list cache's chains in compact arrays,
   - texture binds remembered per GXTexObj (`XGX_TEX_MEMO`, verify mode),
   - the next surface's material prefetched,
   - the mixer's resampler loop with its state in locals (bit-exact),
   - covering vertex programs (`XGX_VP_COVER`: fight loads 25 -> 12/frame),
   - the rain's line clip test in SSE (`XGX_LINE_SSE`, verified),
   - the audio deadlock fix (`CMemory` without the interrupt lock), on red
     and gold already as the free-play build.
   Measure all against hw15 in the same four scenes.
1. **M6a on JUMPs** (hw15: both pushbuffer tests passed on red; hw14's
   CALL failure fell on the boot's one context switch). hw15: Main Plaza
   38-39 -> 43-44, pirates 33 -> 34-35, Lava Lake 42 -> 44-45 with
   texture-only draws (`XGX_MAPS_FAST`). The back end (`xgx_draw`,
   `GXCallDisplayList`) is still the biggest cost in Main Plaza: record a
   world model's draws into a segment, jump to it with a patched JUMP back.
   Then: the game's own draw (render, 10-14 ms) is the biggest bucket ([PROFR] in
   hw12: the immediate-mode flush and rain in the fight, DrawSurface,
   texture binds and SetCurrent in Main Plaza); the mixer is still 13%
   of a fight; GPU rain (a vertex program with EXPP's fraction; the CPU
   line clip near the camera is the open question) and the visor passes
   (M5.6) are next in the plan.
1. **Audio stuck** (hw10, "Console round hw10"): the old fault, seen in
   the chain from r10c on. Next: what each XBE leaves at exit; a warm
   reset before the cold one. Only a console round can test a change.
2. **Fights**: hw10's r10d ran 17-18 fps at 3.4 ticks per render (sim
   ~12 ms a tick): Fixed's catch-up is most of the frame; Flex would give
   ~35. The user knows (2026-10-30, Fixed vs Flex explained: Flex runs
   one update a frame with dt = the frame's time, at most 1/30 s, so below
   30 fps the game slows instead; hw7 measured Main Plaza 31.9 -> 35.3 and
   Landing Site 34.9 -> 37.9 with Flex; never measured in a fight). A cap
   on Fixed's catch-up would be the other lever (the game would run slow
   in fights instead of dropping frames): the user's call. In: skinned lists over
   the dynamic budget go index-only; same-frame reuse of skinned vertices;
   per-model skin workspaces (`GXPortSkinKeep`, on since 2026-10-30, 1 MB,
   given back when memory runs short); `TriBoxOverlap` rejects on the
   bounds first. xemu pirate room profile (`39F2DE28:B9ABCD56`, 6 reports):
   `BuildBoneRange` 5.2 -> 3.4% of the samples, `dyn_fetch_batch` 6.7 ->
   5.4, `TriBoxOverlap` 2.2 -> 1.1. Most repeat calls left are other
   instances of the same model (other pose): nothing to reuse. Game
   Timing: Fixed runs 3.4 updates per drawn frame in xemu's pirate room
   at ~17 fps (`[PERF]`'s "ticks per render" works since 2026-10-30): in
   fights the catch-up is most of the frame; Flex runs one. The user's
   choice; worth telling them.
   Next lever, GPU skinning, sized 2026-10-30: models have 3-82 virtual
   bones (pirate-class 35-50; a virtual bone is one matrix a vertex, at
   most 3 joints blended on the CPU). The VP already takes a per-vertex
   matrix index (`a0` = PNMTXIDX, 10 position and normal matrices, used on
   the console by Melee-X), so a dynamic list can be drawn from the model's
   own arrays with each vertex's virtual bone as its matrix index and the
   palette (current PNMTX x bone, NRM x bone rotation) loaded per draw:
   no VP change. To handle: lists split into draws of at most 10 bones
   (strips become triangle lists: memory); `CSkinRules` (the bones) is
   shared by every instance, so `Calculate` must copy its pose's matrices;
   texgen from position or normal (reflections) wants skinned model-space
   values: CPU path; a vertex whose position and normal fall in different
   bones: CPU path; skinning itself can only be skipped lazily (sPointGen,
   morphs, anything that reads the workspace). Memory is the catch: the
   static skinned vertices must sit in the vertex pool (GPU-readable; full
   in hw9's fights) while the dynamic template stays for the CPU-path
   fallbacks, about twice a character's vertex memory on a console that
   already runs near 3 MB free there. Not started for that reason.
3. **Fog volumes** (Magmoor's lava haze, acid, water; `PostRenderFogs`
   draws none): the only Dolphin difference left in the sweep rooms. A new
   render path: the volume's thickness needs the scene's depth where the
   volume dips into geometry, so the depth buffer must be read as a
   texture (aliased as A8R8G8B8 for a DEPENDENT_AR lookup into the ramp,
   or a depth-format texture). Tiled, maybe compressed Z memory on the
   console: build in xemu, trust only after a console round. The user's
   call to start (asked 2026-10-30).
4. If memory runs out on red again: another reclaimer is easy to add
   (`xhw_mem_add_reclaim`); left: the display lists' dynamic templates.
5. The HUD minimap is 150-300 draws a frame (fill and outline alternate per
   surface, `GXSetTevKColor` between): only a different draw method would
   join them; only if it keeps the retail look exactly.
6. Not done, by choice: `GXSetDstAlpha` and the EFB pixel format (Morph
   Ball's shadow IDs; thermal's DSTALPHA blends, though thermal matches
   Dolphin as it is). Magmoor's "heat haze without its warp" was the
   fog-volume pass all along (Next 3).
7. Parity with Dolphin (`tools/xbox/dolphin_ref.py`, docs/testing.md):
   the 14 sweep rooms match (2026-10-30) but for the fog volumes; thermal
   matches since its palette pass works (`--visor thermal`); X-ray can't be
   held that way (no world pass), scan needs a target to compare.
8. Gate leftovers: the 128 MB hold needs a 128 MB-aware BIOS in xemu; the
   save round trip on red and Dolphin `.gci` (phase 4); Docker/CI (the
   workflow exists, `gh` isn't logged in here).

## Console round hw10 (red, 2026-10-30)

Chain r10a-h on the profiler build (`tools/xbox/scenarios/hw10/README.md`),
logs and shots in `C:\xemu\hw\prime\logs-hw10` (the save folder pulled
whole: each scene's log is `boot_Prime-X-r10x.log`, its shots
`shot_Prime-X-r10x_NN.bmp`); the first attempt's r10a in `logs-hw10a`;
the save folder before the upload in `logs-hw10pre`. Maps
`prime_x.hw10.map` (profiler), `prime_x.hw10ship.map`. No free play (the
user), so the pause screen, the gunship and the pickup hitches weren't
measured; the console still has `Prime-X` (ship) and `Prime-X-prof`
(profiler, no script) for a later session.

- **The landing glow box is gone on red** (r10b shot 14: a starburst
  fading out, as Dolphin), and the lightning-lit clouds fade (shot 13):
  the fog-colour and spot-light fixes hold on hardware.
- **Visors on red** (r10f): thermal's heat palette and X-ray's lavender as
  in xemu, 48-60 fps; the DEPENDENT_GB unit runs on hardware, no fault.
- fps against hw9: Landing Site (r10c vs r9b) mostly 34-59, lowest 24
  (hw9 dipped to 17-22); Main Plaza (r10e vs r9c) mostly 30-36 (hw9
  24-32), a dip to 18-20; Frigate opening (r10h vs r9d) 50-60, as before;
  Lava Lake (r10g) 27-28.
- **Fights** (r10d, Frigate Crash Site pirates): 17-18 fps in the fight,
  3.2-3.5 ticks per render: sim 38-41 ms a frame (~12 ms a tick) plus
  display lists and draws ~16 ms. Under Game Timing: Fixed the fight is
  the catch-up (71% of the profile's samples in the simulation); one tick
  a frame would be ~28 ms (~35 fps). Flex is the user's lever; capping
  Fixed's catch-up would slow the game down instead (a choice for the
  user). After the fight, 33-36 fps.
- Room-entry stalls of 0.3-1.3 s in every scene (`MP stall`), as before;
  the [PROFT] frames (150 ms or more) were ~65% waiting (the game thread
  not running: disc reads), not computing. Lowest free memory in the
  chain 3.7 MB; no fault, hang or out of memory in eight scenes.
- **Audio stuck** ("Sound hardware is stuck"): the first attempt's r10a
  (the console switched off and on before, previous exit recorded as an
  in-game reset) and, in the second attempt, every scene from r10c on.
  r10a (after a power-off) and r10b played; r10c's boot found the AC97 as
  idle as r10b's had (the same registers in `[AUDIO] found`), yet its
  engine never left descriptor 0, through six restarts and a cold reset.
  It began at the relaunch from r10b, which exited at frame 700 in the
  middle of the landing cinematic (lockstep; streamed audio not started).
  An old fault (Melee-X's console A on 2026-10-03, hw9 r9e): the codec
  stops taking frames and only a power-off brings it back, so it isn't
  the bus masters (reset each boot) and likely not the AC-link cold reset
  either (no effect). Next: compare what each XBE leaves at exit (the
  engine running or not, `xhw_audio_shutdown`), and try a warm reset
  (global control bit 2) before the cold one; codec register reads froze
  the CPU twice (hw7, hw9), so not those.

## Session 2026-10-30 (day): Dolphin references, rendering fixes, hw10 staged

- Docs consolidated: the sessions and rounds of 2026-10-27..29 moved to
  `docs/history.md`; `renderer.md`'s status column filled in; the
  roadmap's known issues split into open and fixed-waiting-for-hw10.
- `tools/xbox/dolphin_ref.py`: retail frames from Dolphin in any room, no
  save or input (Gecko codes: the front end goes to the game, the Frigate
  becomes the room asked for; `--visor` holds a visor). Dolphin movies
  were tried for input first: with one playing this build drew nothing.
- **Fog colour on additive blends**: retail's `CGX::update_fog` sets the
  fog colour black while the blend's destination factor is ONE, through
  `GXSetFogColor`, which was a no-op here (`prime_sdk.c`). Fog's colour
  was added over every additive effect in fog: the landing cinematic's
  glow drew in a light square (xemu lockstep frames 450-452; the user saw
  it black on red in hw7, which this doesn't explain: check on red). Now
  in `gx_state.c`.
- **Spot lights**: GX clamps cos(L, dir) at 0 before the angle polynomial
  (Dolphin's LightingShaderGen); the VP didn't, so a cos^2 term lit the
  back. The lightning-lit clouds in the landing cinematic ended on a hard
  polygon edge; now they fade as in Dolphin. `tests/xbox/vp_ref.c` got
  the same line (its header says so) and `legacy_len`'s spot count went
  13 -> 14; `test_vp_opt.py` 200000 keys, 0 failed.
- **Thermal and X-ray palettes**: both visors copy the frame as intensity
  (I4, I8) and draw the copy back as C4/C8 through a TLUT (thermal's heat
  palette, `TXTR_XRayPalette`); the port sampled the copy as plain
  intensity, so the thermal gun drew white and the door's outline black,
  and X-ray drew grey. A CI view of an EFB copy now gets a TLUT texture
  (`XgxMap.dep`, gx_tex.c `dep_palette_for`) that a DEPENDENT_GB unit looks
  the copy's intensity up in (nv2a.c `UNIT_DEP`). Intensity copies now use
  GX's formula (Y = (66 r + 129 g + 25 b) / 256 + 16, Dolphin's
  TextureConverterShaderGen), since the value is an index now. Thermal
  matches Dolphin; X-ray's palette is inverted (dark -> pale lavender,
  bright -> navy), so X-ray now looks pale lavender in dim rooms. Dolphin
  can't give an X-ray reference with `--visor` (the world pass starts on
  a real visor change in `CPlayerVisor`).
- Room sweep 14/14 after each of the two commits (no FATAL, THROW, out
  of memory); the shots
  differ from the night's by timing only. Every sweep room matches
  Dolphin's frame of it but Magmoor's fog volumes; Ice Temple's and the
  quarry's odd poses (gun raised, left hand up) are Samus's idle
  animations, in retail too.

## Working notes

- Build: `tools/xbox/msys/build.sh` (test build: `XBOX_CFLAGS=-DXHW_AUTOPAD=1`;
  draw trace of a SHOT frame: add `-DXGX_DEBUG_TRACE=1`). Output
  `build-xbox/xbe/default.xbe`, map `build-xbox/prime_x.map`, log of the
  last build `build-xbox/build.log`.
- xemu: `. /c/xemu/px/env.sh` (this PC's `PX_*` paths, outside the repo),
  then `PX_STAGE_EXTRA=tools/xbox/scenarios/<name> tools/xbox/xemu_run.sh
  <secs> '<stop regex>'`; shots: `python tools/xbox/fbdump_to_png.py
  /c/xemu/px/run/serial.log build-xbox/shots/<name>`.
- xemu is slow on this game: the title runs ~12 fps (THP's JPEG decode in
  emulated SSE), a load can take 15 s. Give scenarios 20 minutes.
- The user's image: `C:\xemu\prime\Metroid Prime (USA) (v1.00).iso`
  (extracted from the NAS, `P:\Game Console ISO\ngc\Metroid Prime (USA)
  (v1.00).7z`; GM8E01 rev 0, SHA-1 `ac20c744db18fdf0339f37945e880708fd317231`).
- `[STUB] name` lines: a dropped hook the game reached (once each).
- `ref/Melee-X`'s working tree next door (`Documents/GitHub/Melee-X`) has an
  unfinished merge: never touch it; read `ref/Melee-X` (pinned) instead.
- Heredocs in Git Bash mangle backslashes in some cases: write files that
  contain `\\` with the editor tools, not `cat <<EOF`.
- Host tests on this PC: `PATH=/c/msys64/mingw64/bin:$PATH CC=gcc python
  tools/xbox/test_<name>.py` (no compiler on the Windows PATH, no Docker;
  `test_vp_encoder.py` also wants the `nv2a_vsh` module).
- `PX_LOCKSTEP=1` runs aren't frame-exact from one run to the next (loads
  and streaming vary), so a SHOT frame number lands on different moments.
  To bisect a glitch, shoot a dense run of frames in one run, pick the bad
  ones by their traced draw count, and skip draw ranges (`XGX_SKIP`) over
  that window. The trace's first-vertices lines show bad attribute decodes.

## The user's requirements, in one place

From the planning session (all in `docs/decisions.md`):

- USA 1.00 only; MetroidPrimePort trimmed; retail plus light extras
  (cutscene skip, widescreen HUD, FOV).
- 480 and 720 widescreen; 60 fps at 480, 30+ at 720p, as high as possible.
- Settings inside the game's own Options screens, looking built in (as
  OpenCrossing-Xbox's do).
- Splash: Melee-X's "TechProGabe Presents..." remixed in Prime's look.
- Thorough disc validation at boot; 128 MB consoles locked to 64 MB;
  upgraded/overclocked CPUs supported gracefully.
- Repo `TechProGabe/Prime-X`, private.

Added 2026-10-27 (this session):

- Test in xemu until it is stable, then on hardware.
- Level skip and the useful port debug tools must work in xemu and on the
  consoles (`MP_BOOT_WORLD` & co., `docs/testing.md`).
- Saves must not cause hitches (Melee had them before v63).
- No subagents.

## This PC

- Windows 11, Git Bash and PowerShell. `python` (not `python3`) has Pillow.
- MSYS2 `C:\msys64` (clang 21.1.8), nxdk `C:\xdev\nxdk` at `58427c07`,
  xemu `C:\xemu\xemu\xemu.exe` with `C:\xemu\xemu.toml` (shared with
  Melee-X's runs).
- GitHub: `gh` isn't logged in; the stored git credential is TechProGabe.

## Open questions for the user (ask when they come up, not before)

- Whether to fall back to a 720p render scale if 720p can't hold 30
  (`docs/performance.md`).
- "Steady 30" at 720p vs uncapped, once judder can be measured.
- Front LED effects: worth doing for Prime, or just off.
- Fog volumes (Next 3): start the depth-texture render path, knowing it
  needs a console round before it can be trusted (asked 2026-10-30).

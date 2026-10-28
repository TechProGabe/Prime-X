# Handoff: state and working notes

Update this at the end of every session: what was done, what's next,
what's broken, anything the next agent would otherwise rediscover.

## Where it stands now (2026-10-28, afternoon)

The game plays through every world in xemu (`tools/xbox/room_sweep.sh`:
14 rooms, no failure) and on the red console (round hw5: ~35-45 fps in most
rooms at 480i, the Landing Site 37-46, up from hw1's ~10). Everything is committed and pushed on
`dev`. Ship builds are `XBOX_LTO=1 XBOX_PGO=xbox/prime.profdata`
(`docs/pgo.md`). Done overnight, newest first:

- **Particles' texture coordinates**: CElementGen writes each quad corner's
  TEX0 (two S8s, one fraction bit) with `GXTexCoord1s16(0x0202)`; it was
  taken as one 16-bit coordinate, so every particle's t was 0 and its
  texture's top row stretched across it in stripes. A 16-bit write now
  fills the vertex's next two bytes whatever the call is named (`gx_vtx.c`
  `put16`; it also covers the rain's RGBA4 colour, `color16_next` before).
  This was the landing cinematic's "washed out white with vertical streaks"
  (round hw1): the ship's steam clouds, close to the camera.
- The landing's remaining white frames are single frames (shots 2, 7, 23
  of a 20-frame-step run, `/c/xemu/px/white.log`) whose last draw is an
  untextured full-screen additive quad after the letterbox: a camera-filter
  flash, most likely the storm's lightning. Compare with Dolphin.
- Rain and snow grids wholly off screen are skipped
  (`CEnvFxManagerGrid::Render`, `GXPortBoxCulled`); the line clipper sorts
  by outcode first; `put16` decodes the RGBA4 colours and S8 texcoord pairs
  inline. Landing Site in xemu ~27 -> ~30 fps (LTO + PGO). The PGO profile
  predates these: retrain before a release (`docs/pgo.md`).
- **Lines clipped on the CPU** to the near plane and a 2x guard band
  (`gx_vtx.c` `clip_lines`): the rain lines with an end behind or just in
  front of the camera came out flipped or ~300,000 px long.
- Trace builds (`-DXGX_DEBUG_TRACE=1`) print each traced draw's first four
  vertices (position, tc0, colour) and A8R8G8B8 textures' average and alpha
  range.
- Every particle draws its whole texture now, so rooms look busier than
  before (steam, mist, sparks): compare with Dolphin before calling any of
  it a bug.

- The picture's aspect follows the game's EFB (`VIConfigure` ->
  `xgx_set_efb_width`): no more 9% squeeze and side bars at 480, full 16:9
  at 720p, letterboxed 16:9 on a 4:3 screen. 720p tested in xemu (slot 3,
  `docs/testing.md` "720p in xemu").
- Display-list cache: entries keyed by the arrays' layout too (the Frigate's
  skinned lists stopped going volatile); lists spill into the texture
  pool's free space (Main Plaza 7 -> ~29 fps in xemu).
- Movie decode twice as fast (integer IDCT, whole-tile copies): the front
  end 12 -> 26 fps in xemu.
- Skip Cutscenes (MetroidPrimePort's patches), Fusion Suit, descriptions
  under the pause screen's Prime-X rows, B is back in menus, Port Options on
  the main menu (the Fusion bonuses row), three-choice rows only.
- Streamed music (the AI path) mixed into MusyX's output.
- Test hooks: `env PX_ALL_ITEMS=1`, `PX_SKIPPABLE=1`, `PX_WIDESCREEN=0|1|2`.
- Console round hw5 (a profile of five scenes) is prepared, not run.

## Session 2026-10-28 (afternoon): the hw5 free play's list

The user played the ship build (`F:\Applications\Prime-X`, LTO + PGO,
`7f1dfb3` + the watchdog fix) on red, 2026-10-28. Logs:
`C:\xemu\hw\prime\logs-hw5play` (`boot.log`, `lastexit.txt`; map:
`C:\xemu\hw\prime\stage-hw5\ship\prime_x.map`). What came of each:

1. **Out of memory** leaving the ship (~215 s in, 2.1 MB free): game
   `new` lived in malloc (15.5 MB in the Landing Site) beside
   `CGameAllocator`'s MEM1 heap, each with its own high-water mark. Done
   (plan 0.6, `docs/decisions.md` "Memory model"): `new` goes into
   `CGameAllocator` (malloc only when it is full or not up), MEM1 reserved
   32 MB (told 24), freed blocks decommit their whole 64 KB chunks. xemu:
   Main Plaza's lowest free 2.6 -> 4.7 MB; sweep lowest 5.3 MB. **Not yet
   seen on the console**: the ship path (Landing Site, save in the ship,
   walk out) is the test. `XHW_MEMB` lines now say "game heap N KB free".
   6 MB of the heap are resource buffers held by built objects
   (`CResFactory::BuildAsync`), as on the GameCube.
2. **The ship's save station, 12-14 fps** (7 skinned lists decoded every
   call): the dynamic-template budget 1 -> 2 MB, and a list turned away
   for want of budget retries every 120 frames (`gx_vtx.c`
   `DLC_DYN_RETRY`). Not measured: needs the ship path.
3. **Visor fog too strong**, 5. **the landing cinematic's white frames**,
   and the old "Main Plaza all yellow after damage": one bug. GXInit
   leaves all four lighting channels with the material from the vertex;
   ours were zeroed (from the register). Prime's `CGX` only sets
   `GX_COLOR0`/`GX_COLOR1`, so its alphas came from the material
   register (FF) and every vertex-alpha fade drew at full strength.
   Fixed in `gx_state.c` `GXInit` (aurora does the same). The cinematic
   now has partial lightning flashes, translucent steam, lighter rain
   (`build-xbox/shots/white2`), the hit flash a yellow tint.
4. **Widescreen HUD row**: "Centred" ran over the two-value widget's
   divider; the values are "Inner / Edges" now.

**On red now (2026-10-28, hw6)**: the ship build (LTO + the old PGO
profile, `7bdef5e`) in the console's Prime-X folder; stage
`/c/xemu/hw/prime/stage-vhw6`, map `/c/xemu/hw/prime/prime_x.hw6.map`,
console FTP 192.168.158.113. Pull the user's logs with
`PX_HW=/c/xemu/hw/prime PX_FTP_HOST=192.168.158.113 python
tools/xbox/console.py pull hw6`. Also since hw5: the rain in one call
(`GXPortRainLines`, xemu +7%), the title Options' slider draws (the alpha
fix). PGO retraining was stopped: rerun `tools/xbox/pgo_train.sh tallon
frigate phendrana magmoor chozo` with two slots.

**hw6 results (the user, 2026-10-28; log `/c/xemu/hw/prime/logshw6/boot.log`)**,
the next session's list:
1. **Out of memory again**, at 290 s (hw5: 215 s): `xhw_commit` failed with
   2.9 MB free (MEM1+ARAM 25.8 MB). A single commit wanted more than
   was free: the next build logs its size (`[MEM] out of memory
   committing`). Levers: the texture pool (8 MB, 2.7-4.5 MB of it
   unused in play), triple buffering (1.2 MB), the vertex pool, the
   game heap's partly used chunks. The Frigate scenario also runs out
   in the PGO-instrumented build (bigger image), so pgo/frigate and
   pgo/magmoor gave no profile.
2. **Everything too bright after going into Morph Ball** (the user). Look at
   `CMorphBallShadow.cpp` (sets GX_ALPHA0 and colours directly, around
   the CGX cache) and at whether hw5 did it too (it may be the GXInit
   channel change, `0b407ad`).
3. **Two-value Options rows (Widescreen HUD, Field of View, FPS
   Counter): the divider sits on the text** ("stuck on the r" of Inner)
   on the console. Shoot the pause screen's Display rows in xemu at 4:3
   and compare; the cells may need shorter labels or the divider moved.

Dolphin 2609 is unpacked at `C:\xemu\dolphin\Dolphin-x64` (portable,
the user allowed it); no reference frames taken yet: the interesting
scenes (Tallon, the ship) need a save there, and saves here live in
xemu's HDD.

## Console round hw5 (red, 2026-10-28)

Chain `tools/xbox/scenarios/hw5` (five profiler builds, ThinLTO + PGO with
the retrained profile), logs and shots in `C:\xemu\hw\prime\logs-hw5`,
map `C:\xemu\hw\prime\prime_x.hw5.map`. The old test folders on the
console (`Melee-X-r14*`, `-r15*`, `Prime-X-r1*`, `-r4*`) were removed at the
user's request; `Melee-X` and `Prime-X` (free play, the disc images) stay.

- Ran start to end and returned to the dashboard by itself; no hang, and
  the audio start-up freeze of hw4's r4c didn't recur.
- fps at 480i: front end 32-45, Landing Site 37-46 (hw4 27-41, hw1 ~10),
  Main Plaza 21-46 (hw1 ~5; low while it streams in), Frigate 26-43,
  Phendrana 36-43.
- Landing cinematic: the vertical streaks are gone (particle texcoords).
  4 of 9 shots are washed out white, letterbox bars too: the full-screen
  additive camera filter (lightning?). **Next:** read its vertex colour and
  alpha. Trace builds now print vertex colours for untextured draws; a
  dense lockstep run of frames 640-1400 every 10 (`/c/xemu/px/scen-one`)
  was started and stopped unfinished at the end of the session. If the
  game's alpha is small and the frame still goes white, the blend is at
  fault; if the colour itself is near-white at full alpha, it's the game's
  flash (compare with Dolphin).
- Profile, Landing Site: `GXCallDisplayList` 13%, `memcpy` 10%, the rain's
  per-vertex path ~11% (`GXTexCoord1s16`, `GXPosition3s16`,
  `gx_vtx_flush`), `CEnvFxManager::Update` 3.5%, `BuildBoneRange` 3.3%.
  Main Plaza: display lists 13%, `xgx_draw` 8%.
- Watchdog: a busy stall (the boot screen) over 5 minutes now counts as a
  hang, so a freeze there reboots to the dashboard like any other.

## The first build session (2026-10-27)

Then: the game booted in xemu to the title screen and the file select.
Done that session:

- Phase 0.1: `tools/xbox/msys/build.sh`, `xbox/build.sh`, Docker files and
  Melee-X's tools copied and renamed (`PX_` knobs, title `50580001`). MSYS2
  build works on this PC. **Docker is not installed here**: the Docker half
  of gate 0 is unchecked (CI will cover it, phase 7).
- Phase 0.2: the C++ spike decided **one triple (nxdk's `i386-pc-win32`,
  MSVC ABI, nxdk libc++)** (`docs/decisions.md`). `spikes/cxx` holds it.
- Phase 0.3/0.4: `tools/port_blocks.py` -> `docs/port-blocks.md`; all
  `TARGET_PC` blocks kept, Xbox `Port*` hooks (`docs/decisions.md`).
- Phase 1: game, MusyX, aurora headers imported at `673eacaf`; Melee-X's
  `xbox/` imported (Melee-only files set aside: `card_endian`, `menu.c`,
  `features.c`, `simhash.c`, `stubs.c`; `thp.c` in `xbox/src/sdk/later/`);
  every unit compiles; `default.xbe` links (5.2 MB).
- Phase 2 (part): boot (`xbox/src/sdk/boot.c`), frame boundary
  (`prime_sdk.c`, `vi.c`), DOL read for the default font, THP decode
  (aurora's decoder in `xbox/src/port/thp`), pads.

## Console round hw1 (red, 2026-10-27)

The one hardware round so far (`tools/xbox/scenarios/hw1`, logs and shots in
`C:\xemu\hw\prime\logs-hw1`, map `C:\xemu\hw\prime\prime_x.hw1.map`):
five chained stages (first boot to the Frigate, Tallon, Phendrana, Magmoor,
Chozo Main Plaza) then free play. Red: 480i 4:3 composite (AV pack 1).

- Works: boot, full disc check (matches the Redump hash; took about a
  minute), title, file select, New Game, the Frigate, MP_BOOT_WORLD jumps,
  walking, firing, pause inventory and map screens, MusyX sound effects and
  music (the Frigate's and the Overworld's), rumble, the exit combo.
- **CPU-bound, GPU idle**: Tallon ~10 fps in play (sim 70 ms, display lists
  22 ms, 2800 draws, 66k vertices a frame); Chozo Main Plaza ~5 fps (display
  lists 120 ms, 7000 draws, 148k vertices). Phase 6 starts here.
- Memory: lowest free 3.7 MB (Frigate opening, Main Plaza).
- Console-only bugs: the landing cinematic (Tallon, frame 900) washed out
  white with vertical streaks (fixed 2026-10-28: particle texcoords, see
  the top); the arm cannon missing in Main Plaza (the
  user also saw it "white sometimes").
- The user: the disc check needed explaining (done: the boot screen, E).

## Console rounds hw2, hw3 (red, 2026-10-27)

The first-boot experience: boot screen E, the full disc check with the
intro music, then a second boot (logs `C:\xemu\hw\prime\logs-hw2`, `-hw3`).
The user: "overall it's good", intro approved. Fixed after hw2: the check's
second note line was dimmer; the bar sat at 10% and jumped to 100% on the
second boot (it now eases and the splash waits for it to fill); the music
moved from `intro.pcm` into `default.xbe` (IMA ADPCM, `make_intro.py`);
L+R+Y at boot forces the full check; the watchdog wrote a false "no first
frame" `hang.log` during the check. hw3 confirmed: the music plays from the
XBE, the check passes in ~65 s, the splash costs 19 ms a frame at 25 fps
(7.5 in xemu). **No more console rounds until the user says.**

## Session 2026-10-27 (evening): FPS, Options, GX fixes

- **Draw batching**: short strips, fans and line strips (every text glyph,
  billboards, map surfaces) become lists and join (`gx_vtx.c` `seg_list`).
  Landing Site in xemu: 2900-4300 draws a frame -> ~500, dlist 19-27 ms ->
  5-6, xemu fps 11-14 -> 18-19. LTO + PGO on top: 20-24.
- **ThinLTO + PGO + link order** (`docs/pgo.md`): `XBOX_LTO=1`,
  `XBOX_PGO=xbox/prime.profdata` (first profile: Tallon, Frigate, Phendrana;
  Chozo and Magmoor run out of memory instrumented), `xbox/order.txt`.
- **Builtins**: game code gets `-fhosted -fbuiltin -fno-math-errno` (nxdk's
  wrapper made `fabsf`/`sqrtf`/small `memcpy` calls).
- **Menu text fixed**: CI textures keep their texels once and draw with
  the TLUT loaded last (per-unit palette, `gx_tex.c` `palette_for`); the
  game switches palettes per run of text without reloading the font.
- **Rain**: `RenderRainParticles` writes its RGBA4 colour with
  `GXTexCoord1s16`; taken as a texcoord, every rain batch ended short and no
  rain drew (`gx_vtx.c` `color16_next`, now `put16`).
- **GUI**: `CGuiObject` recomputes only the moved subtree (was n^2).
- **Options rows** on the pause screen (`docs/ui.md`); still to do: the
  title-screen Options, Skip Cutscenes, **button remap page** (the user:
  "button mapping will be expected"), restart prompt for the video row.
- The PC crashed once under 3 xemus + a 16-job build (WHEA machine check,
  CPU core 0): at most 2 xemus, builds at `-j8`, not both at once.

- **Streamed music** (the main menu's, some rooms', movie audio): the AI DMA
  path as MetroidPrimePort drives it, the main loop running the callback
  into a 200 ms queue that MusyX's output mixes in (`prime_sdk.c`,
  `px_sdl3.cpp`). `[AI] streamed audio starts` at the title in xemu; not
  heard on a console yet.
- Port Options: Fusion Suit (System), B is back in menus, three-choice rows.
- PGO retrained on the current code (Tallon, Frigate, Phendrana).

## Console round hw4 (red, 2026-10-27, night)

Chain `tools/xbox/scenarios/hw4`, logs `C:\xemu\hw\prime\logs-hw4chain`.
Landing Site in rain: hw1 ~10 fps -> r4b plain ~24-31 -> r4a ThinLTO + PGO
~27-41 (mostly mid-30s). Shots on the console render right (gun, HUD, rain,
inventory Samus): the earlier "gun white" and sky glitches were likely the
stale-palette bug. **r4c froze** during audio start-up after a chained
launch (after `[AUDIO] found`, before `idle`; no hang.log): flushed
`[AUDIO] step:` markers now say where next time. The user's free play: the
menus work; the front end runs ~17 fps because of the background movie's
decode (THPDec's double-precision FMA on an SSE1 CPU, plain float now).
**No console rounds until the user says.**

## Next

The user (2026-10-27, night): keep going without check-ins; FPS, texture
issues, the planned features; no console rounds until they say.

Open, in rough order (after the focus list at the top):
1. FPS: the console runs the Landing Site at ~27-41 (sim 17-25 ms, display
   lists 4-8 ms). A console profile (`-DXHW_PROF=1`) is the next thing a
   round should bring back. Levers left: skinned models' display lists
   decoded every call (the dynamic budget is full), GPU or further SSE
   skinning, the rain's immediate-mode path, CFishCloud.
2. Visors: X-ray (D-pad Right) and thermal (Down) switch and render in
   Phendrana with `env PX_ALL_ITEMS=1` (`/c/xemu/px/scen-visor2`); the
   Landing Site's scripts filter them (game logic). Compare with Dolphin.
3. Rendering to compare against Dolphin: Main Plaza after taking damage
   (`/c/xemu/px/scen-plaza`, shot 2: the whole screen yellow, energy 74;
   probably the hit flash caught mid-way), the arm cannon's grey-white look
   in bright rooms (the user's "gun white sometimes"), Magmoor's second room
   (`/c/xemu/px/scen-mag`, shot 2: the gun grey, big translucent blue
   panels; maybe the elevator's glass), the visors once they switch.
3b. Magmoor's Lava Lake (`env MP_BOOT_WORLD=3EF8237C:A4719C6A`): the
   "block with vertical streaks" was the particle texcoord bug (fixed
   2026-10-28); the lava and its fire draw right in the sweep now. The
   heat-haze overlay is an indirect warp of an EFB copy, drawn without the
   warp (3c).
3c. Morph Ball (`env PX_ALL_ITEMS=1`, Phendrana): transforms and rolls; the
   transition frame had a fine crosshatch over the whole screen; not seen
   again after the particle texcoord fix (`/c/xemu/px/scen-morph`, shots
   12 and 26 frames after B). Right after the boot spawn in Phendrana the
   view is white for ~150 frames: the elevator's arrival steam (draw of 18
   camera-space puffs at 13% alpha), invisible before the fix. Looks like
   the real game's; confirm against Dolphin. The heat haze over Magmoor's rooms is
   drawn without its indirect warp: its two indirect maps are 512x256
   A8R8G8B8 (EFB copies, new each frame), past `ind_tex`'s 64K-texel limit;
   converting them would read ~1 MB of uncached pool memory a frame, ~5-10
   ms on the console, for a subtle effect. A GPU-side conversion (render
   the copy into the BUMPENVMAP layout) would make it cheap. Compare with
   Dolphin.
4. The audio start-up freeze after a chained launch (round hw4 r4c): the
   flushed `[AUDIO] step:` lines will say where next time.
5. The title screen's Options frame draws no slider (moot for Port
   Options, which has none now).

6. Memory: Main Plaza leaves ~2.6 MB free in xemu (the game's `new` heap
   ~15.7 MB, MEM1+ARAM ~14.9 MB committed, image 9 MB, GPU pools 12.8 MB);
   no room ran out in any sweep. Plan 0.6 (route `rs_new` into MEM1) is
   still open; the texture pool (8 MB, ~4 MB free there) is the easiest to
   give back if a room runs short.

(The first session's list is done: profiling, disc validation, the
interlaced movies, streamed music, the title's text.)

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
- Console round hw5 (`tools/xbox/scenarios/hw5`, the console profile) when
  the user allows a round.

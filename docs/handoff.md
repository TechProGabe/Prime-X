# Handoff: state and working notes

Update this at the end of every session: what was done, what's next,
what's broken, anything the next agent would otherwise rediscover.

## Session 2026-10-28 (night, unattended): fixes in xemu, not on red yet

The user's list: fix the hardware crash (the hw6 Out of memory screen),
the white gun ("Chozo Ruins starts on the white gun every time", flickers
in some rooms), everything else known, and fps. Priorities they gave:
stability > visuals > fps; memory may be traded, the retail look may not.
No hardware rounds until they say.

- **Out of memory** (`451e1bf`): memory-pressure reclaimers
  (`xhw_mem_reclaim`); below 3 MB free the game thread gives back up to
  1 MB a frame, and a failed commit asks again (try-lock only) before the
  Out of memory screen. ARAM is the reclaimer: chunks wholly on the disc
  go at once, the rest after a write to `aram.swp` in the save folder
  (`ar.c`). Test builds: `env PX_MEM_BALLOON_KB=<n>` holds RAM back; Main
  Plaza with 7000 ran down to 0.9 MB free with no failure. ~0.9 MB of
  `.bss` less (GX FIFO, random static, pause blur buffers).
- **Morph Ball washing the scene out** and part of the white gun
  (`451e1bf`): `CGX::SGXState::x4c_flags` was a PowerPC-ordered bitfield;
  `CGX::SetNumChans` never reached GX. Shots: `build-xbox/shots/morph4`
  (before) vs `morphfix_009` (after).
- **The white gun**, three causes: (1) `b858563`: five textures on the
  gun with its world shadow, four units; the stage without one read
  white. Uniform textures are now combiner constants; an overflow reads
  the texture's average. (2) `7dbd64b`: a texture a texmap still held was
  released as "idle" after 600 frames, because `CTexture::Load` doesn't
  load again what it thinks is still bound (true on a GameCube); the
  gun's reflection map in a quiet room went, and the gun read white "a
  while into the room" (Chozo's save corridor at frame 1500 in xemu).
  (3) the channel count (`451e1bf`, below). Shots:
  `build-xbox/shots/plazagun_00*`, `savegun_000`.
- The arm cannon vanishing in a room after a while is retail: after 105 s
  without firing Samus lowers it out of view (`CFidget`, `kS_HolsterBeam`,
  `x30_timeUntilHolster`); firing brings it back. xemu runs that stand
  still hit it (Lava Lake from ~frame 2600). Probably hw1's "arm cannon
  missing in Main Plaza" too.
- xemu slot 1's virtual memory card has the Fusion Suit bonus on (an
  earlier test): its runs show Samus's left hand green, slot 2's orange.
- **Display-list thrash** (`b858563`): only lists unused for 3 frames are
  evicted (hw6's 34 ms of display lists with the vertex pool full).
- `xhw_time_ns` reads the ACPI timer every 100 ms and interpolates with the
  TSC (was an I/O-port read and three 64-bit divisions per call, ~3% of
  the console's CPU in Main Plaza).
- Display-list CPU (`ad202e8`): `DlEntry` hot fields in two cache lines,
  normals packed as CMP (13% more lists fit the vertex pool), rarer
  content checks for long-stable lists, perf buckets inline, no PBR-record
  parse per material, batch tables prefetched. The texture overflow pool
  is a memory reclaimer (Prime never let it go).
- **Port Options > Game Timing: Fixed / Flex** (`f2e78fd`): Flex runs one
  game update per drawn frame (MetroidPrimePort's adaptive mode, dt at
  most 1/30 s); Fixed (default) is 60 a second with catch-up, ~1.5 updates
  per frame at ~40 fps on red. Likely the biggest fps lever left, but
  xemu is too slow to show it: **ask the user to try Flex on red** (and
  watch for physics/timing oddities: MetroidPrimePort calls the mode
  experimental).
- Gate 2 shown in xemu: doctored images (wrong ID, other revision,
  trimmed, a changed file) each stop at their screen; the code repair
  self-test. Fixed on the way: the error card ran off the screen, the
  mismatch card said "1 files" and offered the dashboard under its A/B.
- Options: "FPS Counter" is "Frame Rate" (ran past its button); the
  title's value tables sit at the selected row's own offset.
- A review of the new memory and clock code found races, fixed in
  `ebd001b`: a refill could write a chunk a reclaim had just freed; one
  failed swap write stopped swapping for good; the clock could go
  backwards across threads. Left knowingly: the mixer can read a few ms
  of zeros from a chunk being refilled by another thread (only under
  memory pressure; recently used chunks are reclaimed last).
- PGO retrained on today's code (`xbox/prime.profdata`, 2026-10-29):
  Tallon, Frigate, Phendrana, Magmoor and Chozo all gave profiles for the
  first time (the instrumented build no longer runs out of memory). The
  ship build (`XBOX_LTO=1 XBOX_PGO=xbox/prime.profdata`,
  `build-xbox-lto-pgo/xbe/default.xbe`) boots to the title in xemu. A new
  profile now restarts the -pgo build folder by itself (`xbox/build.sh`).
- Fog volumes are left out (`PostRenderFogs`): they need real depth from
  their Z16 copies, ours are masks, and they drew as solid boxes (the
  peach rectangle in Twin Fires). Proper ones need depth copies that
  carry depth: a GPU path to try on a console.
- Host test `test_aram.py` for the ARAM swap (400k random operations).
- nxdk's printf has no `%f` (prints nothing, shifts the arguments): the
  Threat Assessment distance digits were empty and the `port: frame N at
  X s` milestones garbage. Both use the SDK's `xsdk_snprintf` now
  (`ee76d4f`). The room sweep refuses a non-autopad image (a ship image
  sits on the title: the first ship sweep took 40 minutes for 0 shots).
- The LTO + PGO ship code (built with `-DXHW_AUTOPAD=1`) passes the room
  sweep: all 14 rooms render, the Chozo arm cannon is dark metal. Again
  after the display-list changes and the retrained profile (`2623192`):
  14 rooms, no FATAL, THROW, texture drop or out of memory.
- A review (subagent) of `713d3fd` found nothing; the dynamic path's
  verify was also run in the Frigate (0 differ).
- FPS, from round hw5's `[PROFL]` (memcpy's callers) and buckets:
  dynamic (skinned) lists stage each batch through an L1 scratch buffer
  instead of fetching into the ~800 KB template and copying all of it;
  one-draw lists keep their draw in the entry (no batch table miss);
  `emit_textures`' 40- and 36-byte compares inline (were memcmp calls).
  `-DXGX_DYN_VERIFY=1` checks the dynamic path against the old one: no
  draw differed (Landing Site, Main Plaza). Main Plaza: 1633 of 1933
  cached lists are one draw, 4 layouts in all. Not measured on red.
- **Save round trip in xemu** (`eb03882`): a save station offers nothing
  to an `MP_BOOT_WORLD` game (no card serial: retail); `env
  PX_CARD_FILE=0` makes it card file A. Save Station 1 (Chozo, Samus
  starts on the pad: step off, step back) saved; a fresh boot's main menu
  listed "[Samus A] Chozo Ruins" and Continue loaded it there.
  Scenarios: `tools/xbox/scenarios/save`, then `load` on the same slot.

## Where it stood before (2026-10-28, evening)

The game plays through every world in xemu (room sweep: 14 rooms, no
failure) and on the red console (~35-45 fps in most rooms at 480i). Red
has round hw6's ship build (`7bdef5e`, LTO + PGO): it still ran out of
memory in free play (290 s), and two bugs came back from it (below).
Everything is committed and pushed on `dev`. Ship builds are `XBOX_LTO=1
XBOX_PGO=xbox/prime.profdata` (`docs/pgo.md`); deploy with
`tools/xbox/console.py` (stage folder `stage-v<name>`, console FTP
192.168.158.113, `PX_HW=/c/xemu/hw/prime`).

Done 2026-10-28 (afternoon and evening), newest first:

- **Texture pool 8 -> 7 MB** at 480 (`1d7071c`): Main Plaza's lowest free
  in xemu 5.2 -> 6.6 MB, nothing dropped. Not on the console yet.
- **A failed commit logs its size** (`[MEM] out of memory committing`); one
  in a page fault shows the Out of memory screen, not a crash. The display
  lists' second MB of dynamic templates only while 6 MB is free.
- **The rain's streaks in one call** (`GXPortRainLines`): xemu Landing Site
  17.3 -> 18.5 fps.
- **GXInit's lighting channels take the material from the vertex**
  (`0b407ad`): ours were zeroed, so every vertex-alpha fade drew at full
  strength. Fixed the landing cinematic's white frames, the visor steam in
  the ship, Main Plaza's yellow hit flash, opaque steam and rain, and the
  title Options' missing slider.
- **Game `new` in `CGameAllocator`** (plan 0.6, `6d9f022`): one heap
  instead of two, MEM1 reserved 32 MB (the game told 24), freed blocks
  give their whole 64 KB chunks back. Main Plaza's lowest free in xemu
  2.6 -> 4.7 MB (6.6 with the smaller texture pool).
- Display-list dynamic budget doubled, with retries (the ship's save
  station decoded 7 lists every call on the console).
- The GX FIFO the game allocates, 384 -> 128 KB (never read here).
- Port Options: the Widescreen HUD row reads "Inner / Edges".
- Dolphin 2609 is at `C:\xemu\dolphin\Dolphin-x64` (portable); no
  reference frames yet: the scenes worth comparing need a save there, and
  our saves live in xemu's HDD.

## Next session, in order

1. **Console round hw7, staged 2026-10-29** (the user asked): chain
   `tools/xbox/scenarios/hw7` (hw5's five scenes, then Main Plaza and the
   Landing Site with Game Timing: Flex), the files laid out by console path
   in `C:\xemu\hw\prime\stage-hw7\console` (`F/Applications/Prime-X-r7a`..`g`
   with the profiler build, `F/Applications/Prime-X` the ship build,
   `E/UDATA/50580001` its icon), maps `prime_x.hw7.map` (profiler) and
   `prime_x.hw7ship.map`. Built from `4473399` plus the scenarios. Free
   play on the ship build: an LTO + PGO ship build of `dev` on red, the hw6 path (Landing Site, save in the
   ship, walk on, then Chozo and Magmoor). Check: no Out of memory (look
   for `[MEM] low` and `[AR]` lines: reclaims and swap-outs), the arm
   cannon in Chozo (dark metal with its reflection, not white), the view
   after Morph Ball, the title's Port Options Video rows, fps against hw5
   (Main Plaza 21-46), then the same with **Game Timing: Flex**. The
   profile is fresh (retrained after the display-list changes, `a445123`,
   2026-10-29); retrain again only if much code moved.
2. If memory still runs out on red: another reclaimer is easy to add
   (`xhw_mem_add_reclaim`); left: the display lists' dynamic templates
   (malloc: freed memory mostly stays in the heap).
3. FPS (console profile, hw5 Main Plaza, read at the instruction level):
   what is left in `GXCallDisplayList` after this session's changes is the
   batch table (its own allocation per list: a miss a call) and the
   dynamic lists' template copies; `xgx_draw` ~8%; `CStateManager::Update`.
4. Not done, by choice: `GXSetDstAlpha` and the EFB pixel format (Morph
   Ball's shadow IDs, fog volumes, thermal's DSTALPHA blends) and
   Magmoor's heat-haze warp on the GPU (a new render-to-texture pass: the
   console has faulted on those before, so it wants a console round).
5. Visors and parity: no Dolphin reference frames yet (`docs/renderer.md`).
   The peach rectangle seen once in Twin Fires was a fog volume (now left
   out, see above).
6. Gate leftovers: the 128 MB hold needs a 128 MB-aware BIOS in xemu;
   the save round trip on red and Dolphin `.gci` (phase 4; xemu's round
   trip works); Docker/CI (the workflow exists, `gh` isn't logged in here).

## Session 2026-10-28 (night and morning)

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
  flash, the storm's lightning (drawn at full strength: the GXInit
  channel bug, fixed in the afternoon).
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

## Console round hw7 (red, 2026-10-29)

Chain `tools/xbox/scenarios/hw7` (profiler builds of `4473399`), logs and
shots in `C:\xemu\hw\prime\logs-hw7`, map `prime_x.hw7.map`; what was on
the console before (the save, settings, hw6's xbe) in `logs-hw7pre`.

- fps at 480i against hw5 (median, lowest tenth): front end 42.6 -> 48.2
  (33 -> 43), Landing Site 28.2 -> 34.9 (15.5 -> 16.8), Main Plaza 27.8 ->
  31.9 (22 -> 25; sim 22.2 -> 18.4 ms, display lists 7.8 -> 5.7, draw 7.1 ->
  5.8). `memcmp` 2.5% -> 0.8% in Main Plaza, `memcpy` 10.2% -> 8.8% in the
  Landing Site. The Frigate isn't comparable: the script is by frame, and at
  the higher fps frame 5000 came a minute earlier in game time, still on the
  landing deck (hw5 was inside, ~2000 draws).
- No `[MEM] low`, no swap, no texture drop, no warning in r7a-r7d. The arm
  cannon in Main Plaza is dark metal. One of twelve landing-cinematic shots
  is washed grey (hw5: four of nine).
- **r7e froze** (black screen, no reboot, FTP down 40 minutes until it was
  rebooted) at `[AUDIO] step: codec power-up`: the first codec
  register read after the AC-link cold reset, the CPU held on the bus.
  hw4's r4c froze around the same reads. Fixed in `f73d0d7`: on the
  console, no codec access after the reset (it never had anything to power
  up there). Melee-X has the same code.

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

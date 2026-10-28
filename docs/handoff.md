# Handoff: state and working notes

Update this at the end of every session: what was done, what's next,
what's broken, anything the next agent would otherwise rediscover.

## Where it stands now (2026-10-28, evening)

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

1. **Out of memory on red** (hw6, `/c/xemu/hw/prime/logshw6/boot.log`):
   at 290 s `xhw_commit` failed with 2.9 MB free (MEM1+ARAM 25.8 MB), so
   one commit wanted more than was free. Put the current build on red and
   play the same path (Landing Site, save in the ship, walk on); the log
   now says the size. Levers left: triple buffering (1.2 MB), the vertex
   pool (4 MB, ~0.8 MB free in play), the game heap's partly used chunks,
   ARAM. The PGO-instrumented build runs out in the Frigate (its image is
   4 MB bigger): `pgo/frigate` and `pgo/magmoor` give no profile, and the
   profile in the tree is the old one.
2. **Everything too bright after Morph Ball** (the user, hw6). In xemu
   (`/c/xemu/px/scen-morph2`, `build-xbox/shots/morph4`) the view after
   unmorphing looks paler than the old build's; not proven. Suspects:
   the GXInit channel change (`0b407ad`) together with
   `CMorphBallShadow`'s direct GX calls (GX_ALPHA0 lit, then restored;
   `GXSetDstAlpha(true, 0)` and alpha updates left on, as retail). Trace a
   frame before and after a morph (`-DXGX_DEBUG_TRACE=1`) and diff the
   channel and blend state.
3. **The Options divider sits on the text** (the user, hw6: Widescreen
   HUD, Field of View, FPS Counter). The pause screen draws "Inner |
   Edges" right in xemu (`build-xbox/shots/opt3`), so likely the main
   menu's Port Options (`SOptionsFrontEndFrame`), whose left cell is sized
   for retail's "Off". `/c/xemu/px/scen-portopt` doesn't reach Port
   Options (it opens Options twice): find the path, shoot the Video rows,
   then right-justify `textpane_double0` (and its "b" shadow) for
   Prime-X rows, or shorten the labels.
4. **The ship's save station fps** (12-14 on red in hw5): measure with the
   doubled dynamic budget.
5. FPS: console profile levers left after the rain: the dynamic lists'
   template copies (memcpy from `GXCallDisplayList`, ~5%),
   `CEnvFxManager::Update`, `BuildBoneRange`, CFishCloud.
6. Visors: X-ray (D-pad Right) and thermal (Down) render in Phendrana with
   `env PX_ALL_ITEMS=1` (`/c/xemu/px/scen-visor2`); compare with Dolphin.
7. Magmoor's heat haze draws without its warp: its indirect maps are
   512x256 EFB copies, past `ind_tex`'s 64K-texel limit; a GPU-side
   conversion into the BUMPENVMAP layout would make it cheap.
8. The audio start-up freeze after a chained launch (hw4 r4c): the flushed
   `[AUDIO] step:` lines will say where if it recurs.
9. Retrain PGO once the instrumented build fits the Frigate
   (`tools/xbox/pgo_train.sh tallon frigate phendrana magmoor chozo`, two
   xemu slots: unset `PX_RUN_3`). Kill leftover xemus after a training run:
   they hold the HDD image and later runs silently reuse the old log.

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

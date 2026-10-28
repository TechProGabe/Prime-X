# Handoff: state and working notes

Update this at the end of every session: what was done, what's next,
what's broken, anything the next agent would otherwise rediscover.

## Where it stands now (2026-10-28, overnight)

The game plays through every world in xemu (`tools/xbox/room_sweep.sh`:
14 rooms, no failure) and on the red console (round hw4: the Landing Site
at ~27-41 fps, up from hw1's ~10). Everything is committed and pushed on
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
- **Lines clipped on the CPU** to the near plane and a 2x guard band
  (`gx_vtx.c` `clip_lines`): the rain lines with an end behind or just in
  front of the camera came out flipped or ~300,000 px long.
- Trace builds (`-DXGX_DEBUG_TRACE=1`) print each traced draw's first four
  vertices (position, tc0).

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

Open, in rough order:
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
   transition frame has a fine crosshatch over the whole screen (the
   game's blur through an EFB copy? or the transition's particles, which
   drew striped before 2026-10-28's texcoord fix: recheck first). The heat haze over Magmoor's rooms is
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
- Dolphin on this PC for reference frames (rendering checks in phase 3 are
  judged against Dolphin, which isn't installed): may I install it?
- Console round hw5 (`tools/xbox/scenarios/hw5`, the console profile) when
  the user allows a round.

# Handoff: state and working notes

Update this at the end of every session: what was done, what's next,
what's broken, anything the next agent would otherwise rediscover.

## Session 2026-11-03 (on `dev`, after beta3): black chunks; two fixed control layouts; Phazon Suit save screen

**Results first.** Both fixes below are confirmed on gold and merged to
`main` and released as **v1.0-beta4** (2026-11-03, tag on the merge; notes in `.github/release-notes.md`). Gold has two folders: `Prime-X`, the ship
build of 115f933 (ThinLTO + PGO + log always, MD5
81a4a53cf33756b42319f5da040a93d8: copy-clear fix, layouts, texture fixes),
and `Prime-X-savecheck` (a plain autopad build at 115f933, Save Station 1
with every item and `PX_FUSION=0`); every other test folder was removed.

- **Black chunks: fixed, confirmed on gold.** The tester saw them in
  image A (fixes off) only; A logged 872 and 1163 binds per 600 frames of
  an overwritten copy, B dropped one once and drew none. Logs:
  `C:\xemu\hw\prime\logs-blackgold` (`run1b\boot_prev.log` A,
  `run1b\boot.log` B).
- **Samus missing during the save sequence, black behind the pause
  menu: fixed** (decisions.md "An EFB copy's clear writes only what the
  updates let through"). The Phazon Suit's glow copies the alpha with
  colour updates off; our copy clear wiped the colour too, so the scene
  went but what came after (the station's light). It looked
  console-only because the console folders give every item (Phazon
  Suit) and xemu's settings.ini has the Fusion Suit; `env PX_FUSION=0`
  + `PX_ALL_ITEMS=1` shows it in xemu, `env PX_COPY_CLEAR_ALL=1` brings
  the old clear back for an A/B (xemu: before flat brown and black, after
  the blurred room and Samus). Found with the gold round "held copy"
  (`-DXGX_DEBUG_HELD`, kept: `[HELD]` lines on big EFB copies, binds and
  drops; a BACK screenshot also writes the bound copy's texels,
  swizzled, as the next shotNN.bmp). Confirmed on gold by the tester in
  `Prime-X-savecheck` (logs `C:\xemu\hw\prime\logs-savecheck`: Phazon
  Suit, no crash, one `[MEM] low` reclaimed).

The requests, in order:

- **Black chunks of a Chozo room after saving or loading** (a tester:
  "only temporarily after saving or loading into a save"; `shot26.webp`,
  the 2026-11-02 static-memory change didn't fix it). Not reproduced in
  xemu (save and load scenarios, also with `PX_IMG_DELAY_MS=15`); the
  shot's black is pure 0,0,0 in whole-model pieces, the far room fine.
  Found two holes in the texture cache that give exactly that and last
  while the texture is drawn (docs/decisions.md, 2026-11-03): an EFB copy
  entry was taken for any texture later loaded into the freed copy buffer
  (never revalidated), and a texmap the cache emptied was never loaded again
  because `CTexture::Load` thinks it still holds the texture. Fixed both
  (`efb_stale`, `xsdk_tex_map_cleared`). To confirm on a console: save at a
  Chozo station and load that save a few times; boot.log (Log File on)
  shows `[TEX] EFB copy at ... overwritten by a texture` when case 1 hits.
  If black chunks still show, the next suspects are in this session's
  notes below. **Console A/B staged** (the user asked to replicate it on
  hardware): `-DXGX_TEX_STALE_FIX=0` keeps both cases but logs them (image
  A), image B has the fixes; folders `Prime-X-{A,B}-station` (Chozo Save
  Station 1 as card file A) and `Prime-X-{A,B}-title`, README inside;
  `C:\xemu\hw\prime\stage-black` and `Downloads\Prime-X-blacktest.zip`
  (ROUNDS.md "black A/B"). **Uploaded to gold** (`E:\Applications`,
  UDATA backed up to `C:\xemu\hw\prime\backup-udata-gold-black-20261103-1155`,
  `card_a\` included). A booted-in game (`MP_BOOT_WORLD`) has no items, not
  even the Power Beam (the user: "I can't shoot"): the station folders'
  autopad.txt has `env PX_ALL_ITEMS=1` since, re-uploaded. Waiting on the
  user's run; then pull `boot_Prime-X-{A,B}-{station,title}.log` and the
  shots (named per folder) to `logs-blackgold`.
- **Buttons**: first made the triggers remappable with an X-to-bind mode
  (commit d5c688b), then, the same day, the user: "remove button mapping
  entirely and just do two play schemes ... dual stick mode on by default"
  and "make sure in dual stick mode you can shoot with a or right trigger".
  So: no remapping at all. Controller > **Controls** (was Aiming, `[input]
  aim`) is GameCube or Dual Stick, and `pad.c` maps the buttons from it
  (decisions.md "Two control layouts"): Dual Stick A or RT fires, Black +
  right stick beams; GameCube RT Free Aim, Black map. The Buttons tab, its
  controller drawing, bind mode, the `[portN]` button lines (old files'
  are skipped and rewritten away), the Fire/Pause check and the pause
  Options' list-row hooks are gone (~1100 lines). The beta3 images on the
  consoles and the black A/B images still have remapping.
- Host tests: new test_pad (pad.c's two layouts: Dual Stick A or RT fires
  and Black is Beam Select, GameCube RT is Free Aim and Black the map, B/X
  back in menus), test_settings (rewritten for the fixed layouts: an old
  file's button lines dropped quietly), test_tex_cache (old-cache comparison
  under `-DGX_TEX_NO_EFB_STALE`, plus the stale-copy case),
  test_tex_convert pass; bench_menu builds (Buttons page cases replaced by
  the restart question). xemu: `menu` (four tabs, the Controls row's text
  fits, an old settings.ini's button lines dropped) and `dual_stick` (aims;
  xemu is too slow to catch a bolt or the charge glow in a shot, hence
  test_pad). `C:\xemu\xemu.toml` (shared) was found cut to two lines at
  12:36, not by this session; slot 2 was used instead, the file left alone.
- Ruled out while looking (read, not fixed): world surfaces never wait on
  `TryLockTextures`; dumped textures (pause screen's `DestroyAreaTextures`)
  are held off screen by the pause blur until reloaded, and the save
  screen dumps none; card reads are synchronous; display lists spilled into
  the texture pool never sit in the overflow pool; GX's own list culling
  isn't used by Prime. Untested: a static display list built from memory
  that later changed (never re-checked) would also show as missing pieces
  until the room is left.

## Where it stands (2026-11-02): v1.0-beta3

**v1.0-beta3 is `main` at the merge of `dev` (2026-11-02)**, released by
GitHub Actions from the `v1.0-beta3` tag. Its ship build (MD5
66872d4978c206cd6dd19cd256539853, LTO + PGO with `-DXHW_LOG_ALWAYS=1`;
the release's own XBE is CI's) is in `Prime-X` on both consoles; the user
approved it after testing. What it has over v1.0-beta2 (docs/history.md
"Session 2026-11-02" for the detail, docs/decisions.md for the why):

- **Dual Stick aiming, on by default** (GitHub #2): the right stick aims,
  Beam Select (Black by default) held with the right stick picks beams,
  Aim Speed Low / Medium / High; Aiming: GameCube puts the original back.
  Free aim is modern by default (push up looks up).
- **The crash after quitting to the title and loading again**: DVDClose /
  DVDCancel drain their request now (a read landed in freed memory). The
  same corruption most likely explains the "GPU fault" photo and the black
  chunks of room geometry.
- **Xbox Port Options as Prime's pause Options** (amber hologram, the
  disc's Deface fonts, a black controller on the Buttons tab), modal (the
  game is held, moves are instant). Its glyphs are rebuilt at each open
  (the second open drew freed memory). Changing Video Mode asks in a
  dialog of its own: Later | Restart now, D-pad or left stick.
- **The map**: the zoom works (`TakeMapZoom` gave 0); Dual Stick isn't
  switched off for good by a stale "map open" flag.
- **Fusion Suit** kept in settings.ini (`[game] fusion_suit`), so it stays
  on after a restart.
- **The watchdog** is held off around settings saves and the relaunch /
  quit path: gold's drive took over 6 s to save before Restart now.
- Audio: 8 cold resets in a row without a buffer give up (silent, with the
  notice) instead of restarting every ~100 ms. Brightness, Screen Offset
  and Stretch are gone from the Display options (they did nothing).

Performance is RC1c's (round hw18d, median of the profiler's 5 s lines):

| scene | red 480i | gold 720p |
|---|---|---|
| pirates (Frigate Crash Site) | 39 | 30 |
| Main Plaza | 49 | 37 (46 at 480p) |
| Lava Lake | 43 | 34 |
| Landing Site, rain | 50 | 28 |
| Phendrana, visors | 51 | 32 |

Ship builds are `XBOX_LTO=1 XBOX_PGO=xbox/prime.profdata
XBOX_CMAKE_ARGS=-DPX_LTO_MODE=thin` (`docs/pgo.md`); a round's profiler
image adds `XBOX_CFLAGS="-DXHW_AUTOPAD=1 -DXHW_PROF=1"`. Console rounds
and their files: `C:\xemu\hw\prime\ROUNDS.md`; older sessions and rounds:
`docs/history.md`.

**This PC's xemu slot 1** (`C:\xemu\xemu.toml`) was found cut down to one
EEPROM line ("Failed to load BIOS"); slots 2 and 3 work. Restore slot 1
from slot 2's toml with its own paths, or keep using 2 and 3.

## Next session, in order

0. **v1.0-beta3 is out** (2026-11-02):
   https://github.com/TechProGabe/Prime-X/releases/tag/v1.0-beta3; notes
   in `.github/release-notes.md`. The tag is on 3edef90 (the merge). Its
   own run died restoring the toolchain cache ("No space left on device"
   on the runner); the workflow now frees the runner's disk first, and the
   release was published by a manual run on `main` (8a2efea, the same
   source plus that CI step, `release=v1.0-beta3`). The zip tests clean;
   its `default.xbe` (MD5 0edd8e5a21cf6ca6a69d0f12cb49cd71) passed the code
   check and ran 120 s clean in xemu.
   As with beta2, the release's `default.xbe` is CI's build, not the
   consoles' MSYS2 image (nxdk's source paths are baked in and CI builds
   its own LLVM 21: a few dozen functions inline differently from the
   same source). Player reports come with a boot.log only if they turn
   Log File on (Xbox Port Options > System). GitHub #2 (Dual Stick) closed
   with a comment pointing at the release (2026-11-02).
   - Saved for later (the user): **Xbox button glyphs** in the game's own
     text, sized up in docs/history.md "Session 2026-11-02" (~96 strings,
     48 textures, a `CTextParser` hook; ~2-3 days plus the art).
1. **Watch in the next round** (none planned: hardware only when truly
   needed, the user):
   - the page-0 repair: a `[MEM] page 0 at 0x80000000 read-only ...
     pte now ...` line, and the launch going on;
   - gold's first load into a world, 4-5 s (red ~0.8 s): its reads from
     the image on the nearly full drive;
   - the screenshot writer's `[SHOT] wrote ... (N ms)` on gold;
   - rain columns did little on red's Landing Site (~1.65M streaks
     clipped, ~3K not, ~60K culled in r18d): the columns straddle the
     screen's edges; smaller columns or a per-streak test are the levers.
2. **Audio stuck on red** ("Sound hardware is stuck", silent until a
   power-off): every chain launch after the first in hw16 (r16b-k), hw18
   r18d-e, hw18b and hw18c r18e; none in hw18d, never on gold. It comes
   with relaunching one XBE from another (`PX_NEXT_XBE`); a player
   launching from the dashboard hasn't been seen to hit it. Next: what
   each XBE leaves at exit (the engine running or not,
   `xhw_audio_shutdown`), a warm reset (global control bit 2) before the
   cold one; never codec register reads (they froze the CPU, hw7, hw9).
   Notes: history.md "Console round hw10".
3. **720p fill**: gold's 720p scenes are 28-37 fps, the GPU 12-14 ms a
   frame in Phendrana (hw17). Levers in `docs/performance.md`; a render
   scale below 720p is the user's call (open questions).
4. **The back end** (Main Plaza's biggest cost): record a world model's
   draws into a pushbuffer segment and JUMP to it (M6a; JUMPs proven on
   red in hw15). Then the game's own draw (10-14 ms), the mixer (13% of a
   fight), the visor passes (M5.6). `docs/fps-plan.md`.
5. **Fights** run 30-32 fps under Auto (hw12; Fixed's catch-up made them
   17-18). The sizing notes: history.md "Fights, as sized on 2026-10-30".
6. **Fog volumes** (Magmoor's lava haze, acid, water; `PostRenderFogs`
   draws none): the only Dolphin difference left in the sweep rooms. A new
   render path: the volume's thickness needs the scene's depth where the
   volume dips into geometry, so the depth buffer must be read as a
   texture (aliased as A8R8G8B8 for a DEPENDENT_AR lookup into the ramp,
   or a depth-format texture). Tiled, maybe compressed Z memory on the
   console: build in xemu, trust only after a console round. The user's
   call to start (asked 2026-10-30).
7. If memory runs out on red again: another reclaimer is easy to add
   (`xhw_mem_add_reclaim`); left: the display lists' dynamic templates.
8. The HUD minimap is 150-300 draws a frame (fill and outline alternate per
   surface, `GXSetTevKColor` between): only a different draw method would
   join them; only if it keeps the retail look exactly.
9. Not done, by choice: `GXSetDstAlpha` and the EFB pixel format (Morph
   Ball's shadow IDs; thermal's DSTALPHA blends, though thermal matches
   Dolphin as it is). Magmoor's "heat haze without its warp" was the
   fog-volume pass all along (Next 6).
10. Parity with Dolphin (`tools/xbox/dolphin_ref.py`, docs/testing.md):
   the 14 sweep rooms match (2026-10-30) but for the fog volumes; thermal
   matches since its palette pass works (`--visor thermal`); X-ray can't be
   held that way (no world pass), scan needs a target to compare.
11. Gate leftovers: the 128 MB hold needs a 128 MB-aware BIOS in xemu; the
   save round trip on red and Dolphin `.gci` (phase 4); Docker/CI (the
   workflow exists, `gh` isn't logged in here).

## Working notes

- Consoles (FTP xbox/xbox, down while the game runs): red
  192.168.158.113, apps in `F:\Applications`, composite 480i; gold
  192.168.158.125, apps in `E:\Applications`, 720p, a stock-like drive
  with ~250-450 MB free. Saves and logs on both in `E:\UDATA\50580001`:
  back it up before an upload, pull it whole after a round.
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

Added 2026-10-31:

- 720p ticks at 30 Hz; 480 keeps Auto (variable) ("at 480 variable is
  still preferred").
- Stock drives matter: gold's is what most players have, nearly full
  ("if that's a common issue, we should address it").
- Release builds: boot.log is a Port Options setting, default Off.
- No hardware rounds unless truly needed; at most two xemus at once,
  slots 2 and 3.
- Stability first, then visuals, then frame rate.

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
- Fog volumes (Next 6): start the depth-texture render path, knowing it
  needs a console round before it can be trusted (asked 2026-10-30).

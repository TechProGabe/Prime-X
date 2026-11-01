# Handoff: state and working notes

Update this at the end of every session: what was done, what's next,
what's broken, anything the next agent would otherwise rediscover.

## Where it stands (2026-10-31): v1.0 beta (release candidate 1c)

**RC1c is 0ff5ed0.** Its release build (`stage-hw18d/ship`) is in
`Prime-X` on both consoles (red `F:\Applications`, gold
`E:\Applications`; MD5 checked against the stage after round hw18d), next
to the profiler image (`Prime-X-prof`) and round hw18d's chain. The user
calls it "potentially release candidate 1"; it is theirs to play and to
call. Round hw18d ran every scene on both consoles with no crash, hang,
watchdog or fault.

What RC1 has over the free-play builds of 2026-10-30 (decisions.md for
each):

- 720p: 30 Hz ticks throughout (480 keeps Auto, the user), the vertex
  ring in the video library's idle framebuffer, a 7 MB texture pool.
- Stock drives (gold's: nearly full, slow to grow files): no boot.log in
  release builds unless Port Options > System > Log File is on; boot.log
  through a writer thread; the DVD read queue 64 -> 1024; `aram.swp` kept
  at 8 MB from boot to boot. The ARQ worker (copies and swap writes off
  the game thread) is built but off (`XSDK_ARQ_ASYNC 0`): gold froze with
  it on.
- `pb_init`'s store to 0x80000000: `xhw_page0_guard` claims physical page
  0 if it is free, and gives it back its write bit if it is mapped
  read-only (hw18c's crashes; the repair hasn't had to act on hardware
  yet).
- Rain in 4 x 4 columns: inside columns skip the clip, off-screen columns
  aren't drawn.

RC1c in round hw18d (median of the profiler's 5 s lines):

| scene | red 480i | gold 720p |
|---|---|---|
| pirates (Frigate Crash Site) | 39 | 30 |
| Main Plaza | 49 | 37 (46 at 480p) |
| Lava Lake | 43 | 34 |
| Landing Site, rain | 50 | 28 |
| Phendrana, visors | 51 | 32 |

On `dev` after RC1c: 6963d15, screenshots written whole in 64 KB pieces
(gold took 47 s a 720p shot). Not in RC1: test builds and the Screen
Capture setting only, saved for a later round (the user).

Ship builds are `XBOX_LTO=1 XBOX_PGO=xbox/prime.profdata
XBOX_CMAKE_ARGS=-DPX_LTO_MODE=thin` (`docs/pgo.md`); a round's profiler
image adds `XBOX_CFLAGS="-DXHW_AUTOPAD=1 -DXHW_PROF=1"`. Console rounds
and their files: `C:\xemu\hw\prime\ROUNDS.md`; older sessions and rounds
(hw1-hw10, and hw16-hw18d's notes): `docs/history.md`.

## Session 2026-11-01: the beta's first three bug reports

Fixed on `dev` (built with `-DXHW_AUTOPAD=1`, checked in xemu; no console
round):

1. **GitHub #1, "Swapping jump to A"**: a Buttons row only set its own
   button, so A on Jump left X on Jump too and nothing on Fire (GameCube
   A, which confirms in every menu: "unable to select anything"). Now the
   button that had the action takes this one's (a swap), Fire can't
   leave the last button that has it (`port_xbox_options.cpp` `Step`),
   and in any menu Xbox A confirms as B backs whatever the layout
   (`pad.c`, the menu flag).
2. **The no-disc card's timeout** (a tester: don't auto-reboot, the card is
   the guide): every error card stays until a button is pressed, no
   timeout (`xhw_main.c` `wait_button_then_dashboard`).
3. **Overclocked consoles run in slow motion** (two testers, Cerbios
   overclock settings; the menu "severely throttled"; a tester's patched
   XBE ran right, patch unknown). No log from such a console, so the cause
   is inferred: the pacer waits by the clock for 60 Hz, and only a clock
   that runs slow throttles the menu. Two guards, both in `xhw_sys.c` and
   described in `docs/boot-safety.md`: `xhw_clock_display_frame` counts
   VBlank against the clock over the first 3 s of frames and scales the
   clock to the display when the ACPI timer is off (an FSB overclock can
   move the chipset's reference); `rdtsc` is timed after the code check
   and after that window, and if it traps (a patcher's int 2Eh) the clock
   reads the ACPI timer on every call and the interrupt lock uses the
   kernel's 1 ms tick. `[CLOCK]` lines in boot.log, release builds too.
   In xemu: `scenarios/clock` (`PX_CLOCK_SKEW=120`) logged the display at
   49.959 Hz by the clock, scaled time by 0.8327 (net 0.99918) and agreed
   at 60.291 Hz in the next window; the plain title run agreed at 59.922.
   An overclocker's test is arranged (the user, 2026-11-01): a normal
   free-play build of d8731ca (the ship recipe, ThinLTO + the committed
   `prime.profdata`, no autopad; MD5 b84e032a3d877130d97b8f3aa9bedffe)
   is in the user's Downloads as `Prime-X-test-d8731ca.zip` with a
   README-TEST.txt: turn Port Options > System > Log File on, back out
   (saves), restart, play a minute, quit with L+R+BACK+BLACK, send
   `E:\UDATA\50580001\boot.log` plus the overclock settings and CPU.
   **Next: get a boot.log from an overclocked console** (Port Options >
   System > Log File on, or a test build), and ask the tester what the
   XBE patch was: the `[CLOCK]` lines say whether the timer moved, rdtsc
   traps, or neither (then look elsewhere: the ACPI I/O read's cost, the
   kernel tick, the disc). xemu can't overclock; `env PX_CLOCK_SKEW=120`
   in a scenario fakes a fast timer to exercise the correction.

## Next session, in order

0. **v1.0 beta is out** (2026-10-31): RC1c passed the user's free play;
   dev merged into `main`, tagged `v1.0-beta`. The release is built and
   published by GitHub Actions (the user): pushing the tag runs
   `.github/workflows/build.yml` (docs/toolchain.md "Release"), a full
   release marked latest. That build is from main's source on Linux
   (RC1c plus 6963d15's screenshot writer and docs), not the MSYS2 binary
   the consoles ran (MD5 7b0273f0...); likely this repo's first CI run, so
   check it went through. Bug reports from players come with a boot.log
   only if they turn Log File on.
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

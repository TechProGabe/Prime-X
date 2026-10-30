# Testing

Melee-X's `docs/testing.md` is the model: host tests, xemu runs, the
console loop, console rounds, logs, symbolizing crashes, the first-boot
checklist. Phase 1 copies its tools; this file grows with them.

## Host tests (no Xbox)

- The GX/NV2A tests from Melee-X (`test_tex_convert`, `test_vp_encoder`,
  `test_vp_opt`, `test_rc`, `test_fog`, `test_pool`, `test_tex_cache`,
  `test_dl_cull`, `test_audio_mix`, `test_mplib`): they must pass unchanged
  after the import.
- `test_aram.py`: ARAM's disc-backed pages and swap file (`ar.c`) under
  random DMA writes (disc copies and plain data), DMA and CPU reads,
  reclaims (normal and urgent) and failed swap writes, against a shadow
  copy. `test_rc.py` also checks the arm cannon's TEV with texture
  constants.
- New: `test_disc_check.py`, the disc validation (`docs/boot-safety.md`)
  against doctored headers and FSTs built in the test (no game data):
  every rejection reason, the `.ciso` coverage check, CRC mismatches.
- New: `test_tsc_scale.py`, the TSC-rate scaling at 733 MHz, 1.0 GHz and
  1.4 GHz.
- New: the `CHECK_SIZEOF` build (it fails to compile when a layout is wrong).
- MetroidPrimePort's GPU-free port tests (`ctest -L port`) where they cover
  code Prime-X keeps (save bit fields, truncated streams, pathfinder bounds).

## xemu

`tools/xbox/xemu_run.sh` with `PX_*` variables, one instance at a time,
`[FBDUMP]` screenshots for looking at frames (never the desktop). Use it for
boot, correctness and Dolphin comparisons, not frame rates. Scenarios
(`tools/xbox/scenarios/`) script the pad: per benchmark room
(`docs/performance.md`), per visor, the boot-safety cases, settings rows.

## Jumping to a room (level skip)

Test builds (`XBOX_CFLAGS=-DXHW_AUTOPAD=1`) read `autopad.txt` from the
XBE's folder (`D:\`): in xemu it is staged with `PX_STAGE_EXTRA`, on a
console put it next to `default.xbe`. Its `env` lines set what the game's
`getenv` sees, so MetroidPrimePort's test hooks work the same in both:

| line | effect |
|---|---|
| `env MP_BOOT_WORLD=<MLVL>[:<MREA>]` | no logos or front end: a new game starts in that world (its default room without an MREA). Logged as `[TEST] boot world` |
| `env MP_FAST_BOOT=1` | skip the boot logos |
| `env MP_SKIP_CUTSCENES=1`, `env MP_CUTSCENE_SPEED=4` | skip or speed up cinematics |
| `env PX_MEM_BALLOON_KB=<n>` | test builds: hold n KB of RAM from boot, to play the console's low-memory moments in xemu (the reclaimers, `[MEM] low`, the swap file, the Out of memory screen); Main Plaza with 7000 runs at ~1 MB free |
| `env PX_ALL_ITEMS=1` | with `MP_BOOT_WORLD`: every item (visors, beams, suits, 250 missiles, 14 tanks) |
| `env PX_CARD_FILE=<0-2>` | with `MP_BOOT_WORLD`: the game is that memory-card file, so a save station offers to save (`MP_BOOT_WORLD=83F6FF6F:1D5E482C` is Chozo's Save Station 1, where Samus starts on the pad: step off and back on) |
| `env PX_SKIPPABLE=1` | Port Options' Skip Cutscenes on, over settings.ini |
| `env PX_WIDESCREEN=0`, `=1`, `=2` | Port Options' Widescreen: 4:3, as the dashboard, 16:9 (over settings.ini) |
| `env MP_INVULNERABLE=1` | no damage |
| `env PX_LOCKSTEP=1` | one simulation tick per frame (comparable screenshots) |

The ids come from your own image:

```sh
python tools/xbox/prime_rooms.py "<GM8E01 image>"                   # every world and room
python tools/xbox/prime_rooms.py "<image>" --find "reactor"         # matching rooms
python tools/xbox/prime_rooms.py "<image>" --env "Landing Site"     # the env line to paste
```

Worlds (USA 1.00): Frigate `158EFE17`, Tallon Overworld `39F2DE28`, Chozo
Ruins `83F6FF6F`, Magmoor Caverns `3EF8237C`, Phendrana Drifts `A8BE6291`,
Phazon Mines `B1AC4D65`, Impact Crater `C13B09D1`.

## Dolphin reference frames

What retail draws, from the same image in Dolphin, without a save or a
controller: `tools/xbox/dolphin_ref.py` boots it straight into a new game
in a room (two Gecko codes on the retail DOL do what `MP_BOOT_WORLD` does:
the front end goes to the game, the Frigate becomes the world and room
asked for), dumps every frame and keeps one every few seconds:

```sh
python tools/xbox/dolphin_ref.py plaza                    # room_sweep.sh's names, or <MLVL>[:<MREA>]
python tools/xbox/dolphin_ref.py landing --secs 90 --every 3
python tools/xbox/dolphin_ref.py plaza --gui              # Dolphin's window, to play from there
```

Frames go to `build-xbox/shots/dolphin/<room>/SSSS.png` (the second of the
dump; the room is in view ~25 s after boot, the logos and the load before
it). The whole dump (FFV1 AVI, ~5 MB/s) stays in
`C:\xemu\dolphin\px-user\Dump\Frames` until the next run: cut a dense run
from it with `ffmpeg -ss <s> -t <n> -i <avi> -vf fps=4 out%03d.png`.
Dolphin (portable, `C:\xemu\dolphin\Dolphin-x64`) runs from its own user
folder `C:\xemu\dolphin\px-user`, native 640x480, D3D, a fresh card in
slot A. Dolphin movies (`.dtm`) were tried for the input first: with one
playing, this build rendered no frames at all.

## Consoles

The user's two consoles, as on Melee-X (its `CLAUDE.md` "Hardware"): **red**
(game in `/F/Applications/Prime-X/`) and **gold** (softmod, no F:, game in
`/E/Applications/Prime-X/`), FTP `xbox`/`xbox`, addresses kept off the repo
(`PX_FTP_HOST`). `tools/xbox/console.py stage|deploy|pull`, console rounds for
A/B runs. Keep each deployed build's `prime_x.map` for symbolizing its logs.
Pull logs before deploying: each boot rotates `boot.log`.

## First-boot checklist (on each console, each release candidate)

- the splash plays, BACK-held boot gives 480i and says so;
- `[DISC]` full check passes, `[CPU]` and `[MEM]` lines sane, `[BOOT] code
  check` clean;
- title screen, Options rows (`docs/ui.md`) each do what they say;
- new game to the Frigate's first save station, save, power cycle, load;
- each video mode and aspect once; FPS counter matches `[PERF]`;
- L + R + Back + Black quits to the dashboard.

## Room sweep

`tools/xbox/room_sweep.sh [default.xbe]` boots fourteen rooms across every
world in xemu (two at a time), takes two shots in each and lists any FATAL,
THROW, texture drop or out-of-memory line; shots in
`build-xbox/shots/sweep`. About 40 minutes. The first one (2026-10-28):
nothing failed; every room rendered. The image must be an autopad build
(`XBOX_CFLAGS=-DXHW_AUTOPAD=1`, also with `XBOX_LTO`/`XBOX_PGO` for the
ship code): a release image ignores the script and sits on the title, and
the sweep now refuses one.

`-DXGX_DYN_VERIFY=1` (test builds): every dynamic display-list draw is
also made the old way, before and after, and compared; `[DLC] dyn verify:
N draws, M differ (K more with arrays that changed during the call)` every
600 frames. K is the verify's own doing (its megabyte mallocs land on a
skinning buffer the game has already freed); M must be 0. 2026-10-29:
Landing Site 29914 draws, Main Plaza 46877, none differed.

## 720p in xemu

Slot 3 (`PX_RUN_3`, `PX_XEMU_ARGS_3`) is an HD slot: HDTV AV pack and its
own EEPROM with 480p, 720p and widescreen allowed
(`tools/xbox/eeprom_video.py <copy of eeprom.bin>`). `env PX_VIDEO=720` in
a scenario runs 1280x720 there.


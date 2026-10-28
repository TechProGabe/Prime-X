# Testing

Melee-X's `docs/testing.md` is the model: host tests, xemu runs, the
console loop, console rounds, logs, symbolizing crashes, the first-boot
checklist. Phase 1 copies its tools; this file grows with them.

## Host tests (no Xbox)

- The GX/NV2A tests from Melee-X (`test_tex_convert`, `test_vp_encoder`,
  `test_vp_opt`, `test_rc`, `test_fog`, `test_pool`, `test_tex_cache`,
  `test_dl_cull`, `test_audio_mix`, `test_mplib`): they must pass unchanged
  after the import.
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
| `env PX_ALL_ITEMS=1` | with `MP_BOOT_WORLD`: every item (visors, beams, suits, 250 missiles, 14 tanks) |
| `env PX_SKIPPABLE=1` | Port Options' Skip Cutscenes on, over settings.ini |
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

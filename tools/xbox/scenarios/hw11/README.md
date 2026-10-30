# hw11: the mixer's share, ticks apart from drawing, Game Timing: Auto

Milestone 0 of `docs/fps-plan.md` (and a first look at milestone 1).
Profiler builds (`XBOX_LTO=1 XBOX_PGO=xbox/prime.profdata
XBOX_CFLAGS="-DXHW_AUTOPAD=1 -DXHW_PROF=1"`) of `dev` for the chain,
`Prime-X-r11a` .. `r11l`; the ship build (`XBOX_LTO=1
XBOX_PGO=xbox/prime.profdata`) in the plain `Prime-X` folder. New since
hw10:

- [PERF]'s "audio" is the MusyX mixer thread's own time (it was always 0);
  "render" is the game's draw (the world's traversal), so "sim" is the
  ticks and `ms per tick` is new; [PROFS] is a tick-only profile;
- [PROFA]: the profiler's samples of the mixer thread (they used to land
  on the game code it interrupted); prof_report.py prints the GPU-wait
  share;
- [NV2A] draw statistics at each scene's end (NEXT), not only every 600
  frames;
- Game Timing: Auto (the new default): 60 game updates a second while the
  frame fits in 16 ms, 30 drawn at 60 between the last two when it
  doesn't; [PERF] lines end with Auto's share of frames at 30 Hz, and
  `[TIMING] auto:` lines log the switches.

r11b-h run Game Timing: Fixed (`env PX_SIM_AUTO=0`), hw10's scenes, so
their fps should match hw10's and they give the mixer's number and the
tick/draw split per room. r11i-l run Auto (no env line) in the heaviest
rooms.

| folder | scene | look for |
|---|---|---|
| r11a | front end, Port Options | the Game Timing row: Auto, Fixed, Flex |
| r11b | Landing Site (rain), Fixed | fps against r10c; audio %, ms per tick |
| r11c | Frigate Crash Site pirates, Fixed | against r10d (17-18 fps); ms per tick in the fight |
| r11d | Main Plaza, Fixed | against r10e (30-36) |
| r11e | Phendrana, visors, Fixed | against r10f; GPU-wait share |
| r11f | Lava Lake, Fixed | against r10g (27-28) |
| r11g | Frigate opening, Fixed | against r10h (50-60) |
| r11h | pause screen over Main Plaza, Fixed | hw8 free play 14-17 fps; the map and the inventory |
| r11i | pirates, Auto | fps against r11c (model: 33-36); the shots: motion smooth, no snaps |
| r11j | Main Plaza, Auto | against r11d (model ~45) |
| r11k | Landing Site, Auto | against r11b; the rain |
| r11l | Lava Lake, Auto | against r11f (model ~45); the chain ends at the dashboard |

Launch `Prime-X-r11a`; each scene starts the next (`PX_NEXT_XBE`).
Back up `E:\UDATA\50580001` (saves, settings.ini) before uploading; pull
the logs (the save folder whole). Maps: `prime_x.hw11.map` (profiler),
`prime_x.hw11ship.map` (`Prime-X`).

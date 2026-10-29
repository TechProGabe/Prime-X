# hw7: after the night's fixes (staged 2026-10-29)

Seven chained profiler builds (`XBOX_LTO=1 XBOX_PGO=xbox/prime.profdata
XBOX_CFLAGS="-DXHW_AUTOPAD=1 -DXHW_PROF=1"`), one folder each under
`F:\Applications\`, the last back to the dashboard. Each folder holds the
same `default.xbe` and `default.tbn` with its own `autopad.txt`. r7a-r7e are
hw5's scenes unchanged, so their `[PROF*]` lines and fps compare with hw5
directly; r7f and r7g repeat Main Plaza and the Landing Site with Game
Timing: Flex (`env PX_SIM_ADAPTIVE=1`). Read the profiles with
`python tools/xbox/prof_report.py boot_Prime-X-r7c.log --map <the build's map>`
(`C:\xemu\hw\prime\prime_x.hw7.map`).

| folder | what |
|---|---|
| `Prime-X-r7a` | the front end: title movie, main menu, Port Options |
| `Prime-X-r7b` | Landing Site in rain (hw5: 37-46 fps; memcpy 10%, half of it dynamic display lists) |
| `Prime-X-r7c` | Chozo Ruins' Main Plaza (hw5: 21-46 fps); the arm cannon should be dark metal, not white |
| `Prime-X-r7d` | the Frigate's opening (hw5: 26-43) |
| `Prime-X-r7e` | Phendrana Drifts (hw5: 36-43) |
| `Prime-X-r7f` | Main Plaza with Game Timing: Flex |
| `Prime-X-r7g` | Landing Site with Game Timing: Flex, then the dashboard |

What it answers: what the dynamic display-list staging and the one-draw
lists (713d3fd) did to `GXCallDisplayList` and `memcpy` on the Pentium III,
and how much Flex gains over Fixed in the two CPU-bound rooms.

The plain `Prime-X` folder gets the ship build (ThinLTO + PGO, no script)
for free play, the hw6 path: Landing Site, save in the ship, walk on to
Chozo and Magmoor. Look for: no Out of memory screen (`[MEM] low` and `[AR]`
lines in `boot.log` say how close it came), the arm cannon in Chozo, the
view after Morph Ball, the title's Port Options Video rows, a save and a
load after a power cycle, then the same rooms with Game Timing: Flex.

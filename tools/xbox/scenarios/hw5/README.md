# hw5: the console profile round (2026-10-28)

Five chained profiler builds (`XBOX_LTO=1 XBOX_PGO=xbox/prime.profdata
XBOX_CFLAGS="-DXHW_AUTOPAD=1 -DXHW_PROF=1"`), one folder each under
`F:\Applications\`, the last back to the dashboard. The sampling profiler's
`[PROF*]` lines land in each `boot_<folder>.log`; read them with
`python tools/xbox/prof_report.py boot_Prime-X-r5b.log --map <the build's map>`
(keep the map: `C:\xemu\hw\prime\prime_x.hw5.map`).

| folder | what |
|---|---|
| `Prime-X-r5a` | the front end: title movie, main menu, Port Options |
| `Prime-X-r5b` | Landing Site in rain (round hw4: ~27-41 fps); shots every 100 frames through the landing cinematic (hw1's white streaks: particle texcoords, fixed 2026-10-28) |
| `Prime-X-r5c` | Chozo Ruins' Main Plaza (hw1 ~5 fps; display lists now spill into the texture pool) |
| `Prime-X-r5d` | the Frigate's opening (the heaviest scene in xemu) |
| `Prime-X-r5e` | Phendrana Drifts |

Result (2026-10-28): ran through and back to the dashboard; fps and the
profile are in `docs/handoff.md` ("Console round hw5") and
`docs/roadmap.md`.

What it answers: where the console's ~15-25 ms of "sim" goes (xemu's CPU
is faster than the Pentium III and its proportions differ), and whether
the audio start-up freeze after a chained launch (round hw4 r4c) recurs:
the `[AUDIO] step:` lines say where.

The plain `Prime-X` folder gets the ship build (ThinLTO + PGO, no script)
for free play: particles now draw their whole textures (steam, mist,
fire), so rooms look busier than in hw4.

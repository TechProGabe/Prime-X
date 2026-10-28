# hw4: the performance round (2026-10-27)

After the draw batching, ThinLTO + PGO + link order, the builtins, the
display-list decoder's fast path and SSE skinning (docs/performance.md).
Five chained test builds (`-DXHW_AUTOPAD=1 -DXHW_MEMB=30`), one folder each
under `F:\Applications\`; the last goes back to the dashboard. The plain
`Prime-X` folder (the ship build, no script; the disc image lives there) is
deployed apart, for free play: the main menu's Port Options, the pause
screen's Options, the button remap.

| folder | build | what |
|---|---|---|
| `Prime-X-r4a` | ThinLTO + PGO | Landing Site (rain): walk, turn, fire, jump, lock on, ~50 s |
| `Prime-X-r4b` | plain | the same: the console's A/B of LTO + PGO |
| `Prime-X-r4c` | ThinLTO + PGO | Chozo Ruins' Main Plaza (hw1: ~5 fps) |
| `Prime-X-r4d` | ThinLTO + PGO | the Frigate |
| `Prime-X-r4e` | ThinLTO + PGO | Magmoor Caverns |

Each ends with `NEXT`; logs are `E:\UDATA\50580001\boot_<folder>.log`,
shots `shot_<folder>_NN.bmp`. hw1 for comparison: Tallon ~10 fps (sim 70 ms,
display lists 22 ms, 2800 draws), Main Plaza ~5 fps (display lists 120 ms,
7000 draws).

# hw1: the first console round (2026-10-27)

Five chained test builds (`-DXHW_AUTOPAD=1 -DXHW_MEMB=30`), one folder each
under `F:\Applications\`, then the plain `Prime-X` folder (the disc image
lives there; test builds find it) for free play:

| folder | what |
|---|---|
| `Prime-X-r1a` | first boot of the image: the full disc check, the title, START, the file select, New Game, the Frigate opening |
| `Prime-X-r1b` | `MP_BOOT_WORLD` Tallon Overworld: the Landing Site, walk, fire, charge, jump, visor, pause, map |
| `Prime-X-r1c` | Phendrana Drifts' default room |
| `Prime-X-r1d` | Magmoor Caverns' default room (heat haze) |
| `Prime-X-r1e` | Chozo Ruins, Main Plaza (a big room) |

Each ends with `NEXT` (the next folder's XBE); its log is kept as
`E:\UDATA\50580001\boot_<folder>.log` and its shots as
`shot_<folder>_NN.bmp`.

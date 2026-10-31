# hw18: red and gold at once

One profiler image (ThinLTO + PGO, map `prime_x.hw18.map`) for both, and
the release build in `Prime-X` on both for free play (no boot.log unless
Port Options > System > Log File is on). Red: `F:\Applications`, 480i.
Gold: `E:\Applications`, 720p (its stock drive, little free space).

New since hw16/hw17: rain in columns (no clip for the columns inside, none
drawn off the screen), 720p at 30 Hz ticks, the vertex ring in the idle
video framebuffer and a 7 MB texture pool at 720p, boot.log through a
writer thread (and off in release builds), the swap writes on a worker,
ARQ copies on a worker, the DVD read queue 64 -> 1024 (room streaming no
longer waits for the drive).

| console | folder | video | scene |
|---|---|---|---|
| red | r18a | 480i | pirates: against r16a (37-38) |
| red | r18b | 480i | Main Plaza: against r16b (47-48) |
| red | r18c | 480i | Lava Lake: against r16c (44-45) |
| red | r18d | 480i | Landing Site, rain in columns: against r16d (35-38, GPU wait 7 ms) |
| red | r18e | 480i | Phendrana, visors: against r16f; then the dashboard |
| gold | g18a | 720 | Main Plaza at 720p: against r17a (13-15, lists decoded) |
| gold | g18b | 720 | pirates at 720p: against r17b (27-30) |
| gold | g18c | 720 | Landing Site at 720p: against r17c |
| gold | g18d | 720 | Lava Lake at 720p: against r17d |
| gold | g18e | 720 | Phendrana, visors, at 720p: against r17e |
| gold | g18f | 480 | Main Plaza at 480p: against r17f; then the dashboard |

Red: launch `Prime-X-r18a`. Gold: launch `Prime-X-g18a`. Pull both save
folders whole. Look at stalls (`MP stall`, `[WDOG]`), `[DVD] queue`,
`[AR]`, fps against hw16 (red) and hw17 (gold).

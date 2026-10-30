# hw16: the display-list cache's two costs

One ThinLTO + PGO profiler image (`-O3`, map `prime_x.hw16.map`); the ship
build in `Prime-X` is the same code without the profiler.

New since hw15: a room's geometry registered as static memory, so its
cached lists skip their content checks (~5% of Main Plaza's CPU in hw15),
the cache's bucket chains in compact arrays (~3.5%), and the audio
deadlock fix (`CMemory` without the interrupt lock).

| folder | scene | against |
|---|---|---|
| r16a | pirates, Auto | r15a (34-35 fps) |
| r16b | Main Plaza, Auto | r15b (43-44); `[DLC] static memory:` lines |
| r16c | Lava Lake, Auto | r15c (44-45) |
| r16d | Landing Site (rain), Auto; the chain ends at the dashboard | r15d (35-37) |

Launch `Prime-X-r16a`; pull the save folder whole.

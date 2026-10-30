# hw15: texture-only draws, fewer clock reads, the pushbuffer tests again

One ThinLTO + PGO profiler image (`-O3`, map `prime_x.hw15.map`); the ship
build in `Prime-X` is the same code without the profiler.

New since hw14: texture-only draws skip the vertex-program key and the
texgen rows (`XGX_MAPS_FAST`; `xgx_draw` was ~17% of Main Plaza's CPU),
the allocator's and the particle systems' write-only clock reads are gone
(`xhw_time_ns` ~1% of a fight), and the pushbuffer tests' second version.

| folder | scene | against |
|---|---|---|
| r15a | pirates, Auto | r14a (33 fps) |
| r15b | Main Plaza, Auto | r14b (38-39) |
| r15c | Lava Lake, Auto | r14c (42-43) |
| r15d | Landing Site (rain), Auto | r14d (35-37) |
| r15e | front end: the pushbuffer tests; the chain ends at the dashboard | `[NV2A] pushbuffer jump test:` and `call test:` lines, with the pusher's error code |

Launch `Prime-X-r15a`; pull the save folder whole.

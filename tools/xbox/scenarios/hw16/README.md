# hw16: everything since hw15, and three A/Bs

Three profiler images (`-O3`, PGO): **P** ThinLTO (map `prime_x.hw16.map`),
**F** full LTO (`-DPX_LTO_MODE=full`, map `prime_x.hw16full.map`), **K**
the busy-GPU pushbuffer kick at 16384 words (`-DXGX_PB_KICK=16384`, map
`prime_x.hw16kick.map`). The ship build in `Prime-X` is P without the
profiler. Each scene's folder holds its image (the `image` file says which).

New since hw15: a room's geometry as static memory (no content checks on
its lists), compact list-cache chains, texture binds remembered per
texture object, the next surface's material prefetched, covering vertex
programs, the mixer's resampler loop, snow between ticks in one call,
particle quads (textured and untextured) in one call, rain on the GPU
(the fall and the streaks' clip in the vertex program), the audio
deadlock fix, the PGO profile retrained.

| folder | image | scene |
|---|---|---|
| r16a | P | pirates, Auto: against r15a (34-35) |
| r16b | P | Main Plaza, Auto: against r15b (43-44) |
| r16c | P | Lava Lake, Auto: against r15c (44-45) |
| r16d | P | Landing Site (rain on the GPU), Auto: against r15d (35-37) |
| r16e | P | Landing Site, rain on the CPU (PX_GPU_RAIN=0): the A/B for r16d |
| r16f | P | Phendrana, visors, Auto: against r14e |
| r16g | P | Phendrana, visors, with the GPU waited for at each EFB copy ([GPUT] lines; its frame rates are not valid) |
| r16h | F | pirates on the full-LTO image: the A/B for r16a |
| r16i | F | Main Plaza on the full-LTO image: the A/B for r16b |
| r16j | K | pirates on the 16384-word busy kick image: the A/B for r16a |
| r16k | K | Main Plaza on the 16384-word busy kick image: the A/B for r16b; then the dashboard |

Compare dlist + draw with hw15 (a cached list's draws moved from "draw" to
"dlist"). Launch `Prime-X-r16a`; pull the save folder whole.

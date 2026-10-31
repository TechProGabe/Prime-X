# hw17: 720p on gold

For the release (the user: "720p will be a big concern"). One profiler
image (ThinLTO + PGO, map `prime_x.hw17.map`), on **gold** (component
cable, 720p allowed; apps in `E:\Applications`). The ship build in
`Prime-X` for free play at 720p.

New since hw16: 720p runs 30 Hz ticks throughout (480 keeps Auto), the
vertex ring in the video library's idle framebuffer at 720p (+1.5 MB
free), the rain grids' box test (no clip for grids wholly inside, no draw
for grids off the screen).

| folder | video | scene |
|---|---|---|
| r17a | 720 | Main Plaza at 720p |
| r17b | 720 | pirates at 720p |
| r17c | 720 | Landing Site (rain) at 720p |
| r17d | 720 | Lava Lake at 720p |
| r17e | 720 | Phendrana, visors, at 720p |
| r17f | 480 | Main Plaza at 480p: the baseline for r17a; then the dashboard |

Look at `[AR]` (chunks swapped out and filled back), `[MEM] low` and
`[BEAT]` free against gold's earlier 720p play (`logs-gold-fix-pre`).
Launch `Prime-X-r17a`; pull the save folder whole.

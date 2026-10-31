# hw8: the prefetch and rain changes (staged 2026-10-29)

hw7's chain again, folder for folder (`Prime-X-r8a` .. `r8g`, the same
seven scenes and scripts), on profiler builds (`XBOX_LTO=1
XBOX_PGO=xbox/prime.profdata XBOX_CFLAGS="-DXHW_AUTOPAD=1 -DXHW_PROF=1"`)
of `dev` with:

- the skinned lists' array fetch prefetched 8 vertices ahead and big
  memcpys prefetching their next block (`0b0cc35`),
- the rain's streaks written without per-vertex memset/memcpy calls, and
  the line clipper's compaction as fixed-size copies,
- the audio start-up fix (`f73d0d7`, three clean chained starts in hw7),
- Port Options' value tables on their rows (`030553a`).

Compare each scene with hw7 (`C:\xemu\hw\prime\logs-hw7`, and
`tools/xbox/prof_report.py --last` for steady play): `GXCallDisplayList`, `memcpy`, `memset` and
`gx_vtx_flush` in the Landing Site and Main Plaza are what these change.
The Frigate isn't comparable between rounds (the script is by frame, and
how far it gets depends on the fps).

The plain `Prime-X` folder gets the ship build for free play. Launch
`Prime-X-r8a`; the chain ends at the dashboard.

# hw9: charge beam, Port Options grid, pause-screen budget (staged 2026-10-29)

hw7's and hw8's chain again, folder for folder (`Prime-X-r9a` .. `r9g`,
the same seven scenes and scripts), on profiler builds (`XBOX_LTO=1
XBOX_PGO=xbox/prime.profdata XBOX_CFLAGS="-DXHW_AUTOPAD=1 -DXHW_PROF=1"`)
of `dev` with, since hw8:

- the charge beam (`188fb44`: `PortAp::ChargeBeamRule` true without a seed),
- Port Options' value cells on a fixed grid, no dividers (`d2c68b6`),
- the dynamic lists' extra budget from 4 MB free (`a9ee175`; the pause
  screen's Samus ran 14-17 fps in hw8).

hw8's FPS changes (prefetch, rain) are in too; only r8g ran in hw8, so
r9a-r9f are their first measurement against hw7 (`C:\xemu\hw\prime\logs-hw7`,
`tools/xbox/prof_report.py --last` for steady play). The Frigate isn't
comparable between rounds (the script is by frame).

The plain `Prime-X` folder gets the ship build for free play: hold fire
for the charge beam, open the pause screen (fps), Port Options' rows.
Launch `Prime-X-r9a`; the chain ends at the dashboard.

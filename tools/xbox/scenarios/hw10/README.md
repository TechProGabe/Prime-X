# hw10: fights, the visors' palettes, the landing glow (staged 2026-10-30)

Ran on red the same day: the chain r10a-h, no free play. Results in
`docs/handoff.md` ("Console round hw10") and the roadmap's console history.

The first console round since hw9. Profiler builds (`XBOX_LTO=1
XBOX_PGO=xbox/prime.profdata XBOX_CFLAGS="-DXHW_AUTOPAD=1 -DXHW_PROF=1"`)
of `dev` for the chain, `Prime-X-r10a` .. `r10h`; the ship build
(`XBOX_LTO=1 XBOX_PGO=xbox/prime.profdata`) in the plain `Prime-X` folder.
In since hw9 (`docs/handoff.md`):

- fights: skinned lists past the dynamic budget drawn index-only,
  per-model skin workspaces (1 MB, given back when memory runs short),
  cheaper bone, frustum and collision code, [PERF]'s ticks per render;
- the pause screen's extra list budget from 4 MB free; the charge beam;
  Port Options' grid (fixed in hw9's build too, not yet looked at on red);
- audio start-up: no codec register reads on the console (r9f froze on
  them);
- rendering: black fog on additive blends (the landing glow's box), spot
  lights clamped as GX, the thermal and X-ray palettes (a DEPENDENT_GB
  texture unit: new on hardware), GX's intensity formula for copies;
- [PROFT]: the profiler's samples of frames of 150 ms or more, and how
  many of them waited.

| folder | scene | look for |
|---|---|---|
| r10a | front end, Port Options | fps against r9a; the options grid |
| r10b | landing cinematic, lockstep, shots at frames 400-520 every 4 | the gunship's glow: no box (xemu: frames ~448-452); the lightning-lit clouds fade, no hard edge |
| r10c | Landing Site, r9b's script | fps against r9b |
| r10d | Frigate Crash Site pirates, no damage | fights against hw9's 10-20 fps; [PERF] ticks per render |
| r10e | Main Plaza, r9c's script | fps against r9c |
| r10f | Phendrana, every item: combat, X-ray, thermal, scan, combat | thermal: the gun yellow-orange, door outlines red and blue; X-ray: pale lavender; any fault or hang here is the new texture unit |
| r10g | Magmoor, Lava Lake | fps; the lava without its fog volume (known) |
| r10h | Frigate opening, r9d's script | fps against r9d; the chain ends at the dashboard |

Launch `Prime-X-r10a`; each scene starts the next (`PX_NEXT_XBE`), so the
chain also tests the audio start-up eight times.

Free play: `Prime-X` (ship build), as before: a fight or two, the pause
screen (fps), pickups and doors (the 250-300 ms hitches). `Prime-X-prof`
is the profiler build with no script: it plays the same and its log has
[PROFT] for the hitches (use it for the pickups if there's time).

Back up `E:\UDATA\50580001` (saves, settings.ini) before uploading; pull
the logs with `tools/xbox/console.py pull hw10`. Maps:
`prime_x.hw10.map` (profiler: the chain and `Prime-X-prof`),
`prime_x.hw10ship.map` (free play in `Prime-X`).

# hw13: -O2 against -O3, and the small draw-side items

The compiler trial of `docs/fps-plan.md` ("Anytime: compiler trials"):
each heavy scene twice, back to back, on two images of the same code.
Image A (`r13a, c, e, g`) is the usual `-O2`; image B (`r13b, d, f, h`)
is `-O3` for the game and the SDK, the LTO backends too
(`XBOX_CMAKE_ARGS=-DPX_OPT_LEVEL=3`). Both ThinLTO + PGO profiler builds;
maps `prime_x.hw13.map` (A), `prime_x.hw13o3.map` (B). The ship build in
`Prime-X` is image A without the profiler.

New since hw12, in both images: draw merging (unchanged draws join the
open primitive), the minimap's positions in one call, the GPU-skin lists'
vertex-pool budget, models that keep falling back skinned on the CPU from
the start.

| folder | scene | image |
|---|---|---|
| r13a / r13b | pirates, Auto | A / B |
| r13c / r13d | Main Plaza, Auto | A / B |
| r13e / r13f | Lava Lake, Auto | A / B |
| r13g / r13h | Landing Site (rain), Auto | A / B; the chain ends at the dashboard |

Keep -O3 only if B beats A in the same rooms. Launch `Prime-X-r13a`;
pull the save folder whole.

# hw14: the first round on -O3 by default, and the pushbuffer CALL test

One ThinLTO + PGO profiler image (map `prime_x.hw14.map`), `-O3` (the
default since hw13). The ship build in `Prime-X` is the same code without
the profiler (`prime_x.hw14ship.map`).

New since hw13: GPU skinning's lists go to the texture pool when the vertex
pool is full (Main Plaza skinned everything on the CPU in hw13), the sorted
lists' coordinates kept in list order (`BuildNearList`), the interpolation
switch worked out once a frame, Game Timing always Auto (the Port Options
row is gone), and the pushbuffer CALL test (`PX_PB_CALL_TEST=1`).

| folder | scene | against |
|---|---|---|
| r14a | pirates, Auto | r13b (33.4 fps) |
| r14b | Main Plaza, Auto | r13d (36.7); `[DLC] GPU skin:` should show lists in the texture pool and few CPU skins |
| r14c | Lava Lake, Auto | r13f (41.6) |
| r14d | Landing Site (rain), Auto | r13h (36.7) |
| r14e | Phendrana, visors, Auto | r12g |
| r14f | front end with the CALL test; the chain ends at the dashboard | `[NV2A] call test: PASS` (FAIL or a stall ends M6a) |

The CALL test runs last so a hang there costs no measurement. Launch
`Prime-X-r14a`; pull the save folder whole.

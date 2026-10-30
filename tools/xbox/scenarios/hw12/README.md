# hw12: GPU skinning, the mixer, particles at 30 Hz

Milestones 4 and 2 of `docs/fps-plan.md`, on Game Timing: Auto
throughout. Profiler builds (`XBOX_LTO=1 XBOX_PGO=xbox/prime.profdata
XBOX_CFLAGS="-DXHW_AUTOPAD=1 -DXHW_PROF=1"`) of `dev` for the chain,
`Prime-X-r12a` .. `r12i`; the ship build in the plain `Prime-X` folder.
New since hw11:

- **GPU skinning**: characters (pirates, Samus on the pause screen, the
  arm cannon) are drawn from their bind pose with a matrix per bone; the
  CPU no longer skins them or copies their vertices every call. Reflection
  maps follow through per-bone texture matrices. `[DLC] GPU skin:` lines
  count it (and the CPU fallbacks, with the last reason);
- the mixer: the reverb's loop and the ADPCM decode cheaper (bit-exact);
- particles that step two frames in a 30 Hz tick are blended too;
- rain: each grid tested once against the view;
- [PROFR]: the profiler's samples of the game's own draw ([PERF]'s
  render), the biggest bucket in hw11.

| folder | scene | look for |
|---|---|---|
| r12a | front end, Port Options | the Game Timing row (Auto, Fixed, Flex) |
| r12b | pirates, Auto, GPU skinning | fps against r11i (25-26); dlist, draw |
| r12c | the same, GPU skinning off | the A/B: fps against r12b; any difference in the shots |
| r12d | Main Plaza, Auto | against r11j (37-39) |
| r12e | Landing Site, Auto | against r11k; the rain |
| r12f | Lava Lake, Auto | against r11l (34) |
| r12g | Phendrana, visors, Auto | against r11e; thermal and X-ray on skinned models |
| r12h | pause screen, Auto | against r11h (46.5); Samus in the inventory |
| r12i | Frigate opening, Auto | against r11g; the chain ends at the dashboard |

Launch `Prime-X-r12a`; each scene starts the next. Pull the save folder
whole. Maps: `prime_x.hw12.map` (profiler), `prime_x.hw12ship.map`.

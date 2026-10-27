# Renderer: Prime's GX on the NV2A

Melee-X's GX layer (`xbox/src/sdk/gx/`, `xbox/src/hw/nv2a*.c`) is the
starting point; its `docs/renderer.md` explains how it works (state and
dirty groups, the display-list cache, draw merging, generated vertex
programs, TEV to register combiners, indirect texturing, fog, native texture
formats, GPU EFB copies, Z textures). This file lists what Prime asks of it
and the state of each item. Fill in "status" as phase 3 goes.

Every item is checked against Dolphin: the same spot, the same visor,
`[FBDUMP]` shot vs a Dolphin frame dump, at 480p 4:3, 480p 16:9 and 720p.

| feature | game code | Melee-X has | work for Prime | status |
|---|---|---|---|---|
| world and actor geometry from big-endian display lists | `CCubeModel`, `CCubeSurface`, `CCubeMaterial` | BE display lists, DL vertex cache, merged draws | Prime's vertex formats and material flags | not started |
| up to 8 TEV stages, konst colours, swap tables | `CCubeMaterial`, `CGX` | TEV -> register combiners (8 stages) | check the longest Prime materials fit; log any that don't | not started |
| lightmaps, reflection (env) maps, punch-through, additive | `CCubeMaterial` | texgen in vertex programs | Prime's texgen and texture matrices (`CGX::SetTexMtx`) | not started |
| lighting: up to 8 lights, distance/angle attenuation | `CGraphics::SetLightState` | GX lighting in vertex programs | Prime's light setups; vertex-program length | not started |
| CPU-skinned models into host-endian arrays | `CSkinnedModel`, `CSkinRules` | `GXSetArray` on BE data | aurora's little-endian array flag and byte size; draw-sync tokens that free the workspace only when drawn; then GPU skinning (`docs/performance.md`) | not started |
| room fog, `GXSetFogRangeAdj` | `CGraphics`, `CStateManager` | `nv2a_fog.c` | compare ranges at 16:9 | not started |
| fog volumes | `CCubeRenderer::DrawFogVolume*`, `CFogVolume*` | Z textures | depth-based ramp on the NV2A | not started |
| EFB copies: camera filters and blur | `CCameraFilter`, `CCameraBlurPass`, `CPauseScreenBlur` | GPU EFB copies | sizes, formats (I8, RGB565, RGBA8), mip/scale | not started |
| EFB copies: shadows | `CWorldShadow`, `CProjectedShadow`, `CMorphBallShadow` | GPU EFB copies | render-to-texture regions | not started |
| EFB copies: transitions, space warp | `CWorldTransManager`, `CCubeRenderer` | | | not started |
| thermal visor | `CPlayerVisor`, `CCubeRenderer` (thermal hot/cold passes) | C4/C8 palettes, EFB copies | I8 EFB copy read through a palette (TLUT on a copied texture) | not started |
| X-ray visor | `CPlayerVisor`, `CCubeRenderer` | | X-ray draw passes and blur | not started |
| scan and combat visor, helmet, 3D HUD | `CHudDecoInterface*`, `CSamusHud`, `CGuiFrame` | | GUI frames render through the same GX; widescreen anchoring | not started |
| indirect texturing: heat, water, Phazon suit | `CFluidPlaneCPU`, `CCubeRenderer`, `CGX` | indirect texturing (`nv2a.c`) | Prime's indirect matrices and stages | not started |
| particles, swooshes, electric | `CElementGen`, `CParticleSwoosh`, `CParticleElectric` | immediate-mode merging | batching per emitter | not started |
| fluid surfaces | `CFluidPlaneCPU`, `CFluidPlaneDoor` | | CPU-built meshes, ripples | not started |
| movies | `CCubeMoviePlayer` (THP) | `thp.c` | YUV -> RGB setup as Prime does it (`MyTHPGXYuv2RgbSetup`) | not started |
| text and GUI | `CFont`, `CTextRenderBuffer`, `CGuiFrame` | | fonts from the disc's `main.dol` | not started |
| 16:9 hor+ at 480 and 720 | `CGraphics`, `CGameCamera`, `CPlayer` lock-on box | melee-pc's hor+ | MetroidPrimePort's aspect fixes and edge-to-edge HUD | not started |

## Build switches

Every renderer change gets a switch (`-DXGX_...`, 1 = new, 0 = old) listed
here and in `docs/testing.md`, as on Melee-X.

# Renderer: Prime's GX on the NV2A

Melee-X's GX layer (`xbox/src/sdk/gx/`, `xbox/src/hw/nv2a*.c`) is the
starting point; its `docs/renderer.md` explains how it works (state and
dirty groups, the display-list cache, draw merging, generated vertex
programs, TEV to register combiners, indirect texturing, fog, native texture
formats, GPU EFB copies, Z textures). This file lists what Prime asks of it
and the state of each item (2026-10-30).

Every item is checked against Dolphin: the same spot, the same visor,
`[FBDUMP]` shot vs a Dolphin frame dump (`tools/xbox/dolphin_ref.py`,
docs/testing.md), at 480p 4:3, 480p 16:9 and 720p. On 2026-10-30 the 14
room-sweep rooms were compared at 480p 4:3: they match but for the fog
volumes.

| feature | game code | Melee-X has | work for Prime | status |
|---|---|---|---|---|
| world and actor geometry from big-endian display lists | `CCubeModel`, `CCubeSurface`, `CCubeMaterial` | BE display lists, DL vertex cache, merged draws | Prime's vertex formats and material flags | works in every world (sweep); skinned and other changing lists go through dynamic display lists (a budget, index-only past it) |
| up to 8 TEV stages, konst colours, swap tables | `CCubeMaterial`, `CGX` | TEV -> register combiners (8 stages) | check the longest Prime materials fit; log any that don't | the NV2A has 4 texture units: a uniform texture (Prime's 8x8 incandescence and reflectivity maps) is read as a combiner constant, and a stage still left without a unit reads its texture's average colour (it read white: the arm cannon with its world shadow has five textures, and drew white in Chozo and Magmoor). Host test: `test_rc.py` "arm cannon + shadow" |
| lightmaps, reflection (env) maps, punch-through, additive | `CCubeMaterial` | texgen in vertex programs | Prime's texgen and texture matrices (`CGX::SetTexMtx`) | works; additive blends get black fog as retail's `CGX::update_fog` asks (`GXSetFogColor`, a no-op before 2026-10-30: the landing glow's light square) |
| lighting: up to 8 lights, distance/angle attenuation | `CGraphics::SetLightState` | GX lighting in vertex programs | Prime's light setups; vertex-program length | works; `CGX::SetNumChans` reaches GX since `x4c_flags` was laid out for x86 (the scene washed out after Morph Ball); spot lights clamp cos at 0 before the angle polynomial, as GX (2026-10-30: the lightning-lit clouds' hard edge). Host test: `test_vp_opt.py` |
| CPU-skinned models into host-endian arrays | `CSkinnedModel`, `CSkinRules` | `GXSetArray` on BE data | aurora's little-endian array flag and byte size; draw-sync tokens; then GPU skinning (`docs/performance.md`) | works; per-model skin workspaces, same-frame reuse; GPU skinning built (`GXPortSkinGpu`, fps-plan M4; on red since hw12) |
| room fog, `GXSetFogRangeAdj` | `CGraphics`, `CStateManager` | `nv2a_fog.c` | compare ranges at 16:9 | works (host test `test_fog.py`); the range adjustment isn't applied (planar depth) |
| fog volumes | `CCubeRenderer::DrawFogVolume*`, `CFogVolume*` | Z textures | depth-based ramp on the NV2A | left out (`PostRenderFogs`): the pass reads depth through Z16 copies as indirect offsets into a density ramp; ours are masks, and a volume drew as a solid box. Magmoor's lava haze is one (Dolphin's Lava Lake is dimmer). Needs the depth buffer read as a texture (handoff, Next 6) |
| EFB copies: camera filters and blur | `CCameraFilter`, `CCameraBlurPass`, `CPauseScreenBlur` | GPU EFB copies | sizes, formats (I8, RGB565, RGBA8), mip/scale | works on the GPU; intensity copies use GX's formula (66, 129, 25 / 256 + 16) since 2026-10-30 |
| EFB copies: shadows | `CWorldShadow`, `CProjectedShadow`, `CMorphBallShadow` | GPU EFB copies | render-to-texture regions | the gun's world shadow draws (COLOR1 lit by the shadow light); `GXSetDstAlpha` isn't implemented, so Morph Ball's shadow IDs in alpha are all one value |
| EFB copies: transitions, space warp | `CWorldTransManager`, `CCubeRenderer` | | | draw; not compared with Dolphin |
| thermal visor | `CPlayerVisor`, `CCubeRenderer` (thermal hot/cold passes) | C4/C8 palettes, EFB copies | I4 EFB copy read through a palette (TLUT on a copied texture) | works, matches Dolphin (2026-10-30): a CI view of an EFB copy carries its TLUT as a small texture (`XgxMap.dep`) a DEPENDENT_GB unit looks the copy up in; not yet run on a console |
| X-ray visor | `CPlayerVisor`, `CCameraBlurFilter` | | X-ray draw passes and blur | draws through `TXTR_XRayPalette` (inverted: dim rooms go pale lavender) since 2026-10-30; not compared (Dolphin can't hold X-ray without input) |
| scan and combat visor, helmet, 3D HUD | `CHudDecoInterface*`, `CSamusHud`, `CGuiFrame` | | GUI frames render through the same GX; widescreen anchoring | works; matches Dolphin in the sweep rooms |
| indirect texturing: heat, water, Phazon suit | `CFluidPlaneCPU`, `CCubeRenderer`, `CGX` | indirect texturing (`nv2a.c`) | Prime's indirect matrices and stages | BUMPENVMAP for indirect maps up to 64K texels (`ind_tex`); stages it can't do exactly draw direct. Water and the Phazon suit not compared |
| particles, swooshes, electric | `CElementGen`, `CParticleSwoosh`, `CParticleElectric` | immediate-mode merging | batching per emitter | works; matches Dolphin in the sweep rooms |
| fluid surfaces | `CFluidPlaneCPU`, `CFluidPlaneDoor` | | CPU-built meshes, ripples | draw (lava, water, acid); Lava Lake matches but for its fog volume |
| movies | `CCubeMoviePlayer` (THP) | `thp.c` | YUV -> RGB setup as Prime does it (`MyTHPGXYuv2RgbSetup`) | works (title, front end, attract) |
| text and GUI | `CFont`, `CTextRenderBuffer`, `CGuiFrame` | | fonts from the disc's `main.dol` | works |
| 16:9 hor+ at 480 and 720 | `CGraphics`, `CGameCamera`, `CPlayer` lock-on box | melee-pc's hor+ | MetroidPrimePort's aspect fixes and edge-to-edge HUD | Port Options' Widescreen (4:3, as the dashboard, 16:9); 4:3, 16:9 and 720p letterboxing work |

## Build switches

Every renderer change gets a switch (`-DXGX_...`, 1 = new, 0 = old) listed
here and in `docs/testing.md`, as on Melee-X.

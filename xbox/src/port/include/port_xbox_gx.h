// port_xbox_gx.h - GX-layer helpers the game code calls on the Xbox
// (xbox/src/sdk/gx), for work the GameCube did in hardware for free.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// 1 when the box bmin..bmax (model space, through the current position
// matrix and projection) lies wholly outside the view: what it would draw
// is clipped away anyway (gx_cull.h's test, as for cached display lists).
int GXPortBoxCulled(const float bmin[3], const float bmax[3]);

// The rain's n streaks into the open GX_LINES batch: particle i (xyz[3i..])
// from its position, colour c0, to z + dz, colour c1 (RGBA4), as two
// GXPosition3s16 + GXTexCoord1s16 pairs would. 0: the batch isn't that
// layout and nothing was written (make the calls).
// n vertices of one 8-bit position index each (idx[]) into the open batch,
// as n GXPosition1x8 calls would. 0: the batch isn't exactly that (make the
// calls).
int GXPortPositions1x8(const unsigned char* idx, unsigned n);
/* Memory the game promises not to write while registered (a room's static
 * geometry from the disc: CCubeRenderer::AddStaticGeometry): cached
 * display lists that read only such memory skip their content checks.
 * Remove drops the lists cached from it, before the memory is freed. */
void GXPortStaticAdd(const void* key, const void* lo, unsigned bytes);
void GXPortStaticRemove(const void* key);

int GXPortRainLines(const short* xyz, unsigned n, short dz, unsigned short c0, unsigned short c1);
// The same streaks (n particles, as GXPortRainLines takes them, with each
// z the stored height before the grid's fall zoff: (z + zoff) & 0x3fff is
// the game's) from a vertex buffer kept per owner (the grid): written again
// only when gen changes (a new set of particles, or fewer), the new ones
// when n grows. The fall and the clip happen on the GPU. 0: not drawn this
// way (draw them as before).
int GXPortRainGrid(const void* owner, unsigned gen, const short* xyz, unsigned n, short zoff, short dz,
                   unsigned short c0, unsigned short c1);

// The snow's (or the underwater flakes') n quads into the open GX_QUADS
// batch, last particle first: corners p, p + up, p + up + right, p + right
// (short sums) with texture corners (0,0) (0,2) (2,2) (2,0), as four
// GXPosition3s16 + GXTexCoord2u8 pairs a flake would. 0: the batch isn't
// that layout and nothing was written (make the calls).
int GXPortSnowQuads(const short* xyz, unsigned n, const short up[3], const short right[3]);

// n vertices of position (3 floats each in xyz), one RGBA8 colour (as
// GXColor1u32 takes it) and a texture coordinate (2 floats each in uv)
// into the open batch, as n GXPosition3f32 + GXColor1u32 + GXTexCoord2f32
// triples would (the particles' quads). 0: the batch isn't exactly that
// layout and nothing was written (make the calls).
int GXPortVertices3fC2f(const float* xyz, unsigned color, const float* uv, unsigned n);
// The same for the untextured quads, their texture coordinates the 16 bits
// each GXTexCoord1s16 call would write (two S8s): 0 as above.
int GXPortVertices3fC1s16(const float* xyz, unsigned color, const unsigned short* st, unsigned n);

// The CPU skinning wrote these buffers (positions, normals; either may be
// null): a display list drawn from them again in the same frame may reuse
// the vertices it sent before only while they haven't been written since.
void GXPortSkinWritten(const void* verts, const void* normals);
// Written, and only the skinning writes this memory, but the buffers are
// handed out again after the frame (the shared skinning ring): kept track
// of for this frame only.
void GXPortSkinWrittenThisFrame(const void* verts, const void* normals);
// The buffers are freed: forget them (their memory may become anything).
void GXPortSkinForget(const void* verts, const void* normals);
// A skinning workspace of a model's own (32-byte aligned), in place of the
// shared ring's; null: none (over budget or memory short): use the ring.
void* GXPortSkinKeep(unsigned bytes);
// Give it back (and forget it, as GXPortSkinForget).
void GXPortSkinUnkeep(void* verts, const void* normals, unsigned bytes);
// Changes when memory runs short: a workspace kept under another epoch is to
// be given back (GXPortSkinUnkeep) at its model's next skin.
unsigned GXPortSkinKeepEpoch(void);

// Skinning on the GPU (docs/fps-plan.md M4): 1 when it may be asked
// (-DXGX_GPU_SKIN, test builds' env PX_GPU_SKIN=0|1).
int GXPortSkinGpuOn(void);
// The skin of workspace verts/normals left to the GPU: npts points from the
// model's bind-pose arrays src_pos/src_nrm (big-endian floats), nbones
// virtual bones of counts[b] consecutive points each, their 3x4 transforms
// (xf12, row-major) and 3x3 normal rotations (rot9). 1: taken (don't skin
// on the CPU; the GX layer skins there itself if a draw needs the memory);
// 0: skin on the CPU as before. transient: the shared skinning ring.
int GXPortSkinGpu(const void* verts, const void* normals, const void* src_pos, const void* src_nrm, unsigned npts,
                  unsigned nbones, const int* counts, const float* xf12, const float* rot9, int transient);

#ifdef __cplusplus
}
#endif

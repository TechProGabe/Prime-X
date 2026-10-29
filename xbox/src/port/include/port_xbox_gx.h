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
int GXPortRainLines(const short* xyz, unsigned n, short dz, unsigned short c0, unsigned short c1);

// The snow's (or the underwater flakes') n quads into the open GX_QUADS
// batch, last particle first: corners p, p + up, p + up + right, p + right
// (short sums) with texture corners (0,0) (0,2) (2,2) (2,0), as four
// GXPosition3s16 + GXTexCoord2u8 pairs a flake would. 0: the batch isn't
// that layout and nothing was written (make the calls).
int GXPortSnowQuads(const short* xyz, unsigned n, const short up[3], const short right[3]);

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

#ifdef __cplusplus
}
#endif

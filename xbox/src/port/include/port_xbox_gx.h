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

#ifdef __cplusplus
}
#endif

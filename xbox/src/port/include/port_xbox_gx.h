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

#ifdef __cplusplus
}
#endif

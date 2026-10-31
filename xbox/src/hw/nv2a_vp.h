/* nv2a_vp.h - NV2A vertex programs generated from GX transform state.
 * Constant rows (hardware index) used by every generated program: */
#ifndef NV2A_VP_H
#define NV2A_VP_H
#include <stdint.h>

#define VPC_PROJ 0       /* 4 rows: projection with viewport + content rect folded in */
#define VPC_K 4          /* (0, 1, 0.5, 2) */
#define VPC_POS 6        /* 10 position matrices x 3 rows, addressed by a0 = PNMTXIDX */
#define VPC_NRM 36       /* 10 normal matrices x 3 rows, same addressing */
#define VPC_CHAN 66      /* mat0, amb0, mat1, amb1 (0..1 floats) */
#define VPC_LIGHT 70     /* 8 lights x 5 rows: pos, dir, colour, a, k */
#define VPC_TEXGEN 110   /* 4 units x 3 rows (s, t, q), post matrix folded in */
#define VPC_POSTMTX 122  /* 4 units x 3 rows: post-transform after normalize */
#define VPC_FOG 134      /* 3 rows: fog numerator, denominator, curve (nv2a_fog.h) */
#define VPC_TEXMTX 137   /* 10 texture matrices x 3 rows (GX TEXMTX0..9), addressed by a0 = PNMTXIDX */
#define VPC_RAIN 167     /* 14 rows, VpKey.rain's draws (xgx_rain_next): */
#define VPC_COUNT 181
/* VpKey.rain: a draw of the rain's streaks (gx_vtx.c GXPortRainGrid). Each
 * vertex is one end of a vertical streak: position (x, y, z) with x and y
 * scaled and z the stored 8.8 height (0..0x3FFF), colour0.x 0 for the
 * streak's start, 1 for its end. The program adds the grid's fall
 * (z = (z + D) mod 0x4000, as CEnvFxManager::UpdateRainParticles' sums),
 * puts the end dz above the start, and clips the streak as gx_vtx.c's
 * clip_lines does on the CPU (the near plane and the guard band,
 * Liang-Barsky): both ends see the same streak, so each works out its own
 * clipped end, and a streak wholly outside goes to a point off the screen.
 * The rows, from VPC_RAIN (object space: the grid's matrix folded in): */
#define VPR_GUARD 0      /* 4 rows: the guard band's planes, 2 w + x, 2 w - x, 2 w + y, 2 w - y */
#define VPR_NEAR 4       /* the near plane, cz + cw */
#define VPR_DG 5         /* each guard plane's change from start to end (plane.z * dz) */
#define VPR_DN 6         /* the near plane's in x, 0 0 0 */
#define VPR_IG 7         /* -1 / VPR_DG per lane, 0 where that is 0 */
#define VPR_IN 8         /* -1 / VPR_DN.x, 0 0 0 */
#define VPR_Z 9          /* D / 0x4000, 1 / 0x4000, 0x4000 x scale, 0 */
#define VPR_DZ 10        /* dz x scale, 0 0 0 */
#define VPR_C0 11        /* the start's colour (0..1) */
#define VPR_DC 12        /* the end's colour minus the start's */
#define VPR_OFF 13       /* where a dropped streak goes: clip (-64, -64, 0, 1), off the screen */
#define VPR_ROWS 14

/* vertex inputs */
#define VPI_POS 0
#define VPI_MTX 1
#define VPI_NRM 2
#define VPI_COL0 3
#define VPI_COL1 4
/* GX TEX0..7 -> v9..v15, v8 */
static inline int vpi_tex(int n) { return n < 7 ? 9 + n : 8; }

/* light attenuation variants the generator knows */
enum { VPL_OFF = 0, VPL_DIFFUSE, VPL_SPOT, VPL_SPEC };

/* GX fog curves the generator writes oFog for (nv2a_fog.h) */
enum { VPF_OFF = 0, VPF_LIN, VPF_EXP, VPF_EXP2 };

typedef struct {
    uint8_t enable, amb_vtx, mat_vtx;
    uint8_t diff_fn;              /* GXDiffuseFn */
    uint8_t attn;                 /* VPL_* for every light in the mask */
    uint8_t light_mask;
} VpChan;

typedef struct {
    uint8_t src;                  /* GXTexGenSrc */
    uint8_t proj;                 /* MTX3x4: q is used; VP_PROJ_DIVIDE: and s, t divided by it here */
    uint8_t normalize;            /* then post matrix */
    uint8_t pv_mtx;               /* the matrix per vertex: c[A0 + VPC_TEXMTX] (TEXnMTXIDX), then post matrix */
} VpTexGen;

#define VP_PROJ_DIVIDE 2

/* Everything the program's code depends on; constants are separate. */
typedef struct {
    uint8_t has_nrm;
    uint8_t nchans;               /* 0..2 */
    VpChan chan[4];               /* COLOR0 ALPHA0 COLOR1 ALPHA1 */
    uint8_t ntex;                 /* NV2A texture units fed, 0..4 */
    VpTexGen tex[4];
    uint8_t copy;                 /* EFB copy pass (nv2a.c): position and texcoord 0 as given */
    uint8_t fog;                  /* VPF_*: GX fog amount -> oFog.x */
    /* lights whose spot attenuation is the identity in a SPOT channel:
     * angle a = (1, 0, 0) (infinite and point lights), distance k = (1, 0, 0)
     * (infinite lights); their factor is then 1 exactly, so it is left out */
    uint8_t ang_one, dist_one;
    uint8_t rain;                 /* the rain's streaks (VPC_RAIN above) */
} VpKey;

#define VP_MAX_INSNS 136

typedef struct {
    uint32_t n;                   /* instructions */
    uint32_t words[VP_MAX_INSNS * 4];
    int approximated;
} VpProgram;

void vp_generate(const VpKey* key, VpProgram* out);
/* Clears the fields of a key its program doesn't depend on (the second
 * channel's when there is one channel, a disabled half's lighting, ...), so
 * configurations that differ only there share one program. */
void vp_canon(VpKey* key);

#endif

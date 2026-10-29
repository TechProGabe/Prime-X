/* gx_vtx.c - GX vertices -> the canonical vertex layout (xgx.h).
 *
 * Two sources:
 *  - immediate mode (GXBegin, GXPosition*, ..., GXEnd): native values, one
 *    call per attribute in GX attribute order;
 *  - display lists (GXCallDisplayList): the big-endian GX command stream HSD
 *    loads from the disc, with indexed attributes read from GXSetArray arrays
 *    (big-endian disc data, or native when the game built them).
 * Both decode each attribute by the current vertex descriptor and the VAT
 * of the batch's vertex format, into a vertex slot the back end handed out. */
#include <stdlib.h>
#include <string.h>

#include "gx_cull.h"
#include "gx_internal.h"
#include "xgx_probe.h"
#include "xhw.h"

#ifndef XGX_STATS_EVERY
#define XGX_STATS_EVERY 600
#endif

/* GX attribute order within a vertex */
static const uint8_t k_order[] = {
    GX_VA_PNMTXIDX, GX_VA_TEX0MTXIDX, GX_VA_TEX1MTXIDX, GX_VA_TEX2MTXIDX, GX_VA_TEX3MTXIDX,
    GX_VA_TEX4MTXIDX, GX_VA_TEX5MTXIDX, GX_VA_TEX6MTXIDX, GX_VA_TEX7MTXIDX, GX_VA_POS,
    GX_VA_NRM, GX_VA_CLR0, GX_VA_CLR1, GX_VA_TEX0, GX_VA_TEX1, GX_VA_TEX2, GX_VA_TEX3,
    GX_VA_TEX4, GX_VA_TEX5, GX_VA_TEX6, GX_VA_TEX7,
};
#define N_ORDER (int)(sizeof k_order / sizeof k_order[0])

typedef struct {
    uint8_t attr;       /* GX_VA_* (GX_VA_NRM also stands for NBT) */
    uint8_t type;       /* GX_DIRECT / INDEX8 / INDEX16 */
    uint8_t kind;       /* SK_*: store()'s fast path for this format (plan_kinds) */
    uint8_t dsize;      /* bytes of its data (direct in a list, or an array entry) */
    int32_t dst;        /* byte offset in the canonical vertex, -1: consumed only */
    float scale;        /* fixed-point scale of an S16/U16/S8 kind */
    GxAttrFmt fmt;
} Slot;

/* store()'s common formats, decided once per plan: the display-list decoder
 * spent most of its time in the generic per-element switches (Prime's
 * skinned models, ~90 lists a frame on the Landing Site, are decoded on
 * every call). Anything else goes through store(). */
enum { SK_GENERIC = 0, SK_NONE, SK_F32_3, SK_F32_2, SK_S16_3, SK_S16_2, SK_U16_2, SK_S8_3, SK_RGBA8, SK_MTX,
       SK_NRM_F32, SK_NRM_S16 };

/* Normals packed as the NV2A's CMP (D3D's NORMPACKED3: x and y in 11
 * signed bits, z in 10), 4 bytes instead of 12: the cached display lists'
 * vertices ~20% smaller, so more of a room's lists fit the vertex pool
 * (Main Plaza filled it). The vertex program reads them as floats. */
#define NRM_BYTES (XGX_NRM_CMP ? 4 : 12)

static inline int cmp_q(float v, float m) {
    v = v < -1.0f ? -1.0f : v > 1.0f ? 1.0f : v;
    return (int)(v * m + (v >= 0.0f ? 0.5f : -0.5f));
}

/* three floats -> the vertex's normal */
static inline void put_nrm(uint8_t* dst, const float* n) {
#if XGX_NRM_CMP
    uint32_t w = ((uint32_t)cmp_q(n[0], 1023.0f) & 0x7FFu) | ((uint32_t)cmp_q(n[1], 1023.0f) & 0x7FFu) << 11 |
                 ((uint32_t)cmp_q(n[2], 511.0f) & 0x3FFu) << 22;
    memcpy(dst, &w, 4);
#else
    memcpy(dst, n, 12);
#endif
}

typedef struct {
    Slot slot[N_ORDER];
    int n;
    XgxLayout layout;
} Plan;

/* ---- the batch being built ---- */
static struct {
    int open;
    int pending;         /* finished, not drawn: the next batch may join it */
    uint32_t prim;
    int vtxfmt;
    uint32_t expected, done;
    Plan plan;
    uint8_t* base;       /* back-end vertex memory */
    uint8_t* cur;        /* current vertex */
    int cursor;          /* next slot within the vertex */
    float pos_carry[3];   /* position components not yet a whole position */
    int npos_carry;
    float tc_carry[2];    /* GXTexCoord1f32 components not yet a whole ST pair */
    int ntc_carry;
    uint32_t seg_prim;    /* a strip or fan being built to become a list (seg_list) */
    uint32_t seg_start;   /* where its vertices start (after a waiting list it joined) */
} B;

static void sig_recompute(void);
static void plan_kinds(Plan* p, int vtxfmt);
void gx_vtx_reset(void) {
    memset(&B, 0, sizeof B);
    sig_recompute();
}

static void make_plan(Plan* p, int vtxfmt) {
    int i, off = 12;
    XgxLayout* l = &p->layout;
    uint8_t nrm_type = g_gx.desc[GX_VA_NRM] != GX_NONE ? g_gx.desc[GX_VA_NRM] : g_gx.desc[GX_VA_NBT];
    memset(l, 0xFF, sizeof *l);   /* every offset -1 */
    l->off_pos = 0;
    l->tc_mtx = 0;
    p->n = 0;
    for (i = 0; i < N_ORDER; i++) {
        uint8_t a = k_order[i], type = a == GX_VA_NRM ? nrm_type : g_gx.desc[a];
        Slot* s;
        if (type == GX_NONE) continue;
        s = &p->slot[p->n++];
        s->attr = a;
        s->type = type;
        s->fmt = g_gx.vat[vtxfmt][a == GX_VA_NRM && g_gx.desc[GX_VA_NRM] == GX_NONE ? GX_VA_NBT : a];
        s->dst = -1;
        switch (a) {
            case GX_VA_PNMTXIDX: s->dst = l->off_mtx = off; off += 4; break;
            case GX_VA_POS: s->dst = 0; break;
            case GX_VA_NRM: s->dst = l->off_nrm = off; off += NRM_BYTES; break;
            case GX_VA_CLR0: s->dst = l->off_col[0] = off; off += 4; break;
            case GX_VA_CLR1: s->dst = l->off_col[1] = off; off += 4; break;
            default:
                if (a >= GX_VA_TEX0 && a <= GX_VA_TEX7) {
                    s->dst = l->off_tc[a - GX_VA_TEX0] = off;
                    off += 8;
                } else if (a >= GX_VA_TEX0MTXIDX && a <= GX_VA_TEX7MTXIDX) {
                    l->tc_mtx |= 1u << (a - GX_VA_TEX0MTXIDX);   /* consumed, flagged (XgxLayout) */
                }
                break;
        }
    }
    l->stride = (uint32_t)off;
    plan_kinds(p, vtxfmt);
}

/* ---- element decoding ---- */
static int comp_count(const Slot* s) {
    switch (s->attr) {
        case GX_VA_POS: return s->fmt.cnt == GX_POS_XY ? 2 : 3;
        case GX_VA_NRM: return s->fmt.cnt == GX_NRM_XYZ ? 3 : 9;
        default: return s->fmt.cnt == GX_TEX_S ? 1 : 2;
    }
}

static int elem_size(uint8_t type) {
    switch (type) {
        case GX_U8: case GX_S8: return 1;
        case GX_U16: case GX_S16: return 2;
        default: return 4;
    }
}

static int color_size(uint8_t type) {
    switch (type) {
        case GX_RGB565: case GX_RGBA4: return 2;
        case GX_RGB8: case GX_RGBA6: return 3;
        default: return 4;
    }
}

/* bytes one attribute's data occupies (direct in a DL, or one array entry) */
static int data_size(const Slot* s) {
    if (s->attr <= GX_VA_TEX7MTXIDX) return 1;
    if (s->attr == GX_VA_CLR0 || s->attr == GX_VA_CLR1) return color_size(s->fmt.type);
    return comp_count(s) * elem_size(s->fmt.type);
}

static void plan_kinds(Plan* p, int vtxfmt) {
    int i;
    (void)vtxfmt;
    for (i = 0; i < p->n; i++) {
        Slot* s = &p->slot[i];
        uint8_t t = s->fmt.type, k = SK_GENERIC;
        s->dsize = (uint8_t)data_size(s);
        s->scale = 1.0f;
        if (s->dst < 0) {
            k = SK_NONE;
        } else if (s->attr == GX_VA_PNMTXIDX) {
            k = SK_MTX;
        } else if (s->attr == GX_VA_CLR0 || s->attr == GX_VA_CLR1) {
            if (t == GX_RGBA8) k = SK_RGBA8;
        } else if (s->attr == GX_VA_POS) {
            if (s->fmt.cnt == GX_POS_XYZ) {
                s->scale = 1.0f / (float)(1u << (s->fmt.frac & 31));
                k = t == GX_F32 ? SK_F32_3 : t == GX_S16 ? SK_S16_3 : t == GX_S8 ? SK_S8_3 : SK_GENERIC;
            }
        } else if (s->attr == GX_VA_NRM) {
            if (s->fmt.cnt == GX_NRM_XYZ) {   /* fixed fractions: S8 1.6, S16 1.14 */
                if (XGX_NRM_CMP) k = t == GX_F32 ? SK_NRM_F32 : t == GX_S16 ? SK_NRM_S16 : SK_GENERIC;
                else k = t == GX_F32 ? SK_F32_3 : t == GX_S16 ? SK_S16_3 : t == GX_S8 ? SK_S8_3 : SK_GENERIC;
                s->scale = t == GX_S8 ? 1.0f / 64 : 1.0f / 16384;
            }
        } else if (s->attr >= GX_VA_TEX0 && s->attr <= GX_VA_TEX7) {
            if (s->fmt.cnt == GX_TEX_ST) {
                s->scale = 1.0f / (float)(1u << (s->fmt.frac & 31));
                k = t == GX_F32 ? SK_F32_2 : t == GX_S16 ? SK_S16_2 : t == GX_U16 ? SK_U16_2 : SK_GENERIC;
            }
        }
        s->kind = k;
    }
}

static float read_elem(const uint8_t* p, uint8_t type, float scale, int be) {
    switch (type) {
        case GX_U8: return p[0] * scale;
        case GX_S8: return (int8_t)p[0] * scale;
        case GX_U16: return (be ? gx_be16(p) : *(const uint16_t*)p) * scale;
        case GX_S16: return (int16_t)(be ? gx_be16(p) : *(const uint16_t*)p) * scale;
        default: {
            float f;
            if (be) return gx_bef(p);
            memcpy(&f, p, 4);
            return f;
        }
    }
}

static void read_color(const uint8_t* p, uint8_t type, int be, uint8_t* out) {
    uint32_t v;
    switch (type) {
        case GX_RGB565:
            v = be ? gx_be16(p) : *(const uint16_t*)p;
            out[0] = (uint8_t)(((v >> 11) & 31) * 255 / 31);
            out[1] = (uint8_t)(((v >> 5) & 63) * 255 / 63);
            out[2] = (uint8_t)((v & 31) * 255 / 31);
            out[3] = 255;
            break;
        case GX_RGB8:
            out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = 255;
            break;
        case GX_RGBX8:
            out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = 255;
            break;
        case GX_RGBA4:
            v = be ? gx_be16(p) : *(const uint16_t*)p;
            out[0] = (uint8_t)(((v >> 12) & 15) * 17);
            out[1] = (uint8_t)(((v >> 8) & 15) * 17);
            out[2] = (uint8_t)(((v >> 4) & 15) * 17);
            out[3] = (uint8_t)((v & 15) * 17);
            break;
        case GX_RGBA6:
            v = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
            out[0] = (uint8_t)(((v >> 18) & 63) * 255 / 63);
            out[1] = (uint8_t)(((v >> 12) & 63) * 255 / 63);
            out[2] = (uint8_t)(((v >> 6) & 63) * 255 / 63);
            out[3] = (uint8_t)((v & 63) * 255 / 63);
            break;
        default: /* RGBA8 */
            out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = p[3];
            break;
    }
}

/* one attribute's data at p (direct data or an array entry) -> vertex */
static void store(const Slot* s, const uint8_t* p, int be, uint8_t* v) {
    if (s->dst < 0) return;
    if (s->attr == GX_VA_PNMTXIDX) {
        float f = (float)p[0];
        memcpy(v + s->dst, &f, 4);
    } else if (s->attr == GX_VA_CLR0 || s->attr == GX_VA_CLR1) {
        read_color(p, s->fmt.type, be, v + s->dst);
    } else {
        float out[3] = { 0, 0, 0 };
        int n = comp_count(s), i, es = elem_size(s->fmt.type);
        float scale;
        if (s->attr == GX_VA_NRM) {
            /* normals have fixed fractions: S8 1.6, S16 1.14 */
            scale = s->fmt.type == GX_S8 || s->fmt.type == GX_U8 ? 1.0f / 64 : 1.0f / 16384;
            if (n > 3) n = 3;   /* NBT: the normal comes first */
        } else {
            scale = 1.0f / (float)(1u << s->fmt.frac);
        }
        for (i = 0; i < n && i < 3; i++) out[i] = read_elem(p + i * es, s->fmt.type, scale, be);
        /* constant sizes: inline moves (xbuiltin.h) */
        if (s->attr == GX_VA_NRM) put_nrm(v + s->dst, out);
        else if (s->attr == GX_VA_POS) memcpy(v + s->dst, out, 12);
        else memcpy(v + s->dst, out, 8);
    }
}

/* store() for the plan's common kinds; the same values store() makes */
static inline void store_fast(const Slot* s, const uint8_t* p, int be, uint8_t* v) {
    float* o;
    int i;
    switch (s->kind) {
        case SK_NONE: return;
        case SK_F32_3:
            o = (float*)(v + s->dst);
            if (be) { o[0] = gx_bef(p); o[1] = gx_bef(p + 4); o[2] = gx_bef(p + 8); }
            else memcpy(o, p, 12);
            return;
        case SK_F32_2:
            o = (float*)(v + s->dst);
            if (be) { o[0] = gx_bef(p); o[1] = gx_bef(p + 4); }
            else memcpy(o, p, 8);
            return;
        case SK_S16_3:
            o = (float*)(v + s->dst);
            for (i = 0; i < 3; i++) o[i] = (int16_t)(be ? gx_be16(p + 2 * i) : *(const uint16_t*)(p + 2 * i)) * s->scale;
            return;
        case SK_S16_2:
            o = (float*)(v + s->dst);
            for (i = 0; i < 2; i++) o[i] = (int16_t)(be ? gx_be16(p + 2 * i) : *(const uint16_t*)(p + 2 * i)) * s->scale;
            return;
        case SK_U16_2:
            o = (float*)(v + s->dst);
            for (i = 0; i < 2; i++) o[i] = (be ? gx_be16(p + 2 * i) : *(const uint16_t*)(p + 2 * i)) * s->scale;
            return;
        case SK_S8_3:
            o = (float*)(v + s->dst);
            for (i = 0; i < 3; i++) o[i] = (int8_t)p[i] * s->scale;
            return;
        case SK_RGBA8:
            memcpy(v + s->dst, p, 4);
            return;
        case SK_NRM_F32: {
            float n[3];
            if (be) { n[0] = gx_bef(p); n[1] = gx_bef(p + 4); n[2] = gx_bef(p + 8); }
            else memcpy(n, p, 12);
            put_nrm(v + s->dst, n);
            return;
        }
        case SK_NRM_S16: {
            float n[3];
            for (i = 0; i < 3; i++) n[i] = (int16_t)(be ? gx_be16(p + 2 * i) : *(const uint16_t*)(p + 2 * i)) * s->scale;
            put_nrm(v + s->dst, n);
            return;
        }
        case SK_MTX: {
            float f = (float)p[0];
            memcpy(v + s->dst, &f, 4);
            return;
        }
        default:
            store(s, p, be, v);
            return;
    }
}

static void fetch_indexed(const Slot* s, uint32_t idx, uint8_t* v) {
    uint8_t a = s->attr == GX_VA_NRM && g_gx.desc[GX_VA_NRM] == GX_NONE ? GX_VA_NBT : s->attr;
    const uint8_t* base = g_gx.array[a];
    if (!base) return;
    store(s, base + idx * g_gx.array_stride[a], !g_gx.array_le[a], v);
}

/* ---- primitives ----
 * Quads and fans go to the back end as triangle lists. xemu on macOS draws
 * NV2A quads through a geometry shader, which that GL runs as a compute pass
 * ahead of the draw, ending the render pass: each quad or fan draw became a
 * render pass of its own. */
static uint32_t out_prim(uint32_t prim) { return prim == XGX_QUADS || prim == XGX_TRIFAN ? XGX_TRIANGLES : prim; }

/* a joined draw stays within one of xgx_draw's 32768-vertex index windows */
#define JOIN_MAX 0x8000u

static uint32_t out_count(uint32_t prim, uint32_t n) {
    if (prim == XGX_QUADS) return n / 4 * 6;
    if (prim == XGX_TRIFAN) return n >= 3 ? (n - 2) * 3 : 0;
    return n;
}

/* n vertices of `prim` at src, stride s, as out_prim(prim) at dst; returns
 * the bytes written */
static uint32_t copy_prim(uint8_t* dst, const uint8_t* src, uint32_t prim, uint32_t n, uint32_t s) {
    uint8_t* d = dst;
    uint32_t i;
    if (prim == XGX_QUADS) {
        for (i = 0; i + 4 <= n; i += 4, src += 4 * s) {   /* 0 1 2, 0 2 3 */
            memcpy(d, src, 3 * s);
            memcpy(d + 3 * s, src, s);
            memcpy(d + 4 * s, src + 2 * s, 2 * s);
            d += 6 * s;
        }
    } else if (prim == XGX_TRIFAN) {
        for (i = 1; i + 1 < n; i++) {                   /* 0 i i+1 */
            memcpy(d, src, s);
            memcpy(d + s, src + i * s, 2 * s);
            d += 3 * s;
        }
    } else {
        memcpy(d, src, n * s);
        d += n * s;
    }
    return (uint32_t)(d - dst);
}

/* ---- batches ----
 * Immediate-mode vertices are built in cached memory (the vertex ring is
 * write-combined, and they arrive an attribute at a time) and copied to the
 * ring when drawn. A finished batch of a list primitive (quads, triangles,
 * lines, points) isn't drawn at GXEnd: it waits, and a next GXBegin of the
 * same primitive and vertex layout with no state change in between (every
 * state call that changes something flushes first) continues it. HSD's
 * particles, text and HUD pieces come as runs of small batches; the cost of
 * a draw hardly depends on its size, least of all in xemu. */
#define STAGE_MAX (2u * 1024 * 1024)
static uint8_t* s_stage;
static uint32_t s_stage_cap;
static uint32_t s_st_imm_batches, s_st_imm_joined;
/* batches that ended with fewer vertices than GXBegin announced: writes
 * that didn't fit the vertex descriptor (issue #7: every particle) */
static uint32_t s_st_imm_short;

/* room for `bytes` in the staging buffer (its contents kept); 0: too big */
static int stage_reserve(uint32_t bytes) {
    uint32_t cap = s_stage_cap ? s_stage_cap : 64 * 1024;
    uint8_t* p;
    if (bytes <= s_stage_cap) return 1;
    if (bytes > STAGE_MAX) return 0;
    while (cap < bytes) cap *= 2;
    if (cap > STAGE_MAX) cap = STAGE_MAX;
    if (!(p = (uint8_t*)realloc(s_stage, cap))) return 0;
    s_stage = p;
    s_stage_cap = cap;
    return 1;
}

static int list_prim(uint32_t prim, uint32_t n) {
    switch (prim) {
        case XGX_QUADS: return n % 4 == 0;
        case XGX_TRIANGLES: return n % 3 == 0;
        case XGX_LINES: return n % 2 == 0;
        case XGX_POINTS: return 1;
        default: return 0;
    }
}

/* Immediate lines against the view volume, on the CPU. The vertex program
 * divides by w itself (nv2a_vp.c), so a line with an end behind the camera
 * came out flipped across the screen, and one with an end just in front of
 * it hundreds of thousands of pixels long. Prime's rain
 * (CEnvFxManagerGrid::RenderRainParticles: GX_LINES in a grid around the
 * camera) has both every frame. GX clips to the near plane and its guard
 * band; this does the same in clip space, to the near plane and twice the
 * viewport (Liang-Barsky). GX's clip z runs from -w at the near plane to 0
 * at the far one (C_MTXPerspective: w = -z). Perspective only, one position
 * matrix for the batch; the other attributes are interpolated with the
 * position (colours per byte, the rest as floats). Triangles go out as
 * they are. */
#define LINE_GUARD 2.0f
#define CLIP_MAX_STRIDE 256
#define CLIP_PLANES 5
static uint32_t s_st_lines, s_st_line_drop, s_st_line_clip;

/* The clip-space rows for the batch, once: cx, cy, cw and the near plane's
 * distance cz + cw, from the projection times the position matrix. 0 when
 * the batch can't be clipped here. */
static int clip_rows(float r[4][4]) {
    const XgxLayout* l = &B.plan.layout;
    const float(*p)[4] = g_xgx.proj;
    const float(*m)[4];
    int k;
    if (g_xgx.proj_ortho || l->off_mtx >= 0 || l->off_pos < 0 || l->stride > CLIP_MAX_STRIDE) return 0;
    m = g_xgx.posmtx[(g_xgx.cur_posmtx / 3) % XGX_NUM_POSMTX];
    for (k = 0; k < 4; k++) {   /* GX's perspective: cx = p00 x + p02 z, cy = p11 y + p12 z, cz = p22 z + p23, cw = -z */
        const float mz = m[2][k], t = k == 3 ? 1.0f : 0.0f;
        r[0][k] = p[0][0] * m[0][k] + p[0][2] * mz;
        r[1][k] = p[1][1] * m[1][k] + p[1][2] * mz;
        r[2][k] = -mz;
        r[3][k] = p[2][2] * mz + p[2][3] * t - mz;
    }
    return 1;
}

/* the vertex's distances inside the planes (near, then the guard band's
 * four) and its outcode, a bit per plane it is outside */
static inline uint32_t clip_dist(const uint8_t* v, const XgxLayout* l, const float r[4][4], float d[CLIP_PLANES]) {
    const float* x = (const float*)(v + l->off_pos);
    const float cx = r[0][0] * x[0] + r[0][1] * x[1] + r[0][2] * x[2] + r[0][3];
    const float cy = r[1][0] * x[0] + r[1][1] * x[1] + r[1][2] * x[2] + r[1][3];
    const float gw = LINE_GUARD * (r[2][0] * x[0] + r[2][1] * x[1] + r[2][2] * x[2] + r[2][3]);
    d[0] = r[3][0] * x[0] + r[3][1] * x[1] + r[3][2] * x[2] + r[3][3];
    d[1] = gw + cx;
    d[2] = gw - cx;
    d[3] = gw + cy;
    d[4] = gw - cy;
    return (d[0] < 0.0f) | (d[1] < 0.0f) << 1 | (d[2] < 0.0f) << 2 | (d[3] < 0.0f) << 3 | (d[4] < 0.0f) << 4;
}

static void lerp_vertex(uint8_t* dst, const uint8_t* a, const uint8_t* b, float t, const XgxLayout* l) {
    uint32_t w;
    for (w = 0; w < l->stride / 4; w++) {
        int32_t off = (int32_t)(w * 4);
        if (off == l->off_col[0] || off == l->off_col[1]) {
            int k;
            for (k = 0; k < 4; k++) dst[off + k] = (uint8_t)(a[off + k] + (b[off + k] - a[off + k]) * t + 0.5f);
        } else if (XGX_NRM_CMP && off == l->off_nrm) {
            memcpy(dst + off, a + off, 4);   /* packed: the nearer end's (lines rarely carry normals) */
        } else {
            const float fa = *(const float*)(a + off), fb = *(const float*)(b + off);
            *(float*)(dst + off) = fa + (fb - fa) * t;
        }
    }
}

/* Most lines are wholly inside (drawn as they are) or wholly outside one
 * plane (dropped), told apart by their ends' outcodes; only the rest get
 * Liang-Barsky's divisions. */
static void clip_lines(void) {
    const XgxLayout* l = &B.plan.layout;
    const uint32_t s = l->stride;
    float r[4][4];
    uint8_t* base = B.base;
    uint8_t tmp[2][CLIP_MAX_STRIDE];
    uint32_t i, out = 0;
    if (!clip_rows(r)) return;
    for (i = 0; i + 1 < B.done; i += 2) {
        uint8_t* v0 = base + i * s;
        uint8_t* v1 = v0 + s;
        float d0[CLIP_PLANES], d1[CLIP_PLANES];
        const uint32_t o0 = clip_dist(v0, l, r, d0), o1 = clip_dist(v1, l, r, d1);
        if (o0 & o1) {   /* both outside one plane */
            s_st_line_drop++;
            continue;
        }
        if (o0 | o1) {
            float t0 = 0.0f, t1 = 1.0f;
            int q;
            for (q = 0; q < CLIP_PLANES; q++) {
                const float a = d0[q], b = d1[q];
                if (a < 0.0f) {
                    const float t = a / (a - b);
                    if (t > t0) t0 = t;
                } else if (b < 0.0f) {
                    const float t = a / (a - b);
                    if (t < t1) t1 = t;
                }
            }
            if (t0 >= t1) {
                s_st_line_drop++;
                continue;
            }
            s_st_line_clip++;
            lerp_vertex(tmp[0], v0, v1, t0, l);
            lerp_vertex(tmp[1], v0, v1, t1, l);
            memcpy(base + out * s, tmp[0], s);
            memcpy(base + (out + 1) * s, tmp[1], s);
        } else if (out != i) {
            /* out < i: the ranges don't meet. The rain's 16-byte vertices
             * as a fixed-size copy (moves, not a call per line) */
            if (s == 16) __builtin_memcpy(base + out * 16, v0, 32);
            else memcpy(base + out * s, v0, 2 * s);
        }
        out += 2;
    }
    s_st_lines += B.done / 2;
    B.done = B.expected = out;
}

static void draw_batch(void) {
    uint32_t s, n;
    uint8_t* v;
    if (B.prim == XGX_LINES) clip_lines();
    s = B.plan.layout.stride;
    n = out_count(B.prim, B.done);
    B.pending = 0;
    if (!n || !(v = (uint8_t*)xgx_vtx_alloc(n, s))) return;
    copy_prim(v, B.base, B.prim, B.done, s);
    xgx_draw(out_prim(B.prim), n, &B.plan.layout, &g_xgx);
    g_xgx.dirty = 0;
}

/* A short strip or fan (a glyph, a billboard, a HUD quad: the game draws
 * these one GXBegin each) is built as it comes and turned into the list
 * primitive at its end (seg_list), so the next one can join it: one draw
 * instead of hundreds. Long ones are drawn as they are. */
#define SEG_MAX 64u
static uint32_t seg_list_prim(uint32_t prim, uint32_t n) {
    if (n > SEG_MAX) return 0;
    if (prim == XGX_TRISTRIP || prim == XGX_TRIFAN) return n >= 3 ? XGX_TRIANGLES : 0;
    if (prim == XGX_LINESTRIP) return n >= 2 ? XGX_LINES : 0;
    return 0;
}

/* the segment's vertices (B.seg_start on) rewritten as its list primitive */
static void seg_list(void) {
    static uint8_t tmp[SEG_MAX * 128];   /* the widest vertex is ~100 bytes */
    uint32_t s = B.plan.layout.stride, m = B.done - B.seg_start, i, out = 0;
    uint8_t* d;
    if (m * s > sizeof tmp) {   /* can't happen: drop it rather than misdraw */
        B.done = B.expected = B.seg_start;
        B.seg_prim = 0;
        return;
    }
    memcpy(tmp, B.base + B.seg_start * s, m * s);
    d = B.base + B.seg_start * s;
    if (B.seg_prim == XGX_TRISTRIP) {
        for (i = 0; i + 2 < m; i++, d += 3 * s, out += 3) {   /* every other one turned, as GX winds them */
            memcpy(d, tmp + (i + (i & 1)) * s, s);
            memcpy(d + s, tmp + (i + 1 - (i & 1)) * s, s);
            memcpy(d + 2 * s, tmp + (i + 2) * s, s);
        }
    } else if (B.seg_prim == XGX_TRIFAN) {
        for (i = 1; i + 1 < m; i++, d += 3 * s, out += 3) {   /* 0 i i+1 */
            memcpy(d, tmp, s);
            memcpy(d + s, tmp + i * s, 2 * s);
        }
    } else {                                                    /* LINESTRIP: i i+1 */
        for (i = 0; i + 1 < m; i++, d += 2 * s, out += 2) memcpy(d, tmp + i * s, 2 * s);
    }
    B.done = B.expected = B.seg_start + out;
    B.prim = seg_list_prim(B.seg_prim, m);
    B.seg_prim = 0;
}

static void begin_batch(uint32_t prim, int vtxfmt, uint32_t n) {
    Plan plan;
    uint32_t s, list = seg_list_prim(prim, n);
    make_plan(&plan, vtxfmt);
    s = plan.layout.stride;
    s_st_imm_batches++;
    if (B.pending) {
        /* a strip or fan joins a waiting list of what it will become; room
         * for its list form (at most 3 vertices for each) */
        if ((prim == B.prim || list == B.prim) && n &&
            out_count(prim, B.done + (list ? 3 * n : n)) <= JOIN_MAX &&
            memcmp(&plan.layout, &B.plan.layout, sizeof plan.layout) == 0 &&
            stage_reserve((B.done + (list ? 3 * n : n)) * s)) {
            B.base = s_stage;
            B.plan = plan;   /* same layout; the slots may read other formats */
            B.vtxfmt = vtxfmt;
            B.seg_prim = list ? prim : 0;
            B.seg_start = B.done;
            B.expected = B.done + n;
            B.cursor = 0;
            B.cur = B.base + B.done * s;
            B.open = 1;
            B.pending = 0;
            B.npos_carry = 0;
            B.ntc_carry = 0;
            memset(B.cur, 0, s);
            s_st_imm_joined++;
            return;
        }
        draw_batch();
    }
    B.plan = plan;
    B.prim = prim;
    B.vtxfmt = vtxfmt;
    B.seg_prim = list ? prim : 0;
    B.seg_start = 0;
    B.expected = n;
    B.done = 0;
    B.cursor = 0;
    B.base = n && stage_reserve((list ? 3 * n : n) * s) ? s_stage : NULL;
    B.cur = B.base;
    B.open = 1;
    B.npos_carry = 0;
    B.ntc_carry = 0;
    if (B.base) memset(B.base, 0, s);
}

static void end_batch(void) {
    B.open = 0;
    if (B.base && B.done < B.expected) s_st_imm_short++;
    if (B.seg_prim) {
        /* what arrived (all of it, or a short one's whole vertices) as the
         * list; fewer than one primitive's worth draws nothing, as before */
        if (B.base && B.done >= B.seg_start && seg_list_prim(B.seg_prim, B.done - B.seg_start)) {
            seg_list();
        } else {
            B.done = B.expected = B.seg_start;
            B.seg_prim = 0;
        }
    }
    if (!B.base || B.done == 0) return;
    if (B.done == B.expected && list_prim(B.prim, B.done)) B.pending = 1;
    else draw_batch();
}

void gx_vtx_close(void) {
    if (B.open) end_batch();
}

/* who drew a waiting batch (the state call that kept the next one from
 * joining it), for the [DLC] line */
#define FLUSH_WHO 16
static uint32_t s_flush_who[FLUSH_WHO], s_flush_n[FLUSH_WHO];

static void count_flush(uint32_t who) {
    int i, low = 0;
    for (i = 0; i < FLUSH_WHO; i++) {
        if (s_flush_who[i] == who) {
            s_flush_n[i]++;
            return;
        }
        if (s_flush_n[i] < s_flush_n[low]) low = i;
    }
    s_flush_who[low] = who;
    s_flush_n[low] = 1;
}

__attribute__((noinline)) void gx_vtx_flush(void) {
    if (B.open) end_batch();
    if (B.pending) {
        count_flush((uint32_t)(uintptr_t)__builtin_return_address(0));
        draw_batch();
    }
}

/* who begins immediate batches, for the [DLC] line (the game's GXBegin
 * callers: particles, billboards, the HUD) */
static uint32_t s_begin_who[FLUSH_WHO], s_begin_n[FLUSH_WHO];

__attribute__((noinline)) void GXBegin(GXPrimitive type, GXVtxFmt vtxfmt, u16 nverts) {
    uint32_t who = (uint32_t)(uintptr_t)__builtin_return_address(0);
    int i, low = 0;
    for (i = 0; i < FLUSH_WHO; i++) {
        if (s_begin_who[i] == who) break;
        if (s_begin_n[i] < s_begin_n[low]) low = i;
    }
    if (i < FLUSH_WHO) s_begin_n[i]++;
    else {
        s_begin_who[low] = who;
        s_begin_n[low] = 1;
    }
    if (B.open) end_batch();
    begin_batch((uint32_t)type, vtxfmt, nverts);
}

void GXEnd(void) {
    if (B.open) end_batch();
}

/* The slot for the next call of `attr` kind in immediate mode. Calls come in
 * GX order; a caller that skips an attribute leaves its data zeroed. */
static const Slot* next_slot(int want_mtx, uint8_t attr) {
    Plan* p = &B.plan;
    int i;
    if (!B.open || !B.base || B.done >= B.expected) return NULL;
    if (B.cursor < p->n && !want_mtx && p->slot[B.cursor].attr == attr) return &p->slot[B.cursor++];
    for (i = B.cursor; i < p->n; i++) {
        const Slot* s = &p->slot[i];
        if (want_mtx ? s->attr <= GX_VA_TEX7MTXIDX : s->attr == attr) {
            B.cursor = i + 1;
            return s;
        }
    }
    return NULL;
}

static void after_attr(void) {
    if (B.cursor >= B.plan.n) {
        B.done++;
        B.cursor = 0;
        B.cur += B.plan.layout.stride;
        if (B.done < B.expected) {
            uint32_t w, words = B.plan.layout.stride / 4;
            for (w = 0; w < words; w++) ((uint32_t*)B.cur)[w] = 0;
        }
    }
}

/* The immediate-mode calls' fast path: the vertex's next slot when it is
 * `attr`, direct, of store_fast kind `kind` (what the call writes); else
 * NULL and the call takes the general path. The rain alone made ~10000 of
 * these calls a frame (the Landing Site's profile: ~12% in them). */
static inline const Slot* fast_slot(uint8_t attr, uint8_t kind) {
    const Slot* s;
    if (!B.open || !B.base || B.done >= B.expected || B.cursor >= B.plan.n) return NULL;
    s = &B.plan.slot[B.cursor];
    return s->attr == attr && s->type == GX_DIRECT && s->kind == kind ? s : NULL;
}

static void imm_floats(uint8_t attr, const float* f, int n) {
    const Slot* s = next_slot(0, attr);
    if (!s) return;
    if (s->dst >= 0) {
        float out[3] = { 0, 0, 0 };
        int i;
        for (i = 0; i < n && i < 3; i++) out[i] = f[i];
        if (attr == GX_VA_NRM) put_nrm(B.cur + s->dst, out);
        else if (attr == GX_VA_POS) memcpy(B.cur + s->dst, out, 12);
        else memcpy(B.cur + s->dst, out, 8);
    }
    after_attr();
}

static void imm_index(uint8_t attr, uint32_t idx) {
    const Slot* s = next_slot(0, attr);
    if (!s) return;
    fetch_indexed(s, idx, B.cur);
    after_attr();
}

/* 2^-frac: the same value the division gave (a power of two), without one
 * per component */
static const float k_frac_scale[32] = {
    1.f, 1.f / (1u << 1), 1.f / (1u << 2), 1.f / (1u << 3), 1.f / (1u << 4), 1.f / (1u << 5), 1.f / (1u << 6),
    1.f / (1u << 7), 1.f / (1u << 8), 1.f / (1u << 9), 1.f / (1u << 10), 1.f / (1u << 11), 1.f / (1u << 12),
    1.f / (1u << 13), 1.f / (1u << 14), 1.f / (1u << 15), 1.f / (1u << 16), 1.f / (1u << 17), 1.f / (1u << 18),
    1.f / (1u << 19), 1.f / (1u << 20), 1.f / (1u << 21), 1.f / (1u << 22), 1.f / (1u << 23), 1.f / (1u << 24),
    1.f / (1u << 25), 1.f / (1u << 26), 1.f / (1u << 27), 1.f / (1u << 28), 1.f / (1u << 29), 1.f / (1u << 30),
    1.f / (1u << 31)
};
static float q(int v, uint8_t attr) { return (float)v * k_frac_scale[g_gx.vat[B.vtxfmt][attr].frac & 31]; }

void GXPosition3f32(f32 x, f32 y, f32 z) {
    const Slot* s = fast_slot(GX_VA_POS, SK_F32_3);
    if (s) {
        float* o = (float*)(B.cur + s->dst);
        o[0] = x;
        o[1] = y;
        o[2] = z;
        B.cursor++;
        after_attr();
        return;
    }
    {
        float f[3] = { x, y, z };
        imm_floats(GX_VA_POS, f, 3);
    }
}
void GXPosition3u16(u16 x, u16 y, u16 z) { float f[3] = { q(x, GX_VA_POS), q(y, GX_VA_POS), q(z, GX_VA_POS) }; imm_floats(GX_VA_POS, f, 3); }
void GXPosition3s16(s16 x, s16 y, s16 z) {
    const Slot* s = fast_slot(GX_VA_POS, SK_S16_3);
    if (s) {
        float* o = (float*)(B.cur + s->dst);
        o[0] = x * s->scale;
        o[1] = y * s->scale;
        o[2] = z * s->scale;
        B.cursor++;
        after_attr();
        return;
    }
    {
        float f[3] = { q(x, GX_VA_POS), q(y, GX_VA_POS), q(z, GX_VA_POS) };
        imm_floats(GX_VA_POS, f, 3);
    }
}
void GXPosition3u8(u8 x, u8 y, u8 z) { float f[3] = { q(x, GX_VA_POS), q(y, GX_VA_POS), q(z, GX_VA_POS) }; imm_floats(GX_VA_POS, f, 3); }
void GXPosition3s8(s8 x, s8 y, s8 z) { float f[3] = { q(x, GX_VA_POS), q(y, GX_VA_POS), q(z, GX_VA_POS) }; imm_floats(GX_VA_POS, f, 3); }
/* The GX FIFO takes components as a stream, so code that writes an XYZ
 * format's positions as pairs (HSD's shadow background quad: 12 floats in
 * six GXPosition2f32 calls) still makes whole vertices. Collect them (two
 * for an XY format, three for XYZ). */
static void pos_stream(float v) {
    int need = g_gx.vat[B.vtxfmt][GX_VA_POS].cnt == GX_POS_XYZ ? 3 : 2;
    B.pos_carry[B.npos_carry++] = v;
    if (B.npos_carry >= need) {
        if (need == 2) B.pos_carry[2] = 0;
        imm_floats(GX_VA_POS, B.pos_carry, 3);
        B.npos_carry = 0;
    }
}

/* The slot the FIFO's next bytes belong to: the raw writers (GXCmd1u8,
 * GXTexCoord1f32 used as a plain f32 write) fill whatever comes next. */
static const Slot* cur_slot(void) {
    if (!B.open || !B.base || B.done >= B.expected || B.cursor >= B.plan.n) return NULL;
    return &B.plan.slot[B.cursor];
}

static int pos_streams(void) { return B.open && g_gx.vat[B.vtxfmt][GX_VA_POS].cnt == GX_POS_XYZ; }

void GXPosition2f32(f32 x, f32 y) {
    float f[3] = { x, y, 0 };
    if (pos_streams()) {
        pos_stream(x);
        pos_stream(y);
        return;
    }
    imm_floats(GX_VA_POS, f, 3);
}
void GXPosition2u16(u16 x, u16 y) { float f[3] = { q(x, GX_VA_POS), q(y, GX_VA_POS), 0 }; imm_floats(GX_VA_POS, f, 3); }
void GXPosition2s16(s16 x, s16 y) { float f[3] = { q(x, GX_VA_POS), q(y, GX_VA_POS), 0 }; imm_floats(GX_VA_POS, f, 3); }
void GXPosition2u8(u8 x, u8 y) { float f[3] = { q(x, GX_VA_POS), q(y, GX_VA_POS), 0 }; imm_floats(GX_VA_POS, f, 3); }
void GXPosition2s8(s8 x, s8 y) { float f[3] = { q(x, GX_VA_POS), q(y, GX_VA_POS), 0 }; imm_floats(GX_VA_POS, f, 3); }
void GXPosition1x16(u16 i) { imm_index(GX_VA_POS, i); }
void GXPosition1x8(u8 i) { imm_index(GX_VA_POS, i); }

void GXNormal3f32(f32 x, f32 y, f32 z) { float f[3] = { x, y, z }; imm_floats(GX_VA_NRM, f, 3); }
void GXNormal3s16(s16 x, s16 y, s16 z) { float f[3] = { x / 16384.0f, y / 16384.0f, z / 16384.0f }; imm_floats(GX_VA_NRM, f, 3); }
void GXNormal3s8(s8 x, s8 y, s8 z) { float f[3] = { x / 64.0f, y / 64.0f, z / 64.0f }; imm_floats(GX_VA_NRM, f, 3); }
void GXNormal1x16(u16 i) { imm_index(GX_VA_NRM, i); }
void GXNormal1x8(u8 i) { imm_index(GX_VA_NRM, i); }

static void imm_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    /* colours go to CLR0, then CLR1, in order */
    const Slot* s = next_slot(0, GX_VA_CLR0);
    if (!s) s = next_slot(0, GX_VA_CLR1);
    if (!s) return;
    if (s->dst >= 0) {
        B.cur[s->dst] = r;
        B.cur[s->dst + 1] = g;
        B.cur[s->dst + 2] = b;
        B.cur[s->dst + 3] = a;
    }
    after_attr();
}

void GXColor4u8(u8 r, u8 g, u8 b, u8 a) {
    const Slot* s = fast_slot(GX_VA_CLR0, SK_RGBA8);
    if (s) {
        uint8_t* o = B.cur + s->dst;
        o[0] = r;
        o[1] = g;
        o[2] = b;
        o[3] = a;
        B.cursor++;
        after_attr();
        return;
    }
    imm_color(r, g, b, a);
}
void GXColor3u8(u8 r, u8 g, u8 b) { imm_color(r, g, b, 255); }
void GXColor1u32(u32 c) { imm_color((u8)(c >> 24), (u8)(c >> 16), (u8)(c >> 8), (u8)c); }
void GXColor1u16(u16 c) {
    uint8_t b[2] = { (uint8_t)(c >> 8), (uint8_t)c }, out[4];
    read_color(b, g_gx.vat[B.vtxfmt][GX_VA_CLR0].type, 1, out);
    imm_color(out[0], out[1], out[2], out[3]);
}
void GXColor1x16(u16 i) {
    const Slot* s = next_slot(0, GX_VA_CLR0);
    if (!s) s = next_slot(0, GX_VA_CLR1);
    if (!s) return;
    fetch_indexed(s, i, B.cur);
    after_attr();
}
void GXColor1x8(u8 i) { GXColor1x16(i); }

static void imm_tex(float s, float t) {
    const Slot* sl = NULL;
    int a;
    for (a = GX_VA_TEX0; a <= GX_VA_TEX7 && !sl; a++) sl = next_slot(0, (uint8_t)a);
    if (!sl) return;
    if (sl->dst >= 0) {
        float f[2] = { s, t };
        memcpy(B.cur + sl->dst, f, 8);
    }
    after_attr();
}

static float tq(int v) {
    int a;
    for (a = GX_VA_TEX0; a <= GX_VA_TEX7; a++)
        if (g_gx.desc[a] != GX_NONE) return (float)v / (float)(1u << g_gx.vat[B.vtxfmt][a].frac);
    return (float)v;
}

void GXTexCoord2f32(f32 s, f32 t) {
    const Slot* sl = B.open && B.base && B.done < B.expected && B.cursor < B.plan.n ? &B.plan.slot[B.cursor] : NULL;
    if (sl && sl->attr >= GX_VA_TEX0 && sl->attr <= GX_VA_TEX7 && sl->type == GX_DIRECT && sl->kind == SK_F32_2) {
        float* o = (float*)(B.cur + sl->dst);
        o[0] = s;
        o[1] = t;
        B.cursor++;
        after_attr();
        return;
    }
    imm_tex(s, t);
}
void GXTexCoord2u16(u16 s, u16 t) { imm_tex(tq(s), tq(t)); }
void GXTexCoord2s16(s16 s, s16 t) { imm_tex(tq(s), tq(t)); }
void GXTexCoord2u8(u8 s, u8 t) { imm_tex(tq(s), tq(t)); }
void GXTexCoord2s8(s8 s, s8 t) { imm_tex(tq(s), tq(t)); }
/* GXTexCoord1f32 is a plain f32 write to the FIFO: the float is the next
 * component of whatever attribute comes next in the vertex. The Classic
 * team card primes its depth plane with a position-only quad written as
 * twelve GXTexCoord1f32 (fn_80185408); HSD's particles (psdisp.c) write
 * billboard positions as three of them ahead of a TEX0 index, and their
 * form vertices' S and T as two. Taking each call as a whole texture
 * coordinate dropped every textured particle. */
void GXTexCoord1f32(f32 s) {
    const Slot* sl = cur_slot();
    int a;
    if (sl && sl->type == GX_DIRECT) {
        if (sl->attr == GX_VA_POS && sl->fmt.type == GX_F32) {
            pos_stream(s);
            return;
        }
        if (sl->attr >= GX_VA_TEX0 && sl->attr <= GX_VA_TEX7 && sl->fmt.type == GX_F32) {
            B.tc_carry[B.ntc_carry++] = s;
            if (B.ntc_carry >= comp_count(sl)) {
                if (B.ntc_carry < 2) B.tc_carry[1] = 0;
                B.ntc_carry = 0;
                imm_tex(B.tc_carry[0], B.tc_carry[1]);
            }
            return;
        }
    }
    for (a = GX_VA_TEX0; a <= GX_VA_TEX7; a++)
        if (g_gx.desc[a] != GX_NONE) break;
    if (a > GX_VA_TEX7 && g_gx.desc[GX_VA_POS] == GX_DIRECT && pos_streams()) {
        pos_stream(s);
        return;
    }
    imm_tex(s, 0);
}
/* The FIFO only sees bytes: a 16-bit write is the next two bytes of the
 * vertex, whatever the call's name says. Prime writes them through
 * GXTexCoord1s16 where the vertex holds something else: the rain
 * (CEnvFxManagerGrid::RenderRainParticles) a CLR0 RGBA4 per streak end
 * (alpha 10, then 0), the particle quads (CElementGen) a TEX0 of two S8s
 * with one fraction bit (0x0202: s and t 1.0). Taken as one coordinate,
 * the rain ended every batch short and drew nothing, and every particle's
 * t was 0: its texture's top row stretched over it, in stripes (the
 * landing cinematic, round hw1's "sky texture issues on ship entry"). */
static int put16(uint16_t v) {
    const Slot* sl = cur_slot();
    uint8_t b[2];
    if (!sl || sl->type != GX_DIRECT || sl->dsize != 2) return 0;
    if (sl->dst < 0) {
    } else if (sl->attr == GX_VA_CLR0 && sl->fmt.type == GX_RGBA4) {   /* the rain's colours, every streak end */
        uint8_t* o = B.cur + sl->dst;
        o[0] = (uint8_t)((v >> 12 & 15) * 17);
        o[1] = (uint8_t)((v >> 8 & 15) * 17);
        o[2] = (uint8_t)((v >> 4 & 15) * 17);
        o[3] = (uint8_t)((v & 15) * 17);
    } else if (sl->attr >= GX_VA_TEX0 && sl->fmt.type == GX_S8 && sl->fmt.cnt == GX_TEX_ST) {   /* the particles' corners */
        const float sc = 1.0f / (float)(1u << sl->fmt.frac);
        float* o = (float*)(B.cur + sl->dst);
        o[0] = (float)(int8_t)(v >> 8) * sc;
        o[1] = (float)(int8_t)v * sc;
    } else {
        b[0] = (uint8_t)(v >> 8);
        b[1] = (uint8_t)v;
        store(sl, b, 1, B.cur);
    }
    B.cursor++;
    after_attr();
    return 1;
}
void GXTexCoord1u16(u16 s) {
    if (!put16(s)) imm_tex(tq(s), 0);
}
void GXTexCoord1s16(s16 s) {
    if (!put16((u16)s)) imm_tex(tq(s), 0);
}
void GXTexCoord1u8(u8 s) { imm_tex(tq(s), 0); }
void GXTexCoord1s8(s8 s) { imm_tex(tq(s), 0); }
void GXTexCoord1x16(u16 i) {
    const Slot* sl = NULL;
    int a;
    for (a = GX_VA_TEX0; a <= GX_VA_TEX7 && !sl; a++) sl = next_slot(0, (uint8_t)a);
    if (!sl) return;
    fetch_indexed(sl, i, B.cur);
    after_attr();
}
void GXTexCoord1x8(u8 i) { GXTexCoord1x16(i); }

/* GXCmd1u8 inside a batch is a matrix index (PNMTXIDX, TEXnMTXIDX), or the
 * index of an INDEX8 attribute when that comes next: HSD's particle
 * billboards (psdisp.c) write TEX0's index into the corner table this way. */
void GXCmd1u8(const u8 x) {
    const Slot* s = cur_slot();
    if (s && s->attr > GX_VA_TEX7MTXIDX && s->type == GX_INDEX8) {
        B.cursor++;
        fetch_indexed(s, x, B.cur);
        after_attr();
        return;
    }
    s = next_slot(1, 0);
    if (!s) return;
    if (s->dst >= 0) {
        float f = (float)x;
        memcpy(B.cur + s->dst, &f, 4);
    }
    after_attr();
}
void GXCmd1u16(const u16 x) { (void)x; }
void GXCmd1u32(const u32 x) { (void)x; }
void GXParam1u8(const u8 x) { (void)x; }
void GXParam1u16(const u16 x) { (void)x; }
void GXParam1u32(const u32 x) { (void)x; }

/* ---- vertex descriptor / formats / arrays ----
 * The display-list cache keys and checks every call by the descriptor, the
 * formats a list uses and the arrays it indexes (fmt_key, vtx_sig). Those
 * were FNV hashes over ~250 bytes of state per call, ~700 calls a frame.
 * Instead each attribute's settings have a mixed 32-bit term, and the
 * setters keep sums of them up to date: a sum doesn't depend on the order
 * of the changes, so HSD's GXClearVtxDesc and the GXSetVtxDesc calls that
 * put the same descriptor back leave it as it was. */
static uint32_t s_desc_term[GX_VA_MAX_ATTR], s_desc_sum;
/* s_vat_sum[f]: the VAT terms of the attributes the descriptor has on only.
 * A list decodes the same whatever the VAT says for an attribute it doesn't
 * have, and HSD sets those freely (other objects' formats): with them in the
 * key, Fountain of Dreams held 1381 lists, 821 of them a second copy under
 * another key (1.7 MB of the 4 MB vertex pool, which ran full and rebuilt
 * lists every frame on the console); now 560 (docs/fps-plan.md C2). */
static uint32_t s_vat_term[8][GX_VA_MAX_ATTR], s_vat_sum[8];
static uint8_t s_vat_on[GX_VA_MAX_ATTR];   /* attribute a's terms are in s_vat_sum */
static uint32_t s_arr_term[GX_VA_MAX_ATTR], s_arr_sum;       /* indexed arrays: base, stride, byte order */
static uint32_t s_arrs_term[GX_VA_MAX_ATTR], s_arrs_sum;     /* the same without the base */

static uint32_t mix32(uint32_t h) {   /* murmur3's finalizer */
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    return h ^ h >> 16;
}

static void sig_desc(uint32_t a) {
    uint32_t t = mix32(0x01000000u | a << 8 | g_gx.desc[a]), f;
    uint8_t on = g_gx.desc[a] != GX_NONE;
    s_desc_sum += t - s_desc_term[a];
    s_desc_term[a] = t;
    if (on != s_vat_on[a]) {   /* the attribute's formats join or leave the key */
        for (f = 0; f < 8; f++) s_vat_sum[f] += on ? s_vat_term[f][a] : 0u - s_vat_term[f][a];
        s_vat_on[a] = on;
    }
}

static void sig_vat(uint32_t f, uint32_t a) {
    const GxAttrFmt* v = &g_gx.vat[f][a];
    uint32_t t = mix32(0x02000000u ^ f << 29 ^ a << 24 ^ (uint32_t)v->cnt << 16 ^ (uint32_t)v->type << 8 ^ v->frac);
    if (s_vat_on[a]) s_vat_sum[f] += t - s_vat_term[f][a];
    s_vat_term[f][a] = t;
}

/* after a change of attribute a's descriptor or array */
static void sig_arr(uint32_t a) {
    uint32_t t = 0, ts = 0;
    if (g_gx.desc[a] == GX_INDEX8 || g_gx.desc[a] == GX_INDEX16) {
        ts = mix32(0x03000000u ^ a << 24 ^ g_gx.array_stride[a] << 1 ^ g_gx.array_le[a]);
        t = mix32(ts ^ (uint32_t)(uintptr_t)g_gx.array[a]);
    }
    s_arr_sum += t - s_arr_term[a];
    s_arr_term[a] = t;
    s_arrs_sum += ts - s_arrs_term[a];
    s_arrs_term[a] = ts;
}

static void sig_recompute(void) {
    uint32_t a, f;
    memset(s_desc_term, 0, sizeof s_desc_term);
    memset(s_vat_term, 0, sizeof s_vat_term);
    memset(s_arr_term, 0, sizeof s_arr_term);
    memset(s_arrs_term, 0, sizeof s_arrs_term);
    s_desc_sum = s_arr_sum = s_arrs_sum = 0;
    memset(s_vat_sum, 0, sizeof s_vat_sum);
    memset(s_vat_on, 0, sizeof s_vat_on);
    for (a = 0; a < GX_VA_MAX_ATTR; a++) {
        sig_desc(a);
        sig_arr(a);
        for (f = 0; f < 8; f++) sig_vat(f, a);
    }
}

void GXSetVtxDesc(GXAttr attr, GXAttrType type) {
    gx_vtx_close();   /* a finished batch has its vertices already */
    if (attr < GX_VA_MAX_ATTR && g_gx.desc[attr] != (uint8_t)type) {
        g_gx.desc[attr] = (uint8_t)type;
        sig_desc(attr);
        sig_arr(attr);
    }
}

void GXSetVtxDescv(GXVtxDescList* list) {
    for (; list->attr != GX_VA_NULL; list++) GXSetVtxDesc(list->attr, list->type);
}

void GXClearVtxDesc(void) {
    uint32_t a;
    gx_vtx_close();   /* a finished batch has its vertices already */
    for (a = 0; a < GX_VA_MAX_ATTR; a++)
        if (g_gx.desc[a] != GX_NONE) {
            g_gx.desc[a] = GX_NONE;
            sig_desc(a);
            sig_arr(a);
        }
}

void GXSetVtxAttrFmt(GXVtxFmt fmt, GXAttr attr, GXCompCnt cnt, GXCompType type, u8 frac) {
    gx_vtx_close();   /* a finished batch has its vertices already */
    if (fmt >= 8 || attr >= GX_VA_MAX_ATTR) return;
    g_gx.vat[fmt][attr].cnt = (uint8_t)cnt;
    g_gx.vat[fmt][attr].type = (uint8_t)type;
    g_gx.vat[fmt][attr].frac = frac;
    sig_vat(fmt, attr);
}

void GXSetArray(GXAttr attr, const void* data, u32 size, u8 stride, bool le) {
    (void)size;
    gx_vtx_close();
    if (attr >= GX_VA_MAX_ATTR) return;
    g_gx.array[attr] = (const uint8_t*)data;
    g_gx.array_stride[attr] = stride;
    g_gx.array_le[attr] = le;
    sig_arr(attr);
}

/* ---- display lists ---- */
static uint32_t dl_vertex_bytes(const Plan* p) {
    uint32_t n = 0;
    int i;
    for (i = 0; i < p->n; i++) {
        const Slot* s = &p->slot[i];
        int idx_count = s->attr == GX_VA_NRM && s->fmt.cnt == GX_NRM_NBT3 ? 3 : 1;
        if (s->type == GX_DIRECT) n += (uint32_t)data_size(s);
        else n += (uint32_t)(s->type == GX_INDEX8 ? 1 : 2) * (uint32_t)idx_count;
    }
    return n;
}

/* index range each indexed attribute used, for the cache's array hash */
typedef struct {
    uint32_t lo[GX_VA_MAX_ATTR], hi[GX_VA_MAX_ATTR];
} IdxRange;

static uint8_t array_attr(const Slot* s) {
    return s->attr == GX_VA_NRM && g_gx.desc[GX_VA_NRM] == GX_NONE ? GX_VA_NBT : s->attr;
}

/* n vertices of plan p from the list at `at` into out; returns the new
 * offset. idx (optional): the index of every indexed slot that lands in the
 * vertex, vertex by vertex in slot order (the display-list cache's dynamic
 * lists fetch from them again). */
static uint32_t decode_verts(const uint8_t* dl, uint32_t at, const Plan* p, uint32_t n, uint8_t* out,
                             IdxRange* r, uint16_t* idx_out) {
    /* each indexed slot's array, looked up once for the batch */
    const uint8_t* base[N_ORDER];
    uint32_t stride[N_ORDER], words = p->layout.stride / 4, v;
    uint8_t be[N_ORDER];
    int i;
    for (i = 0; i < p->n; i++) {
        const Slot* s = &p->slot[i];
        uint8_t a = s->attr == GX_VA_NRM && g_gx.desc[GX_VA_NRM] == GX_NONE ? GX_VA_NBT : s->attr;
        base[i] = s->type == GX_DIRECT ? NULL : g_gx.array[a];
        stride[i] = g_gx.array_stride[a];
        be[i] = !g_gx.array_le[a];
    }
    for (v = 0; v < n; v++, out += p->layout.stride) {
        uint32_t w;
        for (w = 0; w < words; w++) ((uint32_t*)out)[w] = 0;   /* the canonical stride is whole words */
        for (i = 0; i < p->n; i++) {
            const Slot* s = &p->slot[i];
            if (s->type == GX_DIRECT) {
                store_fast(s, dl + at, 1, out);
                at += s->dsize;
            } else {
                int k, idx_count = s->attr == GX_VA_NRM && s->fmt.cnt == GX_NRM_NBT3 ? 3 : 1;
                for (k = 0; k < idx_count; k++) {
                    uint32_t idx = s->type == GX_INDEX8 ? dl[at] : gx_be16(dl + at);
                    at += s->type == GX_INDEX8 ? 1 : 2;
                    if (k) continue;
                    if (idx_out && s->dst >= 0) *idx_out++ = (uint16_t)idx;
                    if (base[i]) store_fast(s, base[i] + idx * stride[i], be[i], out);
                    if (r) {
                        uint8_t a = array_attr(s);
                        if (idx < r->lo[a]) r->lo[a] = idx;
                        if (idx + 1 > r->hi[a]) r->hi[a] = idx + 1;
                    }
                }
            }
        }
    }
    return at;
}

static void call_display_list(const void* list, u32 nbytes) {
    const uint8_t* dl = (const uint8_t*)list;
    uint32_t at = 0;
    gx_vtx_flush();
    while (at < nbytes) {
        uint8_t cmd = dl[at];
        if (cmd == 0x00 || cmd == 0x48) { at++; continue; }                 /* NOP, INVL_VC */
        if (cmd == 0x08) { at += 6; continue; }                             /* LOAD_CP_REG */
        if (cmd == 0x10) {                                                  /* LOAD_XF_REG */
            uint32_t n = at + 5 <= nbytes ? (uint32_t)gx_be16(dl + at + 1) + 1 : 0;
            at += 5 + n * 4;
            continue;
        }
        if ((cmd & 0xE7) == 0x20) { at += 5; continue; }                    /* LOAD_INDX_A..D */
        if (cmd == 0x40) { at += 9; continue; }                             /* CALL_DL (nested: unsupported) */
        if (cmd == 0x44) { at++; continue; }
        if (cmd == 0x61) { at += 5; continue; }                             /* LOAD_BP_REG */
        if (cmd >= 0x80 && cmd < 0xC0 && at + 3 <= nbytes) {
            uint32_t prim = cmd & 0xF8, fmt = cmd & 7, n = gx_be16(dl + at + 1);
            uint32_t vbytes;
            at += 3;
            begin_batch(prim, (int)fmt, n);
            vbytes = dl_vertex_bytes(&B.plan);
            if (at + n * vbytes > nbytes || !B.base) {
                B.open = 0;
                return;
            }
            /* after the vertices of a batch it joined, if it joined one */
            at = decode_verts(dl, at, &B.plan, n, B.cur, NULL, NULL);
            B.done = B.expected;
            end_batch();
            continue;
        }
        break;   /* unknown command: stop rather than misparse */
    }
}

/* ---- display-list cache ----
 * HSD's display lists are model data: the same list, with the same arrays,
 * is drawn every frame. The first call decodes it into a vertex buffer that
 * outlives the frame (xgx_vbuf_alloc); later calls replay the stored draws.
 * An entry is keyed by the list's address and size, and checked on every
 * call against a signature of the vertex descriptor, the formats the list
 * uses and the arrays it reads. At most once a frame a sampled hash of the
 * list and of the array ranges it indexed is compared too, because HSD
 * reuses memory and some arrays are rewritten (shape animation).
 *
 * A list whose arrays keep changing (skinned and morphed models: HSD
 * rewrites their positions and normals, or points them at another buffer,
 * every frame) goes dynamic: its decode plan, the indices of every vertex
 * and a decoded template in ordinary cached memory are kept. Each call
 * re-fetches only the attributes whose array moved or changed (a sampled
 * hash per array, per call; one that changed once is fetched every call
 * from then on), then copies the template into the vertex ring in one
 * sequential write. Before, such a list was parsed and fully decoded on
 * every call: ~200 a frame, most of the "dlist" time. A dynamic list that
 * doesn't fit the template budget is decoded every call as before
 * (volatile). */
#define DLC_MAX 2048
#define DLC_BUCKETS 4096
#define DLC_MAX_BATCH 4096      /* Fountain of Dreams' stage: one 104 KB list of more than 512 */
#define DLC_MAX_RANGE 12
#define DLC_VOLATILE 4          /* rebuilds before a list goes dynamic */
#define DLC_DYN_BUDGET (1024u * 1024)   /* template + index bytes for all dynamic lists */
/* and 1 MB more while the Xbox has DLC_DYN_EXTRA_FREE free: 1 MB left the
 * ship's save station (Samus up close) decoded every call, 12-14 fps on the
 * console (round hw5), but the memory is the game's to have first. At 6 MB
 * the pause screen's Samus (4.6 MB free there) decoded every call, 14-17
 * fps on the console (round hw8); 4 MB keeps a full extra above
 * XHW_MEM_LOW_KB (3 MB), where the reclaimers start */
#define DLC_DYN_EXTRA (1024u * 1024)
#define DLC_DYN_EXTRA_FREE (4u * 1024)  /* KB */
#define DLC_DYN_RETRY 120       /* frames before a list the budget turned away tries again */

typedef struct {
    uint32_t prim, count, offset;   /* offset: bytes into the entry's buffer */
    XgxLayout layout;
} DlBatch;

typedef struct {
    const uint8_t* p;
    uint32_t bytes;
} DlRange;

/* one array a dynamic list reads */
typedef struct {
    uint8_t attr;                  /* array_attr() */
    uint8_t always;                /* seen changing: fetched every call */
    uint32_t lo, hi;               /* index range */
    const uint8_t* base;           /* g_gx.array[attr] when fetched */
    uint32_t hash;
} DynArray;

typedef struct {
    uint32_t prim, count, offset;  /* offset: bytes into the template */
    uint32_t idx;                  /* first index (uint16) of this batch */
    uint8_t ncol;                  /* indexed slots that land in the vertex */
    uint8_t col_slot[N_ORDER];     /* their plan slots */
    Plan plan;
} DynBatch;

typedef struct {
    DynBatch* batch;
    DlBatch* view;                 /* the batches as DlBatch, then the draws they merge into */
    uint16_t* gfirst;              /* each draw's first batch */
    uint8_t* tmpl;
    uint16_t* idx;
    uint32_t total;                /* template bytes */
    uint32_t bytes;                /* charged to the budget */
    uint32_t sig, dl_hash;
    DynArray arr[DLC_MAX_RANGE];
    uint8_t narr;
    uint16_t nbatch, ndraw;
} DynList;

/* The bucket walk and a cached call's hit path read only the first 64
 * bytes, two of the Pentium III's 32-byte lines, with entries 32-byte
 * aligned: spread over a 184-byte entry (next at the end) they took four
 * cache misses a call, and GXCallDisplayList was 13% of the console's CPU
 * in Main Plaza (round hw5), most of it waiting on those loads. */
typedef struct __attribute__((aligned(32))) {
    const uint8_t* dl;
    uint32_t nbytes;
    int next;                      /* bucket chain, -1: end */
    uint32_t fkey;                 /* fmt_key(fmts) it was made under (part of the key) */
    uint8_t fmts, is_volatile, nrange;
    uint8_t prim1;                 /* a list of one draw (batch NULL): its primitive, */
    uint16_t nbatch;
    uint8_t bounds;                /* bmin/bmax hold the positions' box (no per-vertex matrix) */
    uint8_t off1;                  /* its offset into mem, */
    uint32_t sig;
    uint32_t checked, last_used;
    uint32_t stable;               /* content checks passed in a row (dlc_content_changed) */
    uint8_t* mem;
    DlBatch* batch;
    DynList* dyn;
    uint32_t qhash;                /* quick_hash of the same, for stable lists (dlc_content_changed) */
    uint32_t hash;
    uint16_t lay1, count1;         /* its layout (s_dlc_lay) and vertex count */
    /* 64: the rest */
    uint8_t rebuilds;
    uint32_t vol_retry;            /* volatile for want of budget: the frame to try dyn_build again (0: never) */
    uint32_t mem_bytes;
    float bmin[3], bmax[3];
    DlRange range[DLC_MAX_RANGE];
} DlEntry;
_Static_assert(__builtin_offsetof(DlEntry, count1) + 2 <= 64, "DlEntry's hot fields fit two cache lines");

/* The layouts of the one-draw lists, each once. Most cached lists are one
 * draw after merge_plan; their draw is kept in the entry's hot fields with
 * the layout here, where the few dozen layouts a room uses stay in the
 * cache. The batch table, an allocation of its own, was a cache miss on
 * every call (round hw5's profile). Never emptied: a layout is 64 bytes
 * and the table fills at DLC_LAYOUTS, after which lists keep a table. */
#define DLC_LAYOUTS 128
#define DLC_NO_LAYOUT 0xFFFFu
static XgxLayout s_dlc_lay[DLC_LAYOUTS];
static int s_dlc_nlay;

static uint32_t dlc_layout_id(const XgxLayout* l) {
    int i;
    for (i = 0; i < s_dlc_nlay; i++)
        if (memcmp(&s_dlc_lay[i], l, sizeof *l) == 0) return (uint32_t)i;
    if (s_dlc_nlay == DLC_LAYOUTS) return DLC_NO_LAYOUT;
    s_dlc_lay[s_dlc_nlay] = *l;
    return (uint32_t)s_dlc_nlay++;
}

static DlEntry s_dlc[DLC_MAX];
/* each slot's last_used, DLC_FREE for an empty one: the eviction scan reads
 * these 8 KB in order instead of a word from every 180-byte entry (2048
 * cache misses per eviction; the console's Fountain of Dreams evicted a few
 * times a frame and spent ~1.5% of its CPU there) */
#define DLC_FREE 0xFFFFFFFFu
static uint32_t s_dlc_lru[DLC_MAX];
static int s_dlc_n;
static int s_dlc_bucket[DLC_BUCKETS];
static int s_dlc_ready;
static uint32_t s_dyn_bytes;
static uint32_t s_st_dl_hits, s_st_dl_builds, s_st_dl_direct, s_st_dyn_calls, s_st_dyn_fetch, s_st_dyn_builds;
static uint32_t s_st_chg_sig, s_st_chg_data;   /* why cached lists were rebuilt: formats/arrays, or contents */
static uint32_t s_st_dl_joined;               /* batches drawn as part of the one before */
static const char* s_build_why;               /* why the last build failed, for log_uncached */

/* FNV-1a, 32-bit words (the tail a byte at a time) */
static uint32_t fnv(uint32_t h, const void* p, uint32_t n) {
    const uint8_t* b = (const uint8_t*)p;
    for (; n >= 4; n -= 4, b += 4) {
        uint32_t w;
        memcpy(&w, b, 4);
        h = (h ^ w) * 16777619u;
    }
    while (n--) h = (h ^ *b++) * 16777619u;
    return h;
}

/* 64 sampled words plus the tail, as the texture cache does. Four FNV
 * chains, one per word of every group of four: one chain is a serial
 * multiply per word, and the loads (cache misses, mostly) wait behind it. */
static uint32_t sample_hash(uint32_t h, const uint8_t* p, uint32_t n) {
    uint32_t i, step, h1 = h ^ 1, h2 = h ^ 2, h3 = h ^ 3, w[4];
    if (n < 256) {
        for (; n >= 16; n -= 16, p += 16) {
            memcpy(w, p, 16);
            h = (h ^ w[0]) * 16777619u;
            h1 = (h1 ^ w[1]) * 16777619u;
            h2 = (h2 ^ w[2]) * 16777619u;
            h3 = (h3 ^ w[3]) * 16777619u;
        }
        return fnv(((h ^ h1) * 16777619u ^ h2) * 16777619u ^ h3, p, n);
    }
    step = (n - 4) / 64;
    for (i = 0; i < 64; i += 4) {
        memcpy(&w[0], p + i * step, 4);
        memcpy(&w[1], p + (i + 1) * step, 4);
        memcpy(&w[2], p + (i + 2) * step, 4);
        memcpy(&w[3], p + (i + 3) * step, 4);
        h = (h ^ w[0]) * 16777619u;
        h1 = (h1 ^ w[1]) * 16777619u;
        h2 = (h2 ^ w[2]) * 16777619u;
        h3 = (h3 ^ w[3]) * 16777619u;
    }
    return fnv(((h ^ h1) * 16777619u ^ h2) * 16777619u ^ h3, p + n - 4, 4);
}

/* 16 sampled words plus the tail (everything under 256 bytes): the recheck
 * of a list that has stayed the same for a while. The words are scattered
 * cache misses, so a quarter of sample_hash's cost. */
static uint32_t quick_hash(uint32_t h, const uint8_t* p, uint32_t n) {
    uint32_t i, step, w[4];
    if (n < 256) return sample_hash(h, p, n);
    step = (n - 4) / 16;
    for (i = 0; i < 16; i += 4) {
        memcpy(&w[0], p + i * step, 4);
        memcpy(&w[1], p + (i + 1) * step, 4);
        memcpy(&w[2], p + (i + 2) * step, 4);
        memcpy(&w[3], p + (i + 3) * step, 4);
        h = (((h ^ w[0]) * 16777619u ^ w[1]) * 16777619u ^ w[2]) * 16777619u;
        h = (h ^ w[3]) * 16777619u;
    }
    return fnv(h, p + n - 4, 4);
}

/* the vertex descriptor and the formats `fmts` use; with_arrays: the array
 * bases too (a dynamic list takes moving arrays as they come) */
static uint32_t fmt_key(uint32_t fmts);
static uint32_t vtx_sig(uint32_t fmts, int with_arrays) {
    return mix32(fmt_key(fmts) ^ (with_arrays ? s_arr_sum : s_arrs_sum * 0x9E3779B1u));
}

static uint32_t content_hash(const DlEntry* e) {
    uint32_t h = sample_hash(2166136261u, e->dl, e->nbytes);
    int i;
    for (i = 0; i < e->nrange; i++) h = sample_hash(h, e->range[i].p, e->range[i].bytes);
    return h;
}

static uint32_t content_qhash(const DlEntry* e) {
    uint32_t h = quick_hash(2166136261u, e->dl, e->nbytes);
    int i;
    for (i = 0; i < e->nrange; i++) h = quick_hash(h, e->range[i].p, e->range[i].bytes);
    return h;
}

static uint32_t dl_bucket(const void* dl) { return ((uint32_t)(uintptr_t)dl >> 5) % DLC_BUCKETS; }

static void dlc_unlink(int idx) {
    int* link = &s_dlc_bucket[dl_bucket(s_dlc[idx].dl)];
    while (*link >= 0 && *link != idx) link = &s_dlc[*link].next;
    if (*link == idx) *link = s_dlc[idx].next;
}

static void dyn_free(DlEntry* e) {
    DynList* d = e->dyn;
    if (!d) return;
    s_dyn_bytes -= d->bytes;
    free(d->view);
    free(d->gfirst);
    free(d->batch);
    free(d->tmpl);
    free(d->idx);
    free(d);
    e->dyn = NULL;
}

/* the dynamic list idle longest (not drawn for DYN_IDLE_FRAMES), template
 * freed; 0 when none is that old */
#define DYN_IDLE_FRAMES 120
static int dyn_evict_idle(uint32_t frame, const DlEntry* keep) {
    int i, pick = -1;
    uint32_t oldest = frame;
    for (i = 0; i < DLC_MAX; i++) {
        const DlEntry* x = &s_dlc[i];
        if (!x->dl || !x->dyn || x == keep) continue;
        if (frame - x->last_used >= DYN_IDLE_FRAMES && x->last_used < oldest) {
            oldest = x->last_used;
            pick = i;
        }
    }
    if (pick < 0) return 0;
    dyn_free(&s_dlc[pick]);
    s_dlc[pick].rebuilds = 0;   /* back to an ordinary entry: may go dynamic again */
    return 1;
}

static void dlc_release_mem(DlEntry* e, int now) {
    if (now) xgx_vbuf_free_now(e->mem);
    else xgx_vbuf_free(e->mem);
    free(e->batch);
    e->mem = NULL;
    e->batch = NULL;
    e->mem_bytes = 0;
    e->nbatch = 0;
    dyn_free(e);
}

static void dlc_release(DlEntry* e) { dlc_release_mem(e, 0); }

/* entries stay in their slot; freed slots go on a stack */
static int s_dlc_free[DLC_MAX];
static int s_dlc_nfree;

static void dlc_drop_mem(int idx, int now) {
    dlc_unlink(idx);
    dlc_release_mem(&s_dlc[idx], now);
    s_dlc[idx].dl = NULL;
    s_dlc_lru[idx] = DLC_FREE;
    s_dlc_free[s_dlc_nfree++] = idx;
    s_dlc_n--;
}

static void dlc_drop(int idx) { dlc_drop_mem(idx, 0); }

#ifndef XGX_CENSUS
#define XGX_CENSUS 0
#endif
#if XGX_CENSUS
/* Display-list census (docs/fps-plan.md step 0.4), with the draw census in
 * nv2a.c: every XGX_STATS_EVERY presents, [DLCC] lines give the cached
 * vertex bytes by owner (the p_link of the GObj that drew the list first,
 * xgx_probe.h), the lists cached under more than one format key, and the
 * lists built most often in the interval (evicted and drawn again, or
 * changed). tools/xbox/census_report.py prints them. */
static uint8_t s_cen_owner[DLC_MAX];
#define CEN_BUILDS 4096
typedef struct {
    const uint8_t* dl;
    uint32_t nbytes, builds, mem;
    uint8_t owner;
} CenBuild;
static CenBuild s_cen_b[CEN_BUILDS];
static uint32_t s_cen_lost;

static void cen_build(const DlEntry* e, int idx) {
    uint32_t h = ((uint32_t)(uintptr_t)e->dl * 2654435761u) >> 20, k;
    s_cen_owner[idx] = (uint8_t)xgx_census_tag;
    for (k = 0; k < 32; k++, h = (h + 1) & (CEN_BUILDS - 1)) {
        CenBuild* b = &s_cen_b[h];
        if (b->dl == e->dl && b->nbytes == e->nbytes) {
            b->builds++;
            b->mem = e->mem_bytes;
            return;
        }
        if (!b->dl) {
            b->dl = e->dl;
            b->nbytes = e->nbytes;
            b->builds = 1;
            b->mem = e->mem_bytes;
            b->owner = (uint8_t)xgx_census_tag;
            return;
        }
    }
    s_cen_lost++;
}

static void cen_report(void) {
    static char buf[4096];
    uint32_t kb[256], lists[256], dup = 0, dup_kb = 0, i, k, n = 0;
    CenBuild top[12];
    memset(kb, 0, sizeof kb);
    memset(lists, 0, sizeof lists);
    memset(top, 0, sizeof top);
    for (i = 0; i < DLC_MAX; i++) {
        const DlEntry* e = &s_dlc[i];
        int j;
        if (!e->dl || !e->mem) continue;
        kb[s_cen_owner[i]] += e->mem_bytes;
        lists[s_cen_owner[i]]++;
        /* the same list under another key: same bucket chain */
        for (j = s_dlc_bucket[dl_bucket(e->dl)]; j >= 0; j = s_dlc[j].next)
            if ((uint32_t)j < i && s_dlc[j].dl == e->dl && s_dlc[j].nbytes == e->nbytes && s_dlc[j].mem) {
                dup++;
                dup_kb += e->mem_bytes;
                break;
            }
    }
    n += snprintf(buf + n, sizeof buf - (size_t)n, "[DLCC] cached by owner (lists/KB):");
    for (i = 0; i < 256; i++)
        if (lists[i]) n += snprintf(buf + n, sizeof buf - (size_t)n, " %u:%u/%u", i, lists[i], kb[i] / 1024);
    n += snprintf(buf + n, sizeof buf - (size_t)n, "\n[DLCC] %u lists cached under a second key (%u KB)", dup,
                  dup_kb / 1024);
    for (i = 0; i < CEN_BUILDS; i++) {
        CenBuild b = s_cen_b[i];
        if (!b.dl || b.builds <= top[11].builds) continue;
        for (k = 11; k > 0 && top[k - 1].builds < b.builds; k--) top[k] = top[k - 1];
        top[k] = b;
    }
    n += snprintf(buf + n, sizeof buf - (size_t)n, "\n[DLCC] built most (list:bytes:owner:KB x builds, %u lost):",
                  s_cen_lost);
    for (k = 0; k < 12 && top[k].builds; k++)
        n += snprintf(buf + n, sizeof buf - (size_t)n, " %08x:%u:%u:%u x%u", (unsigned)(uintptr_t)top[k].dl,
                      top[k].nbytes, top[k].owner, top[k].mem / 1024, top[k].builds);
    xhw_log(buf);
    memset(s_cen_b, 0, sizeof s_cen_b);
    s_cen_lost = 0;
}
#endif

/* The content check (sampled words of the list and its arrays) runs once a
 * frame per list; a list that passed DLC_STABLE checks in a row is checked
 * every fourth frame, staggered by slot, and with a quarter of the samples
 * (content_qhash). On the console the checks took ~4% of the CPU in a match,
 * where no list changed content at all; v32's Fountain of Dreams ~3%. */
#define DLC_STABLE 120
/* and one that passed DLC_STABLE2 more is checked every eighth frame: the
 * samples are cache misses in game memory, a few thousand a frame in Main
 * Plaza's ~1600 lists */
#define DLC_STABLE2 (DLC_STABLE + 600)
static int dlc_content_changed(DlEntry* e, uint32_t frame) {
    if (e->checked == frame) return 0;
#if defined(XHW_PMC) && XHW_PMC
    if (xhw_ablate(XHW_AB_RECHECK)) return 0;   /* the probe's window 5 */
#endif
    if (e->stable >= DLC_STABLE && ((frame + (uint32_t)(e - s_dlc)) & (e->stable >= DLC_STABLE2 ? 7 : 3))) return 0;
    e->checked = frame;
    if (e->stable >= DLC_STABLE ? e->qhash != content_qhash(e) : e->hash != content_hash(e)) {
        e->stable = 0;
        return 1;
    }
    if (e->stable < DLC_STABLE2) e->stable++;
    return 0;
}

/* evicts the least recently used entry not drawn this frame, other than
 * `keep`; 0: none */
/* Textures need the room spilled display lists took in the texture pool
 * (xgx_vbuf_alloc): drop up to `max` of them, least recently used first,
 * none drawn this frame. Returns how many. */
int gx_dlc_evict_spilled(int max) {
    uint32_t frame = xgx_present_count();
    int n = 0;
    while (n < max) {
        int i, pick = -1;
        uint32_t best = DLC_FREE;
        for (i = 0; i < DLC_MAX; i++) {
            uint32_t u = s_dlc_lru[i];
            if (u < best && u != frame && s_dlc[i].mem && xgx_vbuf_spilled(s_dlc[i].mem)) {
                best = u;
                pick = i;
            }
        }
        if (pick < 0) break;
        dlc_drop_mem(pick, 1);
        n++;
    }
    return n;
}

/* Only lists not drawn for DLC_EVICT_AGE frames go. When a room's lists
 * need more than the pool, least-recently-used eviction turns over the
 * whole cache every frame: each list is evicted just before it is drawn
 * again, and every call rebuilds (round hw6: 31 builds a frame, 34 ms of
 * display lists, 17 fps). With an age floor the lists that fit stay
 * cached and only the overflow draws uncached; a room change still frees
 * the last room's lists after a few frames. */
#ifndef DLC_EVICT_AGE
#define DLC_EVICT_AGE 3
#endif
static int dlc_evict_one(uint32_t frame, const DlEntry* keep) {
    int i, pick = -1, skip = keep ? (int)(keep - s_dlc) : -1;
    uint32_t best = DLC_FREE;
    for (i = 0; i < DLC_MAX; i++) {
        uint32_t u = s_dlc_lru[i];
        if (u < best && frame - u >= DLC_EVICT_AGE && i != skip) {
            best = u;
            pick = i;
        }
    }
    if (pick < 0) return 0;
    dlc_drop_mem(pick, 1);   /* not drawn this frame: the GPU is done with it */
    return 1;
}

/* The vertex descriptor and the formats a list uses are part of an entry's
 * key: HSD draws some lists under different formats (small lists shared by
 * models whose positions are quantized differently, the shadow pass's
 * descriptor), and one entry per list flipping between them rebuilt on every
 * call until it went volatile (~100 lists, decoded on every call). */
static uint32_t fmt_key(uint32_t fmts) {
    /* the indexed arrays' strides and byte orders too (not their bases):
     * Prime draws a skinned model from its CPU workspace (native order) and
     * the same list from the disc's arrays (big-endian) in one frame; under
     * one key each draw rebuilt the other's entry until it went volatile
     * (the Frigate: 58 lists decoded on every call) */
    uint32_t h = mix32(s_desc_sum ^ s_arrs_sum * 0x9E3779B1u), f;
    for (f = 0; f < 8; f++)
        if (fmts & (1u << f)) h = mix32(h + s_vat_sum[f]);
    return h;
}

static DlEntry* dlc_find(const uint8_t* dl, uint32_t nbytes) {
    uint32_t mask = 0x100, key = 0;   /* fmt_key of the last mask asked for */
    int i;
    for (i = s_dlc_bucket[dl_bucket(dl)]; i >= 0; i = s_dlc[i].next) {
        DlEntry* e = &s_dlc[i];
        if (e->dl != dl || e->nbytes != nbytes) continue;
        if (e->fmts != mask) key = fmt_key(mask = e->fmts);
        if (e->fkey == key) return e;
    }
    return NULL;
}

/* skips a non-draw command at `at`; 0: not one this cache knows */
static uint32_t dl_skip(const uint8_t* dl, uint32_t at, uint32_t nbytes) {
    uint8_t cmd = dl[at];
    if (cmd == 0x00 || cmd == 0x48 || cmd == 0x44) return at + 1;
    if (cmd == 0x08) return at + 6;
    if (cmd == 0x10) return at + 5 + (at + 5 <= nbytes ? ((uint32_t)gx_be16(dl + at + 1) + 1) * 4 : 0);
    if ((cmd & 0xE7) == 0x20 || cmd == 0x61) return at + 5;
    if (cmd == 0x40) return at + 9;
    return 0;
}

/* pass 1 over a list: its draws and their layouts; 0 when it can't be cached */
/* scratch for a build, grown to the most draws a list has had (the stage
 * lists run to hundreds; most lists have a few) */
static DlBatch *s_sc_batch, *s_sc_merged;
static uint16_t* s_sc_first;
static uint32_t* s_sc_idx;
static int s_sc_cap;

static int scan_reserve(int n) {
    int cap = s_sc_cap ? s_sc_cap : 64;
    void* p;
    if (n <= s_sc_cap) return 1;
    while (cap < n) cap *= 2;
    if (!(p = realloc(s_sc_batch, sizeof(DlBatch) * (size_t)cap))) return 0;
    s_sc_batch = (DlBatch*)p;
    if (!(p = realloc(s_sc_merged, sizeof(DlBatch) * (size_t)cap))) return 0;
    s_sc_merged = (DlBatch*)p;
    if (!(p = realloc(s_sc_first, sizeof(uint16_t) * (size_t)cap))) return 0;
    s_sc_first = (uint16_t*)p;
    if (!(p = realloc(s_sc_idx, sizeof(uint32_t) * (size_t)cap))) return 0;
    s_sc_idx = (uint32_t*)p;
    s_sc_cap = cap;
    return 1;
}

static int dl_scan(const uint8_t* dl, uint32_t nbytes, uint32_t* total, uint8_t* fmts) {
    DlBatch* batch = s_sc_batch;
    Plan plan;
    uint32_t at = 0, next;
    int nb = 0;
    *total = 0;
    *fmts = 0;
    while (at < nbytes) {
        uint8_t cmd = dl[at];
        if (cmd >= 0x80 && cmd < 0xC0 && at + 3 <= nbytes) {
            uint32_t n = gx_be16(dl + at + 1), fmt = cmd & 7, vbytes;
            if (nb == DLC_MAX_BATCH || (nb >= s_sc_cap && !scan_reserve(nb + 1))) {
                s_build_why = nb == DLC_MAX_BATCH ? "too many draws" : "no memory";
                return 0;
            }
            batch = s_sc_batch;
            make_plan(&plan, (int)fmt);
            vbytes = dl_vertex_bytes(&plan);
            at += 3;
            if (at + n * vbytes > nbytes) {
                s_build_why = "draw runs past the end";
                return 0;
            }
            batch[nb].prim = cmd & 0xF8;
            batch[nb].count = n;
            batch[nb].offset = *total;
            batch[nb].layout = plan.layout;
            *total += (n * plan.layout.stride + 15) & ~15u;
            *fmts |= (uint8_t)(1u << fmt);
            at += n * vbytes;
            nb++;
            continue;
        }
        if (!(next = dl_skip(dl, at, nbytes))) break;
        at = next;
    }
    return *total ? nb : 0;
}

static void range_reset(IdxRange* r) {
    uint32_t a;
    for (a = 0; a < GX_VA_MAX_ATTR; a++) {
        r->lo[a] = 0xFFFFFFFFu;
        r->hi[a] = 0;
    }
}

/* pass 2: decode the draws found by dl_scan into out (template or buffer) */
static void dl_decode_all(const uint8_t* dl, uint32_t nbytes, const DlBatch* batch, int nb, uint8_t* out, IdxRange* r,
                          uint16_t* idx, const uint32_t* idx_first) {
    Plan plan;
    uint32_t at = 0;
    int i;
    for (i = 0; i < nb && at < nbytes;) {
        uint8_t cmd = dl[at];
        if (cmd >= 0x80 && cmd < 0xC0) {
            make_plan(&plan, cmd & 7);
            at = decode_verts(dl, at + 3, &plan, batch[i].count, out + batch[i].offset, r,
                              idx ? idx + idx_first[i] : NULL);
            i++;
            continue;
        }
        at = dl_skip(dl, at, nbytes);
        if (!at) break;
    }
}

/* Batches of one list share its material, so consecutive ones with the
 * same layout and primitive are drawn as one: lists and quads simply run on,
 * strips are stitched with degenerate triangles. Each draw costs the same
 * whatever its size (xemu most of all), and HSD's lists hold a few strips
 * each. */
static int joinable(const DlBatch* a, uint32_t prim, const XgxLayout* layout) {
    return a->prim == prim &&
           (prim == XGX_TRISTRIP || prim == XGX_TRIANGLES || prim == XGX_LINES || prim == XGX_POINTS) &&
           memcmp(&a->layout, layout, sizeof *layout) == 0;
}

/* vertices a strip adds when joined after `have`: the last one again, then
 * its own first once or twice so it starts on an even vertex (its winding) */
static uint32_t join_extra(uint32_t prim, uint32_t have) { return prim == XGX_TRISTRIP ? 2 + (have & 1) : 0; }

/* groups the batches into draws (quads and fans as triangles, see
 * out_prim); first[g]: the group's first batch */
static int merge_plan(const DlBatch* in, int nb, DlBatch* out, uint16_t* first, uint32_t* total) {
    int i, ng = 0;
    for (i = 0; i < nb; i++) {
        uint32_t prim = out_prim(in[i].prim), n = out_count(in[i].prim, in[i].count);
        if (!n) continue;
        if (ng && joinable(&out[ng - 1], prim, &in[i].layout) &&
            out[ng - 1].count + join_extra(prim, out[ng - 1].count) + n <= JOIN_MAX) {
            out[ng - 1].count += join_extra(prim, out[ng - 1].count) + n;
            continue;
        }
        out[ng] = in[i];
        out[ng].prim = prim;
        out[ng].count = n;
        first[ng++] = (uint16_t)i;
    }
    *total = 0;
    for (i = 0; i < ng; i++) {
        out[i].offset = *total;
        *total += (out[i].count * out[i].layout.stride + 15) & ~15u;
    }
    return ng;
}

/* the vertices of batches [from, end) as one draw at dst */
static void merge_copy_group(const DlBatch* in, int from, int end, const uint8_t* src, uint8_t* dst) {
    uint32_t s = in[from].layout.stride, have = 0, k;
    int i;
    for (i = from; i < end; i++) {
        const uint8_t* v = src + in[i].offset;
        uint32_t n = out_count(in[i].prim, in[i].count);
        if (!n) continue;
        if (have && in[i].prim == XGX_TRISTRIP) {
            uint32_t extra = join_extra(XGX_TRISTRIP, have);
            memcpy(dst, dst - s, s);
            dst += s;
            for (k = 1; k < extra; k++, dst += s) memcpy(dst, v, s);
            have += extra;
        }
        dst += copy_prim(dst, v, in[i].prim, in[i].count, s);
        have += n;
    }
}

static void merge_copy(const DlBatch* in, int nb, const uint8_t* src, const DlBatch* out, const uint16_t* first, int ng,
                       uint8_t* dst) {
    int g;
    for (g = 0; g < ng; g++) merge_copy_group(in, first[g], g + 1 < ng ? first[g + 1] : nb, src, dst + out[g].offset);
}

#ifdef XGX_CHECK_VERTS
/* -DXGX_CHECK_VERTS: every position a list build decodes must be finite and
 * within reach of the scene. A huge or NaN one makes the rasterizer walk far
 * outside the surface, the suspect for the console's GPU stalls (PGRAPH
 * LIMIT_ZETA); the first ones found are logged with the list. Model space,
 * before the matrices: what HSD handed over, not what the GPU computed. */
static void check_verts(const DlEntry* e, const DlBatch* b, int nb, const uint8_t* v) {
    static uint32_t s_logged;
    int i;
    for (i = 0; i < nb && s_logged < 32; i++) {
        uint32_t k;
        if (b[i].layout.off_pos < 0) continue;
        for (k = 0; k < b[i].count; k++) {
            /* exponent bits: 2^20 and up, infinities and NaNs (no float
             * compares, which fast-math may assume finite) */
            const uint32_t* p = (const uint32_t*)(v + b[i].offset + k * b[i].layout.stride + b[i].layout.off_pos);
            if (((p[0] >> 23) & 0xFF) < 127 + 20 && ((p[1] >> 23) & 0xFF) < 127 + 20 && ((p[2] >> 23) & 0xFF) < 127 + 20)
                continue;
            xhw_logf("[WARN] dlist %p (%u bytes) draw %d/%d vertex %u/%u: position %08x %08x %08x", (const void*)e->dl,
                     e->nbytes, i, nb, k, b[i].count, p[0], p[1], p[2]);
            s_logged++;
            break;
        }
    }
}
#endif

/* decode the whole list into one buffer; 0 when it can't be cached */
static int dlc_build(DlEntry* e, uint32_t frame) {
    static IdxRange r;
    DlBatch *batch, *merged;
    uint16_t* first;
    uint32_t total, mtotal, a;
    uint8_t fmts;
    uint8_t* tmp;
    int nb, ng, i;
    s_build_why = "no draws";
    if (!(nb = dl_scan(e->dl, e->nbytes, &total, &fmts))) return 0;
    batch = s_sc_batch;
    merged = s_sc_merged;
    first = s_sc_first;
    /* decoded in cached memory, then copied on in order: the vertex pool is
     * write-combined */
    tmp = (uint8_t*)malloc(total);
    if (!tmp) {
        s_build_why = "no memory";
        return 0;
    }
    range_reset(&r);
    dl_decode_all(e->dl, e->nbytes, batch, nb, tmp, &r, NULL, NULL);
#ifdef XGX_CHECK_VERTS
    check_verts(e, batch, nb, tmp);
#endif
    ng = merge_plan(batch, nb, merged, first, &mtotal);
    for (i = 0; i < ng; i++) mtotal += merged[i].layout.stride;   /* room to align each draw, below */
    e->mem = mtotal ? (uint8_t*)xgx_vbuf_alloc(mtotal) : NULL;
    while (mtotal && !e->mem && dlc_evict_one(frame, e)) e->mem = (uint8_t*)xgx_vbuf_alloc(mtotal);
    if (!e->mem) {
        s_build_why = "vertex pool full";
        free(tmp);
        dlc_release(e);
        return 0;
    }
    {   /* each draw at a multiple of its stride from the pool's start (xgx_draw) */
        uint32_t rel = xgx_vbuf_offset(e->mem), at = 0;
        for (i = 0; i < ng; i++) {
            uint32_t st = merged[i].layout.stride;
            merged[i].offset = (rel + at + st - 1) / st * st - rel;
            at = merged[i].offset + merged[i].count * st;
        }
    }
    e->lay1 = DLC_NO_LAYOUT;
    if (ng == 1 && merged[0].offset <= 0xFF && merged[0].count <= 0xFFFF)
        e->lay1 = (uint16_t)dlc_layout_id(&merged[0].layout);
    e->batch = e->lay1 != DLC_NO_LAYOUT ? NULL : (DlBatch*)malloc(sizeof(DlBatch) * (size_t)ng);
    if (e->lay1 == DLC_NO_LAYOUT && !e->batch) {
        s_build_why = "no memory";
        free(tmp);
        dlc_release(e);
        return 0;
    }
    merge_copy(batch, nb, tmp, merged, first, ng, e->mem);
    e->bounds = 1;   /* the model-space box, for gx_dl_culled */
    e->bmin[0] = e->bmin[1] = e->bmin[2] = 1e30f;
    e->bmax[0] = e->bmax[1] = e->bmax[2] = -1e30f;
    for (i = 0; i < nb && e->bounds; i++) {
        const XgxLayout* l = &batch[i].layout;
        uint32_t v, k;
        if (l->off_pos < 0 || l->off_mtx >= 0) {
            e->bounds = 0;
            break;
        }
        for (v = 0; v < batch[i].count; v++) {
            const float* q = (const float*)(tmp + batch[i].offset + v * l->stride + l->off_pos);
            for (k = 0; k < 3; k++) {
                if (!(q[k] == q[k])) e->bounds = 0;   /* NaN */
                if (q[k] < e->bmin[k]) e->bmin[k] = q[k];
                if (q[k] > e->bmax[k]) e->bmax[k] = q[k];
            }
        }
    }
    free(tmp);
    if (e->batch) {
        memcpy(e->batch, merged, sizeof(DlBatch) * (size_t)ng);
    } else {
        e->prim1 = (uint8_t)merged[0].prim;
        e->off1 = (uint8_t)merged[0].offset;
        e->count1 = (uint16_t)merged[0].count;
    }
    s_st_dl_joined += (uint32_t)(nb - ng);
    e->nbatch = (uint16_t)ng;
    e->mem_bytes = mtotal;
    e->fmts = fmts;
    e->nrange = 0;
    for (a = 0; a < GX_VA_MAX_ATTR; a++)
        if (r.hi[a] > r.lo[a] && g_gx.array[a] && e->nrange < DLC_MAX_RANGE) {
            e->range[e->nrange].p = g_gx.array[a] + r.lo[a] * g_gx.array_stride[a];
            e->range[e->nrange].bytes = (r.hi[a] - r.lo[a]) * g_gx.array_stride[a];
            e->nrange++;
        }
    e->sig = vtx_sig(fmts, 1);
    e->fkey = fmt_key(fmts);
    e->hash = content_hash(e);
    e->qhash = content_qhash(e);
    e->stable = 0;
    e->checked = frame;
    s_st_dl_builds++;
    return 1;
}

static uint32_t dyn_array_hash(const DynArray* a, const uint8_t* base) {
    uint32_t stride = g_gx.array_stride[a->attr];
    return sample_hash(2166136261u, base + a->lo * stride, (a->hi - a->lo) * stride);
}

/* the dynamic form of e (see the comment above DLC_MAX); 0: over budget or
 * not cacheable */
static int dyn_build(DlEntry* e, uint32_t frame) {
    static IdxRange r;
    DlBatch* batch;
    uint32_t* idx_first;
    uint32_t total, nidx = 0, a, bytes, budget;
    uint8_t fmts;
    DynList* d;
    int nb = dl_scan(e->dl, e->nbytes, &total, &fmts), i, k;
    if (!nb) return 0;
    batch = s_sc_batch;
    idx_first = s_sc_idx;
    d = (DynList*)calloc(1, sizeof *d);
    if (!d) return 0;
    d->batch = (DynBatch*)calloc((size_t)nb, sizeof(DynBatch));
    if (!d->batch) {
        free(d);
        return 0;
    }
    for (i = 0; i < nb; i++) {
        d->batch[i].prim = batch[i].prim;
        d->batch[i].count = batch[i].count;
        d->batch[i].offset = batch[i].offset;
    }
    /* the plans (dl_scan keeps only the layouts) and the indexed columns */
    {
        uint32_t at = 0;
        for (i = 0; i < nb && at < e->nbytes;) {
            uint8_t cmd = e->dl[at];
            if (cmd >= 0x80 && cmd < 0xC0) {
                DynBatch* b = &d->batch[i];
                make_plan(&b->plan, cmd & 7);
                for (k = 0; k < b->plan.n; k++) {
                    const Slot* sl = &b->plan.slot[k];
                    if (sl->type != GX_DIRECT && sl->dst >= 0) b->col_slot[b->ncol++] = (uint8_t)k;
                }
                b->idx = idx_first[i] = nidx;
                nidx += b->count * b->ncol;
                at += 3 + b->count * dl_vertex_bytes(&b->plan);
                i++;
                continue;
            }
            at = dl_skip(e->dl, at, e->nbytes);
            if (!at) break;
        }
        if (i != nb) {
            free(d->batch);
            free(d);
            return 0;
        }
    }
    bytes = total + nidx * 2 + (uint32_t)nb * (sizeof(DynBatch) + 2 * sizeof(DlBatch) + sizeof(uint16_t));
    /* over the budget: make room by dropping the dynamic lists no draw has
     * used for a while (a model from a room left behind keeps its template
     * otherwise, and every list after it goes volatile: Phendrana, ~80) */
    budget = DLC_DYN_BUDGET + (xhw_mem_free_kb() >= DLC_DYN_EXTRA_FREE ? DLC_DYN_EXTRA : 0);
    while (s_dyn_bytes + bytes > budget && dyn_evict_idle(frame, e)) {
    }
    if (s_dyn_bytes + bytes > budget) {
        free(d->batch);
        free(d);
        return 0;
    }
    d->tmpl = (uint8_t*)malloc(total);
    d->idx = (uint16_t*)malloc(nidx ? nidx * 2 : 2);
    if (!d->tmpl || !d->idx) {
        free(d->tmpl);
        free(d->idx);
        free(d->batch);
        free(d);
        return 0;
    }
    range_reset(&r);
    dl_decode_all(e->dl, e->nbytes, batch, nb, d->tmpl, &r, d->idx, idx_first);
    d->nbatch = (uint16_t)nb;
    d->total = total;
    d->bytes = bytes;
    d->view = (DlBatch*)malloc(sizeof(DlBatch) * (size_t)nb * 2);
    d->gfirst = (uint16_t*)malloc(sizeof(uint16_t) * (size_t)nb);
    if (d->view && !d->gfirst) {
        free(d->view);
        d->view = NULL;
    }
    if (d->view) {
        uint32_t mtotal;
        for (i = 0; i < nb; i++) {
            d->view[i].prim = d->batch[i].prim;
            d->view[i].count = d->batch[i].count;
            d->view[i].offset = d->batch[i].offset;
            d->view[i].layout = d->batch[i].plan.layout;
        }
        d->ndraw = (uint16_t)merge_plan(d->view, nb, d->view + nb, d->gfirst, &mtotal);
    }
    s_dyn_bytes += bytes;
    for (a = 0; a < GX_VA_MAX_ATTR; a++)
        if (r.hi[a] > r.lo[a] && g_gx.array[a] && d->narr < DLC_MAX_RANGE) {
            DynArray* x = &d->arr[d->narr++];
            x->attr = (uint8_t)a;
            x->lo = r.lo[a];
            x->hi = r.hi[a];
            x->base = g_gx.array[a];
            x->hash = dyn_array_hash(x, x->base);
        }
    d->sig = vtx_sig(fmts, 0);
    e->fkey = fmt_key(fmts);
    d->dl_hash = sample_hash(2166136261u, e->dl, e->nbytes);
    e->dyn = d;
    e->fmts = fmts;
    e->checked = frame;
    s_st_dyn_builds++;
    return 1;
}

/* fetch attribute array `attr` again for the vertices of batch b, into out
 * (the batch's vertices: in the template, or a copy of them). The arrays
 * are the skinning's output, written just before and far larger than the
 * L2, and the Pentium III has no hardware prefetch for an indexed walk:
 * round hw7's Landing Site spent a quarter of GXCallDisplayList's samples
 * on this loop's array load. The indices are known ahead, so each element
 * is prefetched DYN_PREFETCH vertices before it is read (a hint: it never
 * faults, whatever the address). */
#ifndef XGX_DYN_PREFETCH
#define XGX_DYN_PREFETCH 8   /* 0: no prefetch */
#endif
#define DYN_PREFETCH XGX_DYN_PREFETCH
static void dyn_fetch_batch(const DynList* d, const DynBatch* b, uint8_t attr, const uint8_t* base, uint8_t* out) {
    uint32_t stride = g_gx.array_stride[attr];
    int be = !g_gx.array_le[attr], c;
    for (c = 0; c < b->ncol; c++) {
        const Slot* sl = &b->plan.slot[b->col_slot[c]];
        const uint16_t* idx = d->idx + b->idx + c;
        uint8_t* v = out;
        uint32_t k, n = b->count, vs = b->plan.layout.stride, ahead = DYN_PREFETCH * b->ncol;
        if (array_attr(sl) != attr) continue;
        for (k = 0; k < DYN_PREFETCH && k < n; k++) __builtin_prefetch(base + idx[k * b->ncol] * stride);
        for (k = 0; k < n; k++, idx += b->ncol, v += vs) {
            if (DYN_PREFETCH && k + DYN_PREFETCH < n) __builtin_prefetch(base + idx[ahead] * stride);
            store(sl, base + *idx * stride, be, v);
        }
    }
}

/* The arrays a dynamic call fetches again. Skinned models (Samus's arm
 * cannon; the Landing Site's dynamic lists are 800 KB) fetch positions and
 * normals on every call. Fetched into the template and the template then
 * copied to the ring, every template line was read twice and written back
 * once a call (a template is far larger than the 128 KB L2; memcpy was 10%
 * of the console's CPU there, round hw5, half of it called from here).
 * Each batch is staged through a scratch buffer that stays in the L1
 * instead: the template read once, nothing written back, the ring written
 * in order. The template keeps its build-time values for the attributes
 * fetched this way; an array fetched once is fetched on every later call
 * (DynArray.always). */
typedef struct {
    uint8_t attr;
    const uint8_t* base;
} DynFetch;
static uint8_t s_dyn_scratch[16384] __attribute__((aligned(32)));

/* batch i's vertices with this call's arrays fetched: in the scratch buffer,
 * or fetched into the template itself when the batch doesn't fit it */
static const uint8_t* dyn_batch_verts(DynList* d, int i, const DynFetch* f, int nf) {
    const DynBatch* b = &d->batch[i];
    uint32_t bytes = b->count * b->plan.layout.stride;
    uint8_t* out = d->tmpl + b->offset;
    int k;
    if (!nf) return out;
    if (bytes <= sizeof s_dyn_scratch) {
        memcpy(s_dyn_scratch, out, bytes);
        out = s_dyn_scratch;
    }
    for (k = 0; k < nf; k++) dyn_fetch_batch(d, b, f[k].attr, f[k].base, out);
    return out;
}

/* merge_copy_group for a dynamic list: batches [from, end) as one draw at
 * dst, each staged by dyn_batch_verts. A joined strip repeats the last
 * vertex of the one before, kept here: dst is write-combined, and reading
 * it back is an uncached read. */
static void dyn_emit_group(DynList* d, int from, int end, uint8_t* dst, const DynFetch* f, int nf) {
    uint32_t s = d->batch[from].plan.layout.stride, have = 0, k;
    uint8_t last[128] __attribute__((aligned(16)));
    int i;
    for (i = from; i < end; i++) {
        const DynBatch* b = &d->batch[i];
        uint32_t n = out_count(b->prim, b->count);
        const uint8_t* v;
        if (!n) continue;
        v = dyn_batch_verts(d, i, f, nf);
        if (have && b->prim == XGX_TRISTRIP) {
            uint32_t extra = join_extra(XGX_TRISTRIP, have);
            memcpy(dst, s <= sizeof last ? last : dst - s, s);
            dst += s;
            for (k = 1; k < extra; k++, dst += s) memcpy(dst, v, s);
            have += extra;
        }
        dst += copy_prim(dst, v, b->prim, b->count, s);
        have += n;
        if (b->prim == XGX_TRISTRIP && s <= sizeof last) memcpy(last, v + (b->count - 1) * s, s);
    }
}

#if XGX_DYN_VERIFY
/* -DXGX_DYN_VERIFY=1: every dynamic draw made the old way too (fetched into
 * a copy of the whole template, then merge_copy_group), once before the
 * draw and once after, and compared. The two references differing means
 * the arrays changed under the call (counted apart: not this code's) */
static uint32_t s_dynv_draws, s_dynv_bad, s_dynv_moved;
static uint8_t* dyn_ref(DynList* d, int from, int end, uint32_t bytes, int merged, const DynFetch* f, int nf) {
    uint8_t* t = (uint8_t*)malloc(d->total);
    uint8_t* want = (uint8_t*)malloc(bytes);
    int i, k;
    if (t && want) {
        memcpy(t, d->tmpl, d->total);
        for (i = from; i < end; i++)
            for (k = 0; k < nf; k++) dyn_fetch_batch(d, &d->batch[i], f[k].attr, f[k].base, t + d->batch[i].offset);
        if (merged) merge_copy_group(d->view, from, end, t, want);
        else memcpy(want, t + d->batch[from].offset, bytes);
    } else {
        free(want);
        want = NULL;
    }
    free(t);
    return want;
}

static void dyn_verify(DynList* d, int from, int end, uint8_t* want0, const uint8_t* got, uint32_t bytes, int merged,
                       const DynFetch* f, int nf) {
    uint8_t* want = dyn_ref(d, from, end, bytes, merged, f, nf);
    if (want0 && want) {
        s_dynv_draws++;
        if (memcmp(want0, want, bytes) != 0) {
            s_dynv_moved++;
        } else if (memcmp(want, got, bytes) != 0 && s_dynv_bad++ < 8) {
            uint8_t* mine = (uint8_t*)malloc(bytes);
            if (mine) {
                if (merged) dyn_emit_group(d, from, end, mine, f, nf);
                else memcpy(mine, dyn_batch_verts(d, from, f, nf), bytes);
                xhw_logf("[DLC] dyn verify: emitted again into cached memory: %s the reference, %s the ring",
                         memcmp(mine, want, bytes) ? "differs from" : "matches",
                         memcmp(mine, got, bytes) ? "differs from" : "matches");
                free(mine);
            }
            uint32_t at = 0, s = d->batch[from].plan.layout.stride, ndiff = 0, o;
            while (want[at] == got[at]) at++;
            for (o = at; o < bytes; o++) ndiff += want[o] != got[o];
            xhw_logf("[DLC] dyn verify: batches %d-%d (%u bytes, %d fetched) differ: first at %u (vertex %u byte %u of "
                     "%u), %u bytes in all; want %02x%02x%02x%02x got %02x%02x%02x%02x",
                     from, end - 1, bytes, nf, at, at / s, at % s, s, ndiff, want[at], want[at + 1], want[at + 2],
                     want[at + 3], got[at], got[at + 1], got[at + 2], got[at + 3]);
        }
    }
    free(want0);
    free(want);
}
#endif

/* the first lists to go volatile, and why, for tuning the cache */
static void log_volatile(const DlEntry* e, const char* why) {
    static int logged;
    if (logged++ >= 24) return;
    xhw_logf("[DLC] volatile: list %08x (%u bytes, formats %02x, %u rebuilds): %s", (unsigned)(uintptr_t)e->dl,
             e->nbytes, e->fmts, e->rebuilds, why);
}

/* 1: drawn from the dynamic template */
static int dyn_call(DlEntry* e, uint32_t frame) {
    DynList* d = e->dyn;
    DynFetch fetch[DLC_MAX_RANGE];
    int i, nf = 0;
#if XGX_DYN_VERIFY
    uint8_t* want0;
#endif
    int sig_changed = d->sig != vtx_sig(e->fmts, 0);
    if (sig_changed ||
        (e->checked != frame && (e->checked = frame, d->dl_hash != sample_hash(2166136261u, e->dl, e->nbytes)))) {
        dyn_free(e);
        if (++e->rebuilds >= DLC_VOLATILE * 4 || !dyn_build(e, frame)) {
            log_volatile(e, e->rebuilds >= DLC_VOLATILE * 4 ? (sig_changed ? "formats changed" : "contents changed")
                                                            : "over the dynamic budget");
            e->is_volatile = 1;
            e->vol_retry = e->rebuilds >= DLC_VOLATILE * 4 ? 0 : frame + DLC_DYN_RETRY;
            return 0;
        }
        d = e->dyn;
    }
    for (i = 0; i < d->narr; i++) {
        DynArray* x = &d->arr[i];
        const uint8_t* base = g_gx.array[x->attr];
        if (!base) continue;
        if (!x->always) {
            uint32_t h;
            if (base == x->base && (h = dyn_array_hash(x, base)) == x->hash) continue;
            x->always = 1;
        }
        x->base = base;
        fetch[nf].attr = x->attr;
        fetch[nf++].base = base;
        s_st_dyn_fetch++;
    }
    for (i = 0; d->view && i < d->ndraw; i++) {
        const DlBatch* b = &d->view[d->nbatch + i];
        int end = i + 1 < d->ndraw ? d->gfirst[i + 1] : d->nbatch;
        uint8_t* v = (uint8_t*)xgx_vtx_alloc(b->count, b->layout.stride);
        if (!v) break;   /* too big for the ring as one: batch by batch below */
#if XGX_DYN_VERIFY
        want0 = dyn_ref(d, d->gfirst[i], end, b->count * b->layout.stride, 1, fetch, nf);
#endif
        dyn_emit_group(d, d->gfirst[i], end, v, fetch, nf);
#if XGX_DYN_VERIFY
        dyn_verify(d, d->gfirst[i], end, want0, v, b->count * b->layout.stride, 1, fetch, nf);
#endif
        xgx_draw(b->prim, b->count, &b->layout, &g_xgx);
        g_xgx.dirty = 0;
    }
    for (i = d->view && i < d->ndraw ? d->gfirst[i] : d->view ? d->nbatch : 0; i < d->nbatch; i++) {
        const DynBatch* b = &d->batch[i];
        uint32_t bytes = b->count * b->plan.layout.stride;
        uint8_t* v;
        if (!b->count || !(v = (uint8_t*)xgx_vtx_alloc(b->count, b->plan.layout.stride))) continue;
#if XGX_DYN_VERIFY
        want0 = dyn_ref(d, i, i + 1, bytes, 0, fetch, nf);
#endif
        memcpy(v, dyn_batch_verts(d, i, fetch, nf), bytes);
#if XGX_DYN_VERIFY
        dyn_verify(d, i, i + 1, want0, v, bytes, 0, fetch, nf);
#endif
        xgx_draw(b->prim, b->count, &b->plan.layout, &g_xgx);
        g_xgx.dirty = 0;
    }
    e->last_used = s_dlc_lru[e - s_dlc] = frame;
    s_st_dyn_calls++;
    return 1;
}

/* the first lists drawn uncached, and why */
static void log_uncached(const uint8_t* dl, uint32_t nbytes, const char* why) {
    static const uint8_t* seen[24];
    static int logged;
    int i;
    for (i = 0; i < logged; i++)
        if (seen[i] == dl) return;
    if (logged >= 24) return;
    seen[logged++] = dl;
    xhw_logf("[DLC] uncached: list %08x (%u bytes): %s", (unsigned)(uintptr_t)dl, nbytes, why);
}

/* 1: drawn from the cache */
static int dlc_call(const uint8_t* dl, uint32_t nbytes) {
    uint32_t frame = xgx_present_count();
    DlEntry* e;
    int i;
    if (!s_dlc_ready) {
        memset(s_dlc_bucket, 0xFF, sizeof s_dlc_bucket);
        memset(s_dlc_lru, 0xFF, sizeof s_dlc_lru);
        for (i = 0; i < DLC_MAX; i++) s_dlc_free[i] = DLC_MAX - 1 - i;
        s_dlc_nfree = DLC_MAX;
        s_dlc_ready = 1;
    }
    e = dlc_find(dl, nbytes);
    /* a list of more than one draw has a batch table, an allocation of its
     * own (a cache miss a call, round hw5's profile): asked for now, it
     * arrives during the checks */
    if (e && e->batch) __builtin_prefetch(e->batch);
    if (e && e->dyn) return dyn_call(e, frame);
    if (e && e->is_volatile) {
        /* turned away by the budget: lists left behind free it in time */
        if (!e->vol_retry || (int32_t)(frame - e->vol_retry) < 0) return 0;
        if (!dyn_build(e, frame)) {
            e->vol_retry = frame + DLC_DYN_RETRY;
            return 0;
        }
        e->is_volatile = 0;
        e->vol_retry = 0;
        return dyn_call(e, frame);
    }
    if (e && e->mem && (e->sig != vtx_sig(e->fmts, 1) || dlc_content_changed(e, frame))) {
        if (e->sig != vtx_sig(e->fmts, 1)) s_st_chg_sig++;
        else s_st_chg_data++;
        dlc_release(e);   /* changed: rebuild below */
        if (++e->rebuilds >= DLC_VOLATILE) {
            if (dyn_build(e, frame)) return dyn_call(e, frame);
            log_volatile(e, "no dynamic form");
            e->is_volatile = 1;
            e->vol_retry = frame + DLC_DYN_RETRY;
            return 0;
        }
    }
    if (!e) {
        int idx;
        if (!s_dlc_nfree && !dlc_evict_one(frame, NULL)) {
            log_uncached(dl, nbytes, "no free entry");
            return 0;
        }
        idx = s_dlc_free[--s_dlc_nfree];
        e = &s_dlc[idx];
        memset(e, 0, sizeof *e);
        s_dlc_lru[idx] = 0;   /* last_used */
        e->dl = dl;
        e->nbytes = nbytes;
        e->fkey = fmt_key(0);   /* until a build knows the formats */
        e->next = s_dlc_bucket[dl_bucket(dl)];
        s_dlc_bucket[dl_bucket(dl)] = idx;
        s_dlc_n++;
    }
    if (!e->mem) {
        if (!dlc_build(e, frame)) {
            log_uncached(dl, nbytes, s_build_why);
            return 0;
        }
#if XGX_CENSUS
        cen_build(e, (int)(e - s_dlc));
#endif
    }
    e->last_used = s_dlc_lru[e - s_dlc] = frame;
    s_st_dl_hits++;
    if (!e->batch) {   /* one draw, kept in the entry */
        xgx_vtx_use(e->mem + e->off1);
        xgx_draw(e->prim1, e->count1, &s_dlc_lay[e->lay1], &g_xgx);
        g_xgx.dirty = 0;
        return 1;
    }
    for (i = 0; i < e->nbatch; i++) {
        const DlBatch* b = &e->batch[i];
        if (!b->count) continue;
        xgx_vtx_use(e->mem + b->offset);
        xgx_draw(b->prim, b->count, &b->layout, &g_xgx);
        g_xgx.dirty = 0;
    }
    return 1;
}

void gx_vtx_cache_flush(void) {
    int i;
    for (i = 0; i < DLC_MAX; i++)
        if (s_dlc[i].dl) dlc_drop(i);
}

/* The memory breakdown (xhw_mem_breakdown_log) with the display-list
 * cache's malloc'd part: batch tables and dynamic lists. */
static void memb_log(void) {
    char extra[200];
    uint32_t batch = 0, one = 0;
    int i;
    for (i = 0; i < DLC_MAX; i++) {
        if (s_dlc[i].batch) batch += (uint32_t)sizeof(DlBatch) * s_dlc[i].nbatch;
        else one += s_dlc[i].mem != NULL;
    }
    snprintf(extra, sizeof extra,
             "DLC %d lists (%u of one draw, %d layouts), batches %u B, dynamic %u B | vertex pool %u KB free, tex pool %u "
             "KB free",
             s_dlc_n, one, s_dlc_nlay, batch, s_dyn_bytes, xgx_vbuf_pool_free_kb(), xgx_tex_pool_free_kb());
    xhw_mem_breakdown_log(extra);
}

void gx_vtx_frame_end(void) {
    if (XHW_MEMB && xhw_mem_breakdown_due(XHW_MEMB)) memb_log();
    if (xgx_present_count() % XGX_STATS_EVERY == 0 && s_dlc_ready) {
        int i, vol = 0, dyn = 0;
        for (i = 0; i < DLC_MAX; i++) {
            vol += s_dlc[i].dl && s_dlc[i].is_volatile;
            dyn += s_dlc[i].dl && s_dlc[i].dyn;
        }
        xhw_logf("[DLC] %d of %d lists (%d dynamic, %u KB; %d volatile), vertex pool %u of %u KB free | per %u: %u "
                 "cached calls, %u builds (%u after a format/array change, %u after a content change; %u batches "
                 "joined), %u dynamic calls (%u array fetches, %u builds), %u decoded | immediate: %u batches, %u "
                 "joined, %u short",
                 s_dlc_n, DLC_MAX, dyn, s_dyn_bytes / 1024, vol, xgx_vbuf_pool_free_kb(), xgx_vbuf_pool_kb(),
                 XGX_STATS_EVERY, s_st_dl_hits, s_st_dl_builds, s_st_chg_sig, s_st_chg_data, s_st_dl_joined,
                 s_st_dyn_calls, s_st_dyn_fetch, s_st_dyn_builds, s_st_dl_direct, s_st_imm_batches, s_st_imm_joined,
                 s_st_imm_short);
        s_st_dl_hits = s_st_dl_builds = s_st_dl_direct = s_st_dyn_calls = s_st_dyn_fetch = s_st_dyn_builds = 0;
        s_st_chg_sig = s_st_chg_data = s_st_dl_joined = s_st_imm_batches = s_st_imm_joined = s_st_imm_short = 0;
        {
            char line[256];
            int n = 0;
            for (i = 0; i < FLUSH_WHO; i++)
                if (s_flush_n[i]) n += snprintf(line + n, sizeof line - (size_t)n, " %08x:%u", s_flush_who[i], s_flush_n[i]);
            xhw_logf("[DLC] waiting batches drawn from:%s", n ? line : " -");
            memset(s_flush_n, 0, sizeof s_flush_n);
            memset(s_flush_who, 0, sizeof s_flush_who);
            n = 0;
            for (i = 0; i < FLUSH_WHO; i++)
                if (s_begin_n[i]) n += snprintf(line + n, sizeof line - (size_t)n, " %08x:%u", s_begin_who[i], s_begin_n[i]);
            xhw_logf("[DLC] immediate batches begun from:%s", n ? line : " -");
            xhw_logf("[DLC] lines %u: %u outside the view, %u clipped", s_st_lines, s_st_line_drop, s_st_line_clip);
            s_st_lines = s_st_line_drop = s_st_line_clip = 0;
            memset(s_begin_n, 0, sizeof s_begin_n);
            memset(s_begin_who, 0, sizeof s_begin_who);
        }
#if XGX_CENSUS
        cen_report();
#endif
#if XGX_DYN_VERIFY
        xhw_logf("[DLC] dyn verify: %u draws, %u differ (%u more with arrays that changed during the call)",
                 s_dynv_draws, s_dynv_bad, s_dynv_moved);
#endif
    }
}

/* For HSD_DObjDisp (PORT): 1 when a cached list's vertices, moved by mtx
 * (model to view) and the current projection, lie wholly outside the view
 * volume's sides or behind the camera. 0 when unknown: not cached yet,
 * dynamic, per-vertex matrices, contents changed. */
int gx_dl_culled(const void* list, u32 nbytes, const float mtx[3][4]) {
    const DlEntry* e = NULL;
    int i;
    if (s_dlc_ready)
        for (i = s_dlc_bucket[dl_bucket((const uint8_t*)list)]; i >= 0; i = s_dlc[i].next)
            if (s_dlc[i].dl == (const uint8_t*)list && s_dlc[i].nbytes == nbytes) {
                e = &s_dlc[i];
                break;
            }
    if (!e || !e->bounds || e->dyn || e->is_volatile || !e->mem) return 0;
    /* a culled list is never called, so its content check runs here: memory
     * reused for another model must not stay hidden behind the old box (a
     * changed list is drawn, and dlc_call rebuilds it) */
    {
        uint32_t frame = xgx_present_count();
        if (frame - e->checked > 16) {
            if (e->stable >= DLC_STABLE ? e->qhash != content_qhash(e) : e->hash != content_hash(e)) return 0;
            ((DlEntry*)e)->checked = frame;
        }
    }
    /* The box in view space: its centre, and the half extents of the
     * view-aligned box around it (|M| times the model-space half extents).
     * That box contains the 8 corners, so "wholly outside one clip plane"
     * holds for it only when it holds for every corner: it culls a little
     * less than testing the corners, never more, at a third of the cost
     * (the 8-corner version was ~2% of the console's CPU on Fountain of
     * Dreams). The clip planes are kept per projection (gx_cull.h). */
    {
        static GxCullPlanes s_planes;
        return gx_cull_box(gx_cull_planes(&s_planes, g_xgx.proj, gx_proj_gen), e->bmin, e->bmax, mtx);
    }
}

/* port_xbox_gx.h: the same test for a box the game knows before it draws
 * (Prime's rain and snow grids, CEnvFxManagerGrid::Render), through the
 * current position matrix */
int GXPortBoxCulled(const float bmin[3], const float bmax[3]) {
    static GxCullPlanes s_planes;
    if (g_xgx.proj_ortho) return 0;
    return gx_cull_box(gx_cull_planes(&s_planes, g_xgx.proj, gx_proj_gen), bmin, bmax,
                       (const float(*)[4])g_xgx.posmtx[(g_xgx.cur_posmtx / 3) % XGX_NUM_POSMTX]);
}

/* port_xbox_gx.h: the rain's streaks in one call, where the open batch is
 * exactly a direct S16 position then a direct RGBA4 colour (vertex format
 * 6 as CEnvFxManagerGrid::RenderRainParticles sets it): what its two
 * GXPosition3s16 and two GXTexCoord1s16 calls a particle write, ~10% of a
 * rainy frame on the console in calls (round hw5's profile). */
int GXPortRainLines(const short* xyz, unsigned n, short dz, unsigned short c0, unsigned short c1) {
    const Slot *ps, *cs;
    uint8_t col[2][4];
    uint32_t i, s;
    float sc;
    if (!B.open || !B.base || B.cursor != 0 || B.plan.n != 2 || B.done > B.expected ||
        B.expected - B.done < 2 * n)
        return 0;
    ps = &B.plan.slot[0];
    cs = &B.plan.slot[1];
    if (ps->attr != GX_VA_POS || ps->type != GX_DIRECT || ps->kind != SK_S16_3 || ps->dst < 0) return 0;
    if (cs->attr != GX_VA_CLR0 || cs->type != GX_DIRECT || cs->dsize != 2 || cs->fmt.type != GX_RGBA4 ||
        cs->dst < 0)
        return 0;
    for (i = 0; i < 2; i++) {   /* put16's RGBA4 decode */
        uint16_t v = i ? c1 : c0;
        col[i][0] = (uint8_t)((v >> 12 & 15) * 17);
        col[i][1] = (uint8_t)((v >> 8 & 15) * 17);
        col[i][2] = (uint8_t)((v >> 4 & 15) * 17);
        col[i][3] = (uint8_t)((v & 15) * 17);
    }
    s = B.plan.layout.stride;
    sc = ps->scale;
    /* Position and colour fill the whole vertex (12 + 4 bytes, the rain's
     * layout): every byte is stored, so no clears, and the colour is one
     * word. memset and memcpy calls per vertex were ~4% of the console's CPU
     * in the Landing Site's rain (round hw7). */
    if (s == 16 && ((ps->dst == 0 && cs->dst == 12) || (ps->dst == 4 && cs->dst == 0))) {
        uint32_t w0, w1;
        memcpy(&w0, col[0], 4);
        memcpy(&w1, col[1], 4);
        for (i = 0; i < n; i++, xyz += 3) {
            uint8_t* v = B.cur;
            float* o = (float*)(v + ps->dst);
            o[0] = xyz[0] * sc;
            o[1] = xyz[1] * sc;
            o[2] = xyz[2] * sc;
            *(uint32_t*)(v + cs->dst) = w0;
            v += 16;
            o = (float*)(v + ps->dst);
            o[0] = xyz[0] * sc;
            o[1] = xyz[1] * sc;
            o[2] = (s16)(xyz[2] + dz) * sc;   /* the game's short sum, as the call's s16 took it */
            *(uint32_t*)(v + cs->dst) = w1;
            B.cur = v + 16;
        }
        B.done += 2 * n;
        if (n && B.done < B.expected) memset(B.cur, 0, s);   /* after_attr's clear of the next vertex */
        return 1;
    }
    for (i = 0; i < n; i++, xyz += 3) {
        uint8_t* v = B.cur;
        float* o = (float*)(v + ps->dst);
        if (i) memset(v, 0, s);   /* the first was cleared when the batch began */
        o[0] = xyz[0] * sc;
        o[1] = xyz[1] * sc;
        o[2] = xyz[2] * sc;
        memcpy(v + cs->dst, col[0], 4);
        v += s;
        memset(v, 0, s);
        o = (float*)(v + ps->dst);
        o[0] = xyz[0] * sc;
        o[1] = xyz[1] * sc;
        o[2] = (s16)(xyz[2] + dz) * sc;   /* the game's short sum, as the call's s16 took it */
        memcpy(v + cs->dst, col[1], 4);
        B.cur = v + s;
    }
    B.done += 2 * n;
    if (n && B.done < B.expected) memset(B.cur, 0, s);   /* after_attr's clear of the next vertex */
    return 1;
}

void GXCallDisplayList(const void* list, u32 nbytes) {
    int pf = xhw_perf_enter(XHW_PERF_DLIST);
    gx_vtx_flush();
    if (!list || !nbytes || !dlc_call((const uint8_t*)list, nbytes)) {
        s_st_dl_direct++;
        call_display_list(list, nbytes);
    }
    xhw_perf_leave(pf);
}

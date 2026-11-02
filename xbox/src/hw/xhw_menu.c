/* xhw_menu.c - the Port Options popup's card (xhw.h "the Port Options
 * popup"; xbox/src/port/port_menu.cpp runs it, docs/ui.md describes it).
 *
 * Laid out in 480-line pixels on a 576 x 416 card and drawn at the output's
 * scale: 1x at 480 (3/4 as wide when the dashboard says 16:9, which the TV
 * stretches back), 1.5x at 720p. Everything is antialiased from distances
 * (rounded boxes, discs, rings, lines, triangles) and the text is unscii-16
 * supersampled to the scale, so 720p isn't a blown-up 480 card.
 *
 * Nothing here reads the framebuffer (write-combined: reads crawl). The card
 * is drawn in bands of BAND device rows into a small buffer in RAM, where
 * the shapes blend over each other, and each band is converted to the
 * framebuffer's format into a cache of the whole card (allocated while the
 * popup is up, freed when it closes). Each presented frame copies the cache
 * into the back buffer row by row (the opaque span of each row: the corners
 * are round). The card is drawn again only when the view changes. If the
 * cache can't be had (memory is tight in some rooms), the bands go straight
 * into every frame instead: the same picture, more CPU per frame.
 *
 * The Buttons page is N64Recomp-X's Controls page (xhw_panel_pad.c there):
 * the same controller body (a smooth union of a rounded box and two grips,
 * shaded from its distance field), the same spots and callouts, re-laid for
 * Prime's inputs; what the player holds lights up. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "xgx.h"
#include "xhw.h"
#include "xhw_internal.h"

#define CW 576   /* the card, 480-line pixels */
#define CH 416
#define BAND 16
#define MAXW 896   /* device pixels: 576 x 1.5 = 864 */
#define MAXH 640   /* 416 x 1.5 = 624 */

static xhw_menu_view s_view;
static int s_on;
static int s_sx = 16, s_sy = 16;   /* device pixels per logical pixel, x16 */
static int s_dw, s_dh, s_bpp;      /* the card in device pixels; the framebuffer's depth */
static uint8_t* s_cache;           /* s_dw x s_dh in the framebuffer's format */
static uint16_t s_span[MAXH][2];   /* each row's opaque columns [x0, x1) */
static uint32_t s_band[BAND][MAXW];   /* ARGB, straight alpha */
static int s_by0, s_by1;           /* the band's device rows */
static uint32_t* s_body;           /* the controller body at this scale (optional) */
static int s_body_w, s_body_h, s_body_ok;
static int s_logged;
/* Device pixel centres in logical 1/16 px, by column and row (no divide per
 * pixel: the P3 pays ~40 cycles for one, and the first card took 336 ms a
 * redraw on red, a stall the music skipped through) */
static int s_lx[MAXW], s_ly[MAXH];
static int s_aa, s_aa_k;           /* half a device pixel in L16; 128/s_aa in 16.16 */
/* What is still to be drawn into the cache: device rows [s_pend0, s_pend1),
 * a band at a time within XHW_MENU_BUDGET_US a frame, so a change never
 * stalls the game (its music skipped). s_ready: the cache holds a whole card. */
#ifndef XHW_MENU_BUDGET_US
#define XHW_MENU_BUDGET_US 3000
#endif
#ifndef XHW_MENU_OPEN_BUDGET_US
#define XHW_MENU_OPEN_BUDGET_US 10000   /* the first drawing: the card shows in a few frames */
#endif
static int s_pend0, s_pend1, s_ready, s_whole;   /* s_whole: the whole card again (a tab) */
/* Each glyph's coverage at this scale (0..16 a device pixel), made the first
 * time it is drawn: 16 samples a pixel once, not on every redraw */
#define GLYPHS 128
static uint8_t* s_gl[2][GLYPHS];
static uint8_t* s_gl_mem;
static int s_gl_used, s_gl_cap, s_gw[2], s_gh[2];

/* ---------------------------------------------------------------- helpers */
static uint32_t rgb(int r, int g, int b) {
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
#define R_(c) ((int)(((c) >> 16) & 0xFF))
#define G_(c) ((int)(((c) >> 8) & 0xFF))
#define B_(c) ((int)((c) & 0xFF))
#define DIV255(x) ((((x) + 128) + (((x) + 128) >> 8)) >> 8)

static uint32_t shade(uint32_t c, int k) { return rgb(R_(c) * k >> 8, G_(c) * k >> 8, B_(c) * k >> 8); }

static int isqrt(int v) { return v <= 0 ? 0 : (int)__builtin_sqrtf((float)v); }

/* col over the band pixel at alpha a (0..255) */
static void mix(uint32_t* p, uint32_t col, int a) {
    uint32_t c = *p;
    int da;
    if (a <= 0) return;
    if (a >= 255) {
        *p = 0xFF000000u | col;
        return;
    }
    da = (int)(c >> 24);
    if (!da) {
        *p = ((uint32_t)a << 24) | col;
        return;
    }
    *p = ((uint32_t)(a + DIV255(da * (255 - a))) << 24) | ((uint32_t)DIV255(R_(c) * (255 - a) + R_(col) * a) << 16) |
         ((uint32_t)DIV255(G_(c) * (255 - a) + G_(col) * a) << 8) | (uint32_t)DIV255(B_(c) * (255 - a) + B_(col) * a);
}

/* Logical coordinates in 1/16 pixel (L16). A device pixel's centre in L16,
 * and a logical box's device pixels clipped to the band */
#define lx16(px) (s_lx[px])
#define ly16(py) (s_ly[py])

typedef struct { int x0, x1, y0, y1; } Box;
static int clip(Box* b, int l16x0, int l16y0, int l16x1, int l16y1) {
    b->x0 = l16x0 * s_sx / 256 - 1;
    b->x1 = l16x1 * s_sx / 256 + 2;
    b->y0 = l16y0 * s_sy / 256 - 1;
    b->y1 = l16y1 * s_sy / 256 + 2;
    if (b->x0 < 0) b->x0 = 0;
    if (b->x1 > s_dw) b->x1 = s_dw;
    if (b->y0 < s_by0) b->y0 = s_by0;
    if (b->y1 > s_by1) b->y1 = s_by1;
    return b->x0 < b->x1 && b->y0 < b->y1;
}
#define PX(x, y) (&s_band[(y) - s_by0][x])

static int cover(int d) {   /* d: signed distance in L16, negative inside -> 0..256 */
    return d <= -s_aa ? 256 : d >= s_aa ? 0 : ((s_aa - d) * s_aa_k) >> 16;
}

/* ------------------------------------------------------------- the shapes */
/* rbox leaves out the device columns [s_hole_x0, s_hole_x1) of the rows
 * [s_hole_y0, s_hole_y1) while s_hole is set: the glass's two rims, whose
 * insides the gradient covers anyway (three full-card passes were most of a
 * redraw) */
static int s_hole, s_hole_x0, s_hole_x1, s_hole_y0, s_hole_y1;

static void hole(int x0, int y0, int x1, int y1) {   /* logical px; 0 size: none */
    int i;
    s_hole = x1 > x0;
    s_hole_x0 = s_hole_x1 = s_hole_y0 = s_hole_y1 = 0;
    if (!s_hole) return;
    for (i = 0; i < s_dw && s_lx[i] < x0 * 16; i++) {}
    s_hole_x0 = i;
    for (; i < s_dw && s_lx[i] < x1 * 16; i++) {}
    s_hole_x1 = i;
    for (i = 0; i < s_dh && s_ly[i] < y0 * 16; i++) {}
    s_hole_y0 = i;
    for (; i < s_dh && s_ly[i] < y1 * 16; i++) {}
    s_hole_y1 = i;
}

/* a rounded box in logical pixels (r: corner radius) */
static void rbox(int x, int y, int w, int h, int r, uint32_t col, int alpha) {
    Box b;
    int px, py, cx = (2 * x + w) * 8, cy = (2 * y + h) * 8, hx = w * 8 - r * 16, hy = h * 8 - r * 16;
    if (!clip(&b, x * 16, y * 16, (x + w) * 16, (y + h) * 16)) return;
    for (py = b.y0; py < b.y1; py++) {
        int qy = ly16(py) - cy, skip = s_hole && py >= s_hole_y0 && py < s_hole_y1;
        qy = (qy < 0 ? -qy : qy) - hy;
        for (px = b.x0; px < b.x1; px++) {
            if (skip && px == s_hole_x0) {
                px = s_hole_x1 - 1;
                continue;
            }
            int qx = lx16(px) - cx, d, c;
            qx = (qx < 0 ? -qx : qx) - hx;
            if (qx > 0 && qy > 0) d = isqrt(qx * qx + qy * qy) - r * 16;
            else d = (qx > qy ? qx : qy) - r * 16;
            c = cover(d);
            if (c) mix(PX(px, py), col, alpha * c >> 8);
        }
    }
}

/* a box from top colour to bottom colour, square */
static void vgrad(int x, int y, int w, int h, uint32_t top, uint32_t bot) {
    Box b;
    int px, py;
    if (!clip(&b, x * 16, y * 16, (x + w) * 16, (y + h) * 16)) return;
    for (py = b.y0; py < b.y1; py++) {
        int t = (ly16(py) - y * 16) * 256 / (h * 16), k;
        uint32_t c;
        if (t < 0) t = 0;
        if (t > 256) t = 256;
        k = 256 - t;
        c = rgb((R_(top) * k + R_(bot) * t) >> 8, (G_(top) * k + G_(bot) * t) >> 8, (B_(top) * k + B_(bot) * t) >> 8);
        if (py & 1) c = shade(c, 236);   /* the scanlines */
        if (ly16(py) < y * 16 || ly16(py) >= (y + h) * 16) continue;
        for (px = b.x0; px < b.x1; px++)
            if (lx16(px) >= x * 16 && lx16(px) < (x + w) * 16) *PX(px, py) = 0xFF000000u | c;
    }
}

/* a disc and a ring, centre and radius in L16 */
static void disc(int cx, int cy, int r, uint32_t col, int alpha) {
    Box b;
    int px, py;
    if (!clip(&b, cx - r - 16, cy - r - 16, cx + r + 16, cy + r + 16)) return;
    for (py = b.y0; py < b.y1; py++)
        for (px = b.x0; px < b.x1; px++) {
            int dx = lx16(px) - cx, dy = ly16(py) - cy, c = cover(isqrt(dx * dx + dy * dy) - r);
            if (c) mix(PX(px, py), col, alpha * c >> 8);
        }
}

static void ring(int cx, int cy, int r, int w, uint32_t col, int alpha) {
    Box b;
    int px, py, ro = r + w / 2;
    if (!clip(&b, cx - ro - 16, cy - ro - 16, cx + ro + 16, cy + ro + 16)) return;
    for (py = b.y0; py < b.y1; py++)
        for (px = b.x0; px < b.x1; px++) {
            int dx = lx16(px) - cx, dy = ly16(py) - cy, d = isqrt(dx * dx + dy * dy) - r, c;
            if (d < 0) d = -d;
            c = cover(d - w / 2);
            if (c) mix(PX(px, py), col, alpha * c >> 8);
        }
}

/* a line, ends round, w16 wide; points in L16 (floats: a 64-bit divide a
 * pixel was the Buttons page's slowest part) */
static void line(int x0, int y0, int x1, int y1, int w16, uint32_t col, int alpha) {
    Box b;
    int px, py, pad = w16 / 2 + 16;
    float dx = (float)(x1 - x0), dy = (float)(y1 - y0), len2 = dx * dx + dy * dy, inv = len2 > 0 ? 1.0f / len2 : 0;
    if (!clip(&b, (x0 < x1 ? x0 : x1) - pad, (y0 < y1 ? y0 : y1) - pad, (x0 > x1 ? x0 : x1) + pad,
              (y0 > y1 ? y0 : y1) + pad))
        return;
    for (py = b.y0; py < b.y1; py++) {
        float qy = (float)(ly16(py) - y0);
        for (px = b.x0; px < b.x1; px++) {
            float qx = (float)(lx16(px) - x0), t = (qx * dx + qy * dy) * inv, ex, ey;
            int c;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            ex = qx - t * dx, ey = qy - t * dy;
            c = cover((int)__builtin_sqrtf(ex * ex + ey * ey) - w16 / 2);
            if (c) mix(PX(px, py), col, alpha * c >> 8);
        }
    }
}

/* a triangle pointing left (dir -1) or right (+1), centre and half-height in
 * logical px: its three edges as lines facing out, the distance the largest */
static void arrow(int cx, int cy, int hh, int dir, uint32_t col, int alpha) {
    Box b;
    int px, py, i;
    float vx[3], vy[3], nx[3], ny[3], nc[3], mx, my;
    vx[0] = (float)(cx * 16 + dir * hh * 16), vy[0] = (float)(cy * 16);   /* the tip */
    vx[1] = vx[2] = (float)(cx * 16 - dir * hh * 12);
    vy[1] = (float)((cy - hh) * 16), vy[2] = (float)((cy + hh) * 16);
    mx = (vx[0] + vx[1] + vx[2]) / 3.0f, my = (vy[0] + vy[1] + vy[2]) / 3.0f;
    for (i = 0; i < 3; i++) {
        int j = (i + 1) % 3;
        float ex = vx[j] - vx[i], ey = vy[j] - vy[i], l = __builtin_sqrtf(ex * ex + ey * ey);
        nx[i] = ey / l, ny[i] = -ex / l, nc[i] = -(nx[i] * vx[i] + ny[i] * vy[i]);
        if (nx[i] * mx + ny[i] * my + nc[i] > 0) nx[i] = -nx[i], ny[i] = -ny[i], nc[i] = -nc[i];
    }
    if (!clip(&b, cx * 16 - hh * 16 - 16, (cy - hh) * 16 - 16, cx * 16 + hh * 16 + 16, (cy + hh) * 16 + 16)) return;
    for (py = b.y0; py < b.y1; py++)
        for (px = b.x0; px < b.x1; px++) {
            float x = (float)lx16(px), y = (float)ly16(py), d = -1e9f;
            int c;
            for (i = 0; i < 3; i++) {
                float e = nx[i] * x + ny[i] * y + nc[i];
                if (e > d) d = e;
            }
            c = cover((int)d);
            if (c) mix(PX(px, py), col, alpha * c >> 8);
        }
}

/* --------------------------------------------------------------- the text */
static int tw(const char* s, int z) { return (int)strlen(s) * 8 * z; }

/* unscii-16, zoom z, top-left at logical (x, y), 4 x 4 samples a pixel */
static void glyphs_sampled(const char* s, int x, int y, int z, uint32_t col, int alpha) {
    Box b;
    int px, py, n = (int)strlen(s);
    if (!clip(&b, x * 16, y * 16, (x + n * 8 * z) * 16, (y + 16 * z) * 16)) return;
    for (py = b.y0; py < b.y1; py++)
        for (px = b.x0; px < b.x1; px++) {
            int i, j, hit = 0;
            for (j = 0; j < 4; j++) {
                int ly = ((py * 16 + 2 + j * 4) * 16) / s_sy - y * 16, gy;
                if (ly < 0) continue;
                gy = ly / (16 * z);
                if (gy >= 16) continue;
                for (i = 0; i < 4; i++) {
                    int lx = ((px * 16 + 2 + i * 4) * 16) / s_sx - x * 16, gx, ch;
                    if (lx < 0) continue;
                    gx = lx / (16 * z);
                    ch = gx / 8;
                    if (ch >= n) continue;
                    if (xhw_font16[(unsigned char)s[ch] * 16 + gy] & (0x80 >> (gx & 7))) hit++;
                }
            }
            if (hit) mix(PX(px, py), col, alpha * hit / 16);
        }
}

/* glyph c's coverage at zoom z (1 or 2), made on first use; NULL without memory */
static const uint8_t* glyph(int zi, unsigned c) {
    uint8_t* g;
    int gw = s_gw[zi], gh = s_gh[zi], dx, dy, z = zi + 1;
    if (c >= GLYPHS) c = '?';
    if (s_gl[zi][c]) return s_gl[zi][c];
    if (!s_gl_mem || s_gl_used + gw * gh > s_gl_cap) return NULL;
    g = s_gl_mem + s_gl_used;
    s_gl_used += gw * gh;
    for (dy = 0; dy < gh; dy++)
        for (dx = 0; dx < gw; dx++) {
            int i, j, hit = 0;
            for (j = 0; j < 4; j++) {
                int gy = ((dy * 16 + 2 + j * 4) * 16) / s_sy / (16 * z);
                if (gy >= 16) continue;
                for (i = 0; i < 4; i++) {
                    int gx = ((dx * 16 + 2 + i * 4) * 16) / s_sx / (16 * z);
                    if (gx < 8 && (xhw_font16[c * 16 + gy] & (0x80 >> gx))) hit++;
                }
            }
            g[dy * gw + dx] = (uint8_t)hit;
        }
    s_gl[zi][c] = g;
    return g;
}

/* unscii-16 at zoom z, top-left at logical (x, y), snapped to a device pixel
 * (a glyph is a whole number of device pixels at every scale: 6, 8 or 12 wide) */
static void glyphs(const char* s, int x, int y, int z, uint32_t col, int alpha) {
    int zi = z - 1, gw = s_gw[zi], gh = s_gh[zi], ox = (x * s_sx + 8) >> 4, oy = (y * s_sy + 8) >> 4, cx = x;
    int y0 = oy < s_by0 ? s_by0 : oy, y1 = oy + gh > s_by1 ? s_by1 : oy + gh;
    if (y0 >= y1) return;
    for (; *s; s++, ox += gw, cx += 8 * z) {
        const uint8_t* g = glyph(zi, (unsigned char)*s);
        int px, py, x0 = ox < 0 ? 0 : ox, x1 = ox + gw > s_dw ? s_dw : ox + gw;
        if (!g) {   /* no memory for the cache: sampled as it is drawn */
            char one[2];
            one[0] = *s, one[1] = 0;
            glyphs_sampled(one, cx, y, z, col, alpha);
            continue;
        }
        for (py = y0; py < y1; py++) {
            const uint8_t* gr = g + (py - oy) * gw - ox;
            uint32_t* d = PX(0, py);
            for (px = x0; px < x1; px++)
                if (gr[px]) mix(&d[px], col, (alpha * gr[px]) >> 4);
        }
    }
}

static void text(const char* s, int x, int y, int z, uint32_t col) {
    glyphs(s, x + z, y + z, z, rgb(2, 4, 10), 200);   /* the shadow */
    glyphs(s, x, y, z, col, 255);
}

/* words of s wrapped to `cols` columns from line `y`, at most `max` lines;
 * returns the lines used */
static int wrap(const char* s, int x, int y, int cols, int max, uint32_t col) {
    char ln[96];
    int lines = 0;
    if (cols > (int)sizeof ln - 1) cols = (int)sizeof ln - 1;
    while (*s && lines < max) {
        int n = 0, cut = -1;
        while (s[n] && n < cols) {
            if (s[n] == ' ') cut = n;
            n++;
        }
        if (s[n] && s[n] != ' ' && cut > 0) n = cut;
        memcpy(ln, s, (size_t)n);
        ln[n] = '\0';
        text(ln, x, y + lines * 16, 1, col);
        lines++;
        s += n;
        while (*s == ' ') s++;
    }
    return lines;
}

/* ------------------------------------------------------------- the colours */
#define C_TEXT rgb(226, 234, 244)
#define C_DIM rgb(128, 150, 172)
#define C_CYAN rgb(120, 220, 250)
#define C_EDGE rgb(70, 170, 210)
#define C_WARM rgb(255, 184, 92)
#define C_SEL rgb(20, 74, 104)

/* an Xbox button's badge: a pill in its own colour with its name */
static uint32_t badge_col(const char* b) {
    if (!strcmp(b, "A")) return rgb(70, 190, 70);
    if (!strcmp(b, "B")) return rgb(220, 50, 50);
    if (!strcmp(b, "X")) return rgb(50, 110, 230);
    if (!strcmp(b, "Y")) return rgb(235, 200, 40);
    if (!strcmp(b, "BK")) return rgb(20, 20, 24);
    if (!strcmp(b, "WH")) return rgb(232, 232, 236);
    return rgb(96, 104, 118);
}

static int badge(const char* b, int x, int y) {   /* returns its width */
    uint32_t c = badge_col(b);
    int w = tw(b, 1) + 8, light = R_(c) + G_(c) + B_(c) > 420;
    rbox(x, y, w, 18, 6, c, 255);
    if (R_(c) + G_(c) + B_(c) < 120) {   /* BLACK: an outline so it reads on the dark card */
        rbox(x, y, w, 1, 0, rgb(130, 138, 150), 255);
        rbox(x, y + 17, w, 1, 0, rgb(130, 138, 150), 255);
    }
    glyphs(b, x + 4, y + 1, 1, light ? rgb(20, 22, 30) : rgb(250, 250, 255), 255);
    return w;
}

/* ------------------------------------------------------- the card's frame */
static void draw_frame(const xhw_menu_view* v) {
    int i;
    /* the glass: the rim and its dark inner edge, then the gradient with its
     * scanlines inside them */
    hole(8, 8, CW - 8, CH - 8);
    rbox(0, 0, CW, CH, 14, C_EDGE, 255);
    rbox(2, 2, CW - 4, CH - 4, 12, rgb(10, 22, 40), 255);
    hole(0, 0, 0, 0);
    vgrad(8, 8, CW - 16, CH - 16, rgb(12, 26, 46), rgb(4, 9, 20));
    /* the scan visor's corner brackets */
    for (i = 0; i < 4; i++) {
        int cx = i & 1 ? CW - 10 : 10, cy = i & 2 ? CH - 10 : 10, sx = i & 1 ? -1 : 1, sy = i & 2 ? -1 : 1;
        line(cx * 16, cy * 16, (cx + sx * 22) * 16, cy * 16, 40, C_CYAN, 230);
        line(cx * 16, cy * 16, cx * 16, (cy + sy * 22) * 16, 40, C_CYAN, 230);
    }

    /* the header */
    text("XBOX PORT OPTIONS", 26, 14, 2, C_TEXT);
    glyphs("PRIME-X", 26 + tw("XBOX PORT OPTIONS", 2) + 14, 30, 1, C_WARM, 255);
    text(v->status, CW - 26 - tw(v->status, 1), 30, 1, C_DIM);
    line(24 * 16, 54 * 16, (CW - 24) * 16, 54 * 16, 20, C_EDGE, 160);

    /* the tabs, LT and RT at the ends */
    badge("LT", 22, 63);
    badge("RT", CW - 22 - tw("RT", 1) - 8, 63);
    for (i = 0; i < v->tabs && i < XHW_MENU_TABS; i++) {
        int x0 = 58 + i * 460 / v->tabs, x1 = 58 + (i + 1) * 460 / v->tabs - 4, w = x1 - x0;
        int sel = i == v->tab;
        if (sel) {
            rbox(x0, 60, w, 24, 6, C_EDGE, 255);
            rbox(x0 + 1, 61, w - 2, 22, 5, C_SEL, 255);
            rbox(x0 + 4, 61, w - 8, 2, 1, C_CYAN, 255);
        } else {
            rbox(x0, 60, w, 24, 6, rgb(40, 64, 90), 180);
            rbox(x0 + 1, 61, w - 2, 22, 5, rgb(8, 16, 30), 255);
        }
        text(v->tab_name[i], x0 + (w - tw(v->tab_name[i], 1)) / 2, 64, 1, sel ? C_TEXT : C_DIM);
    }

    /* the footer: what the buttons do */
    {
        static const char* const k_b[] = { "A", "B", "Y" };
        const char* k_t[3];
        int x = 26;
        k_t[0] = v->page ? "Change action" : "Change";
        k_t[1] = "Close";
        k_t[2] = "Defaults";
        for (i = 0; i < 3; i++) {
            x += badge(k_b[i], x, CH - 30) + 6;
            text(k_t[i], x, CH - 29, 1, C_DIM);
            x += tw(k_t[i], 1) + 18;
        }
        /* the D-pad: a little cross */
        rbox(x + 6, CH - 30, 6, 18, 2, rgb(150, 160, 175), 255);
        rbox(x, CH - 24, 18, 6, 2, rgb(150, 160, 175), 255);
        x += 24;
        text("Select", x, CH - 29, 1, C_DIM);
    }
}

/* the box under the rows: what the selection does, what its value does, a note */
static void draw_help(const xhw_menu_view* v, int y, int h) {
    int lines;
    rbox(20, y, CW - 40, h, 8, rgb(40, 90, 120), 200);
    rbox(21, y + 1, CW - 42, h - 2, 7, rgb(6, 14, 28), 255);
    lines = wrap(v->help, 32, y + 6, 64, 3, C_TEXT);
    lines += wrap(v->value_help, 32, y + 6 + lines * 16, 64, (h - 12) / 16 - lines - (v->note[0] ? 1 : 0), C_CYAN);
    if (v->note[0]) text(v->note, 32, y + h - 22, 1, C_WARM);
}

/* ----------------------------------------------------------- the rows page */
#define ROW_Y 96
#define ROW_H 28
#define VAL_X0 336
#define VAL_X1 548
#define ROWS_FIT 6   /* rows that fit above the help box (a tab has at most 5) */

static void draw_rows(const xhw_menu_view* v) {
    int r;
    for (r = 0; r < v->rows && r < ROWS_FIT; r++) {
        int y = ROW_Y + r * ROW_H, sel = r == v->sel, f = v->flags[r], mid = (VAL_X0 + VAL_X1) / 2;
        uint32_t vc = f & XHW_MENU_ROW_CHANGED ? C_WARM : C_CYAN;
        char lab[40];
        if (sel) {
            rbox(20, y, CW - 40, ROW_H - 2, 6, C_EDGE, 255);
            rbox(21, y + 1, CW - 42, ROW_H - 4, 5, C_SEL, 255);
            rbox(22, y + 4, 4, ROW_H - 10, 2, C_CYAN, 255);
        } else if (r & 1) {
            rbox(20, y, CW - 40, ROW_H - 2, 6, rgb(14, 26, 44), 200);
        }
        strcpy(lab, v->label[r]);
        if (f & XHW_MENU_ROW_RESTART) strcat(lab, " *");
        text(lab, 36, y + 5, 1, sel ? C_TEXT : rgb(186, 200, 216));
        if (f & XHW_MENU_ROW_ACTION) {
            text(v->value[r], VAL_X1 - 8 - tw(v->value[r], 1), y + 5, 1, sel ? C_TEXT : C_DIM);
            continue;
        }
        arrow(VAL_X0 + 8, y + 12, 5, -1, sel ? C_CYAN : rgb(60, 90, 120), 255);
        arrow(VAL_X1 - 8, y + 12, 5, 1, sel ? C_CYAN : rgb(60, 90, 120), 255);
        text(v->value[r], mid - tw(v->value[r], 1) / 2, y + 3, 1, sel ? (f & XHW_MENU_ROW_CHANGED ? C_WARM : C_TEXT) : vc);
        if (v->choices[r] > 1) {   /* the pips: how many values, which one */
            int n = v->choices[r], i, x0 = mid * 16 - (n - 1) * 56;
            for (i = 0; i < n; i++)
                disc(x0 + i * 112, (y + 22) * 16, i == v->choice[r] ? 30 : 20,
                     i == v->choice[r] ? (sel ? C_CYAN : vc) : rgb(50, 74, 100), 255);
        }
        if (f & XHW_MENU_ROW_CHANGED) disc((VAL_X0 - 10) * 16, (y + 13) * 16, 36, C_WARM, 255);
    }
}

/* ------------------------------------------------------- the Buttons page */
#define CX 288   /* the controller's centre on the card */
#define CY 190
#define LW 280   /* its body's box around the centre */
#define LH 200
#define LX0 (CX - LW / 2)
#define LY0 (CY - 80)
#define BOX_W 122   /* "Morph Ball" and the changed dot */
#define BOX_H 18
#define COL_L 22
#define COL_R (CW - 22 - BOX_W)

typedef struct { int x, y, r; } Spot;
/* N64Recomp-X's spots, in this page's input order (xhw.h XHW_MENU_IN_*) */
static const Spot k_spot[XHW_MENU_IN_COUNT] = {
    /* A B X Y */ { 70, -2, 9 }, { 88, -20, 9 }, { 52, -20, 9 }, { 70, -38, 9 },
    /* WHITE BLACK */ { 50, 12, 7 }, { 70, 20, 7 },
    /* START BACK */ { 12, -8, 0 }, { -12, -8, 0 },
    /* L click, R click: on the sticks */ { -64, -18, 0 }, { 30, 32, 0 },
    /* LT RT */ { -70, -64, 0 }, { 70, -64, 0 },
    /* D-pad, left stick, right stick */ { -34, 32, 19 }, { -64, -18, 21 }, { 30, 32, 19 },
};
static const char* const k_badge[XHW_MENU_IN_COUNT] = { "A", "B", "X", "Y", "WH", "BK", "ST", "BA",
                                                        "L3", "R3", "LT", "RT", "+", "LS", "RS" };
/* the callouts: column and top */
static const struct { int8_t col; int16_t y; } k_call[XHW_MENU_IN_COUNT] = {
    /* A */ { 1, 180 }, /* B */ { 1, 158 }, /* X */ { 1, 136 }, /* Y */ { 1, 114 },
    /* WHITE */ { 1, 202 }, /* BLACK */ { 1, 224 }, /* START */ { 0, 220 }, /* BACK */ { 0, 188 },
    /* L click */ { 0, 156 }, /* R click */ { 1, 268 }, /* LT */ { 0, 92 }, /* RT */ { 1, 92 },
    /* D-pad */ { 0, 252 }, /* left stick */ { 0, 124 }, /* right stick */ { 1, 246 },
};

static int held(const xhw_menu_view* v, int in) {
    static const uint32_t k_btn[XHW_MENU_IN_MAPPABLE] = {
        XHW_BTN_A, XHW_BTN_B, XHW_BTN_X, XHW_BTN_Y, XHW_BTN_WHITE, XHW_BTN_BLACK,
        XHW_BTN_START, XHW_BTN_BACK, XHW_BTN_LSTICK, XHW_BTN_RSTICK,
    };
    if (!v->pad_ok) return 0;
    switch (in) {
    case XHW_MENU_IN_LT: return v->pad_lt > 128;
    case XHW_MENU_IN_RT: return v->pad_rt > 128;
    case XHW_MENU_IN_DPAD: return (v->pad_buttons & (XHW_BTN_UP | XHW_BTN_DOWN | XHW_BTN_LEFT | XHW_BTN_RIGHT)) != 0;
    case XHW_MENU_IN_LSTICK: return v->pad_lx * v->pad_lx + v->pad_ly * v->pad_ly > 12000 * 12000;
    case XHW_MENU_IN_RSTICK: return v->pad_rx * v->pad_rx + v->pad_ry * v->pad_ry > 12000 * 12000;
    default: return (v->pad_buttons & k_btn[in]) != 0;
    }
}

/* the body's shape (N64Recomp-X), in logical pixels around (CX, CY) */
static float fabs_(float v) { return v < 0 ? -v : v; }
static float fmax_(float a, float b) { return a > b ? a : b; }
static float fmin_(float a, float b) { return a < b ? a : b; }
static float sd_rbox(float px, float py, float hx, float hy, float r) {
    float qx = fabs_(px) - hx + r, qy = fabs_(py) - hy + r, ox = fmax_(qx, 0), oy = fmax_(qy, 0);
    return __builtin_sqrtf(ox * ox + oy * oy) + fmin_(fmax_(qx, qy), 0) - r;
}
static float sd_capsule(float px, float py, float ax, float ay, float bx, float by, float r) {
    float pax = px - ax, pay = py - ay, bax = bx - ax, bay = by - ay;
    float h = (pax * bax + pay * bay) / (bax * bax + bay * bay), dx, dy;
    h = h < 0 ? 0 : h > 1 ? 1 : h;
    dx = pax - bax * h, dy = pay - bay * h;
    return __builtin_sqrtf(dx * dx + dy * dy) - r;
}
static float smin(float a, float b, float k) {
    float h = 0.5f + 0.5f * (b - a) / k;
    h = h < 0 ? 0 : h > 1 ? 1 : h;
    return b + (a - b) * h - k * h * (1 - h);
}
static float sd_body(float x, float y) {
    float c = sd_rbox(x, y + 16, 92, 44, 36);
    float gl = sd_capsule(x, y, -74, -6, -96, 68, 30), gr = sd_capsule(x, y, 74, -6, 96, 68, 30);
    return smin(smin(c, gl, 16), gr, 16);
}
static float sd_trigger(float x, float y) {
    return fmin_(sd_rbox(x + 70, y + 60, 28, 12, 9), sd_rbox(x - 70, y + 60, 28, 12, 9));
}

/* one device pixel of the body (ARGB, alpha the coverage); px, py relative
 * to the body box's device origin */
static uint32_t body_px(int dx, int dy) {
    float s = (float)s_sx / 16.0f, t = (float)s_sy / 16.0f;
    float px = ((float)dx + 0.5f) / s + (float)(LX0 - CX), py = ((float)dy + 0.5f) / t + (float)(LY0 - CY);
    float d = sd_body(px, py), dt = sd_trigger(px, py), e = 0.5f;
    float cov = d <= -0.5f ? 1.0f : d >= 0.5f ? 0.0f : 0.5f - d;
    float tcov = dt <= -0.5f ? 1.0f : dt >= 0.5f ? 0.0f : 0.5f - dt;
    float r, g, b, a;
    if (cov > 0) {
        float gx = sd_body(px + e, py) - sd_body(px - e, py), gy = sd_body(px, py + e) - sd_body(px, py - e);
        float edge = 1.0f - fmin_(-d / 14.0f, 1.0f), lit = -(gx * -0.45f + gy * -0.89f);
        float top = 1.0f - (py + 64) / 190.0f, k;
        if (edge < 0) edge = 0;
        k = 0.78f + 0.22f * top + lit * edge * 0.55f;
        r = 44 * k, g = 49 * k, b = 56 * k;
        if (d > -2.2f) r += 30, g += 34, b += 38;   /* the rim */
        if (gy > 0.3f && edge > 0.5f) g += 26 * (edge - 0.5f) * gy, b += 8 * (edge - 0.5f) * gy;
        a = cov;
        if (tcov > 0 && cov < 1) {   /* the trigger behind the body's edge */
            r = r * cov + 80 * (1 - cov), g = g * cov + 86 * (1 - cov), b = b * cov + 94 * (1 - cov);
            a = 1.0f;
        }
    } else if (tcov > 0) {
        float k = 0.9f - (py + 72) / 40.0f * 0.3f;
        r = 70 * k, g = 76 * k, b = 84 * k;
        if (dt > -2.0f) r += 26, g += 28, b += 30;
        a = tcov;
    } else {
        float ds = sd_body(px - 4, py - 7);   /* the drop shadow */
        a = ds < 10 ? (1.0f - ds / 10.0f) * 0.45f : 0;
        if (a < 0) a = 0;
        r = g = b = 0;
    }
    return ((uint32_t)(a * 255.0f) << 24) | rgb((int)r, (int)g, (int)b);
}

static void body_cache(void) {
    int x, y;
    s_body_w = LW * s_sx / 16 + 1;
    s_body_h = LH * s_sy / 16 + 1;
    if (s_body) return;
    /* only with memory to spare: without it the body is shaded each time */
    if (xhw_mem_free_kb() < 4096 + (uint32_t)(s_body_w * s_body_h * 4 / 1024)) return;
    s_body = (uint32_t*)malloc((size_t)s_body_w * (size_t)s_body_h * 4);
    if (!s_body) return;
    for (y = 0; y < s_body_h; y++)
        for (x = 0; x < s_body_w; x++) s_body[y * s_body_w + x] = body_px(x, y);
    s_body_ok = 1;
}

static void draw_body(void) {
    Box b;
    int px, py, ox = LX0 * s_sx / 16, oy = LY0 * s_sy / 16;
    if (!clip(&b, LX0 * 16, LY0 * 16, (LX0 + LW) * 16, (LY0 + LH) * 16)) return;
    for (py = b.y0; py < b.y1; py++)
        for (px = b.x0; px < b.x1; px++) {
            int dx = px - ox, dy = py - oy;
            uint32_t c;
            if (dx < 0 || dy < 0 || dx >= s_body_w || dy >= s_body_h) continue;
            c = s_body_ok ? s_body[dy * s_body_w + dx] : body_px(dx, dy);
            if (c >> 24) mix(PX(px, py), c & 0xFFFFFFu, (int)(c >> 24));
        }
}

static void face(int x, int y, int r, uint32_t c, int lit) {
    int x16 = x * 16, y16 = y * 16;
    disc(x16 + 16, y16 + 24, r * 16 + 8, 0, 120);
    disc(x16, y16, r * 16, shade(c, lit ? 330 : 200), 255);
    disc(x16 - r * 3, y16 - r * 4, r * 9, shade(c, lit ? 400 : 290), 190);
    disc(x16 - r * 5, y16 - r * 6, r * 4, rgb(255, 255, 255), lit ? 150 : 90);
    ring(x16, y16, r * 16 - 6, 12, shade(c, 110), 200);
}

static void stick(int x, int y, int r, int lit) {
    int x16 = x * 16, y16 = y * 16;
    disc(x16, y16, r * 16, rgb(14, 16, 20), 255);
    ring(x16, y16, r * 16 - 8, 16, rgb(70, 76, 86), 200);
    disc(x16 + 24, y16 + 32, (r - 6) * 16, 0, 140);
    disc(x16, y16, (r - 6) * 16, lit ? rgb(120, 200, 130) : rgb(92, 98, 108), 255);
    disc(x16, y16, (r - 10) * 16, lit ? rgb(150, 230, 160) : rgb(72, 78, 88), 255);
    ring(x16, y16, (r - 6) * 16 - 8, 14, rgb(150, 156, 168), 120);
}

static void dpad(int x, int y, int r, uint32_t buttons, int ok) {
    int a = 6, l = 14, i;
    disc(x * 16, y * 16, r * 16, rgb(14, 16, 20), 255);
    rbox(x - a, y - l, 2 * a, 2 * l, 3, rgb(96, 102, 112), 255);
    rbox(x - l, y - a, 2 * l, 2 * a, 3, rgb(96, 102, 112), 255);
    for (i = 0; i < 4 && ok; i++) {
        static const uint32_t k_dir[4] = { XHW_BTN_UP, XHW_BTN_DOWN, XHW_BTN_LEFT, XHW_BTN_RIGHT };
        static const int8_t k_dx[4] = { 0, 0, -1, 1 }, k_dy[4] = { -1, 1, 0, 0 };
        if (buttons & k_dir[i]) rbox(x + k_dx[i] * 9 - 4, y + k_dy[i] * 9 - 4, 8, 8, 2, rgb(150, 230, 160), 255);
    }
    disc(x * 16, y * 16, 3 * 16, rgb(70, 76, 86), 255);
}

static void small(int x, int y, int lit) {   /* BACK, START */
    rbox(x - 6, y - 3, 12, 7, 3, 0, 120);
    rbox(x - 7, y - 4, 12, 7, 3, lit ? rgb(150, 230, 160) : rgb(150, 156, 168), 255);
}

static void callout(const xhw_menu_view* v, int in) {
    int col = k_call[in].col, bx = col ? COL_R : COL_L, by = k_call[in].y, sel = in == v->pad_sel;
    int fixed = in >= XHW_MENU_IN_MAPPABLE, lit = held(v, in);
    int bw = tw(k_badge[in], 1) + 8;
    uint32_t bc = badge_col(k_badge[in]), tc;
    rbox(bx - 1, by - 1, BOX_W + 2, BOX_H + 2, 7, sel ? C_CYAN : rgb(60, 74, 100), sel ? 255 : 150);
    rbox(bx, by, BOX_W, BOX_H, 6, sel ? C_SEL : rgb(10, 14, 26), 240);
    if (lit) rbox(bx, by, BOX_W, BOX_H, 6, rgb(80, 200, 110), 90);
    if (!strcmp(k_badge[in], "+") || !strcmp(k_badge[in], "LS") || !strcmp(k_badge[in], "RS") ||
        !strcmp(k_badge[in], "LT") || !strcmp(k_badge[in], "RT") || !strcmp(k_badge[in], "ST") ||
        !strcmp(k_badge[in], "BA") || !strcmp(k_badge[in], "L3") || !strcmp(k_badge[in], "R3"))
        bc = rgb(96, 104, 118);
    rbox(bx + 2, by + 2, bw, BOX_H - 4, 5, bc, 255);
    glyphs(k_badge[in], bx + 6, by + 1, 1, R_(bc) + G_(bc) + B_(bc) > 420 ? rgb(20, 22, 30) : rgb(250, 250, 255), 255);
    tc = sel ? rgb(255, 240, 160) : fixed ? C_DIM : v->pad_changed[in] ? C_WARM : C_TEXT;
    text(v->pad_map[in], bx + bw + 8, by + 1, 1, tc);
    if (v->pad_changed[in] && !fixed) disc((bx + BOX_W - 7) * 16, (by + BOX_H / 2) * 16, 40, C_WARM, 255);
}

static void draw_pad(const xhw_menu_view* v) {
    int in;
    draw_body();
    /* the jewel */
    disc(CX * 16, (CY - 36) * 16, 15 * 16, rgb(10, 12, 16), 255);
    disc(CX * 16, (CY - 36) * 16, 12 * 16, rgb(60, 160, 60), 255);
    disc((CX - 3) * 16, (CY - 40) * 16, 6 * 16, rgb(140, 220, 120), 160);
    /* the leader lines */
    for (in = 0; in < XHW_MENU_IN_COUNT; in++) {
        int col = k_call[in].col, ax = CX + k_spot[in].x, ay = CY + k_spot[in].y + (in == XHW_MENU_IN_LT || in == XHW_MENU_IN_RT ? -4 : 0);
        int by = k_call[in].y + BOX_H / 2, bx = col ? COL_R : COL_L + BOX_W, ex = col ? bx - 12 : bx + 12;
        int sel = in == v->pad_sel;
        uint32_t lc = sel ? rgb(140, 240, 220) : rgb(90, 120, 150);
        int la = sel ? 230 : 100;
        line(bx * 16, by * 16, ex * 16, by * 16, 22, lc, la);
        line(ex * 16, by * 16, ax * 16, ay * 16, 22, lc, la);
        disc(ax * 16, ay * 16, sel ? 40 : 28, lc, 255);
    }
    dpad(CX + k_spot[XHW_MENU_IN_DPAD].x, CY + k_spot[XHW_MENU_IN_DPAD].y, k_spot[XHW_MENU_IN_DPAD].r, v->pad_buttons,
         v->pad_ok);
    stick(CX + k_spot[XHW_MENU_IN_LSTICK].x, CY + k_spot[XHW_MENU_IN_LSTICK].y, k_spot[XHW_MENU_IN_LSTICK].r,
          held(v, XHW_MENU_IN_LSTICK) || held(v, XHW_MENU_IN_LCLICK));
    stick(CX + k_spot[XHW_MENU_IN_RSTICK].x, CY + k_spot[XHW_MENU_IN_RSTICK].y, k_spot[XHW_MENU_IN_RSTICK].r,
          held(v, XHW_MENU_IN_RSTICK) || held(v, XHW_MENU_IN_RCLICK));
    for (in = XHW_MENU_IN_A; in <= XHW_MENU_IN_BLACK; in++)
        face(CX + k_spot[in].x, CY + k_spot[in].y, k_spot[in].r, badge_col(k_badge[in]), held(v, in));
    small(CX + k_spot[XHW_MENU_IN_BACK].x, CY + k_spot[XHW_MENU_IN_BACK].y, held(v, XHW_MENU_IN_BACK));
    small(CX + k_spot[XHW_MENU_IN_START].x, CY + k_spot[XHW_MENU_IN_START].y, held(v, XHW_MENU_IN_START));
    for (in = XHW_MENU_IN_LT; in <= XHW_MENU_IN_RT; in++)
        if (held(v, in)) rbox(CX + k_spot[in].x - 22, CY - 68, 44, 8, 4, rgb(150, 230, 160), 200);
    /* the selected input: a ring around it */
    if (v->pad_sel >= 0 && v->pad_sel < XHW_MENU_IN_COUNT) {
        const Spot* s = &k_spot[v->pad_sel];
        int r = s->r ? s->r + 5 : 10, sx = CX + s->x, sy = CY + s->y;
        ring(sx * 16, sy * 16, r * 16, 40, rgb(120, 240, 220), 220);
        ring(sx * 16, sy * 16, r * 16 + 56, 48, rgb(60, 180, 190), 90);
    }
    for (in = 0; in < XHW_MENU_IN_COUNT; in++) callout(v, in);
}

/* ------------------------------------------------------------ one band */
static void draw_band(const xhw_menu_view* v) {
    draw_frame(v);
    if (v->page == 1) {
        draw_pad(v);
        draw_help(v, 296, 82);
    } else {
        draw_rows(v);
        draw_help(v, ROW_Y + ROWS_FIT * ROW_H + 2, CH - 36 - (ROW_Y + ROWS_FIT * ROW_H + 2));
    }
}

static uint16_t to565(uint32_t c) { return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F)); }

/* the band's rows into dst (the cache, or the frame at the card's place) */
static void band_out(uint8_t* dst, int pitch, int dst_y0) {
    int y, x;
    for (y = s_by0; y < s_by1; y++) {
        const uint32_t* src = s_band[y - s_by0];
        uint8_t* row = dst + (size_t)(y - dst_y0) * (size_t)pitch;
        int x0 = 0, x1 = s_dw;
        while (x0 < x1 && (src[x0] >> 24) < 128) x0++;
        while (x1 > x0 && (src[x1 - 1] >> 24) < 128) x1--;
        s_span[y][0] = (uint16_t)x0;
        s_span[y][1] = (uint16_t)x1;
        if (s_bpp == 16)
            for (x = x0; x < x1; x++) ((uint16_t*)row)[x] = to565(src[x]);
        else
            for (x = x0; x < x1; x++) ((uint32_t*)row)[x] = 0xFF000000u | src[x];
    }
}

/* one band (device rows y .. y + BAND) into dst, whose row 0 is the card's */
static void render_band(uint8_t* dst, int pitch, int y) {
    s_by0 = y;
    s_by1 = y + BAND > s_dh ? s_dh : y + BAND;
    memset(s_band, 0, sizeof s_band);
    draw_band(&s_view);
    band_out(dst, pitch, 0);
}

/* -------------------------------------------------------------- the API */
static void geometry(void) {
    const xhw_video_mode* vm = xhw_video();
    int i;
    if (vm->height >= 720) s_sx = s_sy = 24;
    else s_sy = 16, s_sx = vm->widescreen ? 12 : 16;
    s_dw = CW * s_sx / 16;
    s_dh = CH * s_sy / 16;
    s_bpp = vm->bpp;
    if (s_dw > MAXW) s_dw = MAXW;
    if (s_dh > MAXH) s_dh = MAXH;
    for (i = 0; i < MAXW; i++) s_lx[i] = ((i * 16 + 8) * 16) / s_sx;
    for (i = 0; i < MAXH; i++) s_ly[i] = ((i * 16 + 8) * 16) / s_sy;
    s_aa = 8 * 32 / (s_sx + s_sy);
    s_aa_k = (128 << 16) / s_aa;
    for (i = 0; i < 2; i++) {
        s_gw[i] = 8 * (i + 1) * s_sx / 16;
        s_gh[i] = 16 * (i + 1) * s_sy / 16;
    }
}

static void drop(void) {
    free(s_cache);
    s_cache = NULL;
    free(s_body);
    s_body = NULL;
    s_body_ok = 0;
    free(s_gl_mem);
    s_gl_mem = NULL;
    memset(s_gl, 0, sizeof s_gl);
    s_gl_used = s_gl_cap = 0;
}

/* logical rows [y0, y1) are to be drawn again */
static void dirty(int y0, int y1) {
    int d0 = (y0 - 2) * s_sy / 16, d1 = ((y1 + 2) * s_sy + 15) / 16;
    d0 = d0 < 0 ? 0 : d0 / BAND * BAND;
    d1 = d1 > s_dh ? s_dh : d1;
    if (s_pend0 >= s_pend1) {
        s_pend0 = d0, s_pend1 = d1;
    } else {
        if (d0 < s_pend0) s_pend0 = d0;
        if (d1 > s_pend1) s_pend1 = d1;
    }
}

#define DIFF(f) memcmp(&o->f, &v->f, sizeof v->f)
/* what changed between the view drawn (o) and the new one (v) */
static void dirty_diff(const xhw_menu_view* o, const xhw_menu_view* v) {
    int r;
    if (o->tab != v->tab || o->tabs != v->tabs || o->page != v->page || DIFF(tab_name) || DIFF(status)) {
        dirty(0, CH);
        s_whole = 1;
        return;
    }
    if (DIFF(help) || DIFF(value_help) || DIFF(note)) dirty(v->page ? 296 : ROW_Y + ROWS_FIT * ROW_H, CH - 34);
    if (v->page == 1) {
        if (o->pad_sel != v->pad_sel || DIFF(pad_map) || DIFF(pad_changed) || o->pad_ok != v->pad_ok ||
            o->pad_buttons != v->pad_buttons || o->pad_lx != v->pad_lx || o->pad_rx != v->pad_rx ||
            o->pad_lt != v->pad_lt || o->pad_rt != v->pad_rt)
            dirty(84, 296);
        return;
    }
    for (r = 0; r < ROWS_FIT; r++)
        if (o->rows != v->rows || (r == o->sel) != (r == v->sel) || DIFF(label[r]) || DIFF(value[r]) ||
            o->choices[r] != v->choices[r] || o->choice[r] != v->choice[r] || o->flags[r] != v->flags[r])
            dirty(ROW_Y + r * ROW_H, ROW_Y + (r + 1) * ROW_H);
}

void xhw_menu_show(const xhw_menu_view* v) {
    if (!v) {
        if (s_on) drop();
        s_on = 0;
        return;
    }
    if (!s_on) {
        uint32_t bytes;
        geometry();
        bytes = (uint32_t)s_dw * (uint32_t)s_dh * (uint32_t)(s_bpp / 8);
        s_cache = (uint8_t*)malloc(bytes);
        if (!s_cache && xhw_mem_reclaim(bytes / 1024 + 256, 0)) s_cache = (uint8_t*)malloc(bytes);
        s_gl_cap = GLYPHS * (s_gw[0] * s_gh[0] + s_gw[1] * s_gh[1]);
        s_gl_mem = (uint8_t*)malloc((size_t)s_gl_cap);
        if (!s_logged || !s_cache) {
            s_logged = 1;
            xhw_logf("[MENU] card %dx%d at %d-bit: %s, glyph cache %s (%u KB free)", s_dw, s_dh, s_bpp,
                     s_cache ? "cached" : "no memory for the cache, drawn each frame", s_gl_mem ? "on" : "off",
                     xhw_mem_free_kb());
        }
        s_on = 1;
        s_ready = 0;
        s_view = *v;
        s_pend0 = 0, s_pend1 = s_dh;
    } else if (memcmp(v, &s_view, sizeof *v)) {
        dirty_diff(&s_view, v);
        s_view = *v;
    }
    if (v->page == 1 && !s_body) body_cache();
}

int xhw_menu_active(void) { return s_on; }
int xhw_menu_busy(void) { return s_on && s_pend0 < s_pend1; }

/* nv2a.c: the GPU is idle and the frame finished; the card goes on it */
void xhw_menu_draw(void* fb, int w, int h, int bpp, int pitch) {
    int x0, y0, y;
    uint8_t* f = (uint8_t*)fb;
    if (!s_on || !fb || bpp != s_bpp) return;
    x0 = (w - s_dw) / 2;
    y0 = (h - s_dh) / 2;
    if (x0 < 0 || y0 < 0) return;
    if (!s_cache) {   /* no cache: drawn straight into the frame, every frame */
        uint8_t* d = f + (size_t)y0 * (size_t)pitch + (size_t)x0 * (size_t)(bpp / 8);
        for (y = 0; y < s_dh; y += BAND) render_band(d, pitch, y);
        s_pend0 = s_pend1 = 0;
        return;
    }
    if (s_pend0 < s_pend1) {   /* what changed, a band at a time, within the frame's budget */
        uint64_t t0 = xhw_time_ns();
        do {
            render_band(s_cache, s_dw * (s_bpp / 8), s_pend0);
            s_pend0 += BAND;
        } while (s_pend0 < s_pend1 &&
                 xhw_time_ns() - t0 <
                     (uint64_t)(s_ready && !s_whole ? XHW_MENU_BUDGET_US : XHW_MENU_OPEN_BUDGET_US) * 1000u);
        if (s_pend0 >= s_pend1) s_pend0 = s_pend1 = 0, s_ready = 1, s_whole = 0;
    }
    if (!s_ready) return;   /* the first drawing isn't whole yet: a few frames */
    for (y = 0; y < s_dh; y++) {
        int a = s_span[y][0], b = s_span[y][1], bytes = bpp / 8;
        if (b <= a) continue;
        memcpy(f + (size_t)(y0 + y) * (size_t)pitch + (size_t)(x0 + a) * (size_t)bytes,
               s_cache + ((size_t)y * (size_t)s_dw + (size_t)a) * (size_t)bytes, (size_t)(b - a) * (size_t)bytes);
    }
    __asm__ volatile("sfence" ::: "memory");
}

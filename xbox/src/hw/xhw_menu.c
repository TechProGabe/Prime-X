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
 * framebuffer's format straight into the frame on screen. While the popup is
 * up the game is held (port_menu.cpp's modal loop, beta2: "the port options
 * menu is heavy and laggy"): nothing is drawn or flipped, so the card stays
 * on the screen and only what changed is drawn again, a band at a time.
 * Before, the game ran on behind it, every frame waited for the GPU and
 * copied a ~1 MB cache of the card in, and a change was drawn 3 ms a frame
 * over several of the game's frames.
 *
 * The look is Prime's own pause Options (the user's pick of four mockups,
 * 2026-11-02): an amber hologram over the game (the frame behind the card is
 * read once when the popup opens, xhw_menu_capture, and shows through the
 * glass at a fifth), angular frames, and the game's Deface lettering
 * (xhw_menu_set_font) with its outline; unscii-16 if the font can't be had.
 *
 * The Buttons page is N64Recomp-X's Controls page (xhw_panel_pad.c there):
 * the controller's shape (a rounded box and two grips) as a flat amber
 * hologram, the same spots and callouts, re-laid for Prime's inputs. */
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
static uint16_t s_span[MAXH][2];   /* each row's opaque columns [x0, x1) */
static uint32_t s_band[BAND][MAXW];   /* ARGB, straight alpha */
static int s_by0, s_by1;           /* the band's device rows */
static int s_logged;
/* Device pixel centres in logical 1/16 px, by column and row (no divide per
 * pixel: the P3 pays ~40 cycles for one, and the first card took 336 ms a
 * redraw on red, a stall the music skipped through) */
static int s_lx[MAXW], s_ly[MAXH];
static int s_aa, s_aa_k;           /* half a device pixel in L16; 128/s_aa in 16.16 */
/* What is still to be drawn: device rows [s_pend0, s_pend1). The game is
 * held while the popup is up, so a change is drawn whole at the next frame
 * of the modal loop (XHW_MENU_BUDGET_US is only a guard against a stall the
 * music's disc reads would notice). */
#ifndef XHW_MENU_BUDGET_US
#define XHW_MENU_BUDGET_US 60000
#endif
static int s_pend0, s_pend1;
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
/* a rounded box in logical pixels (r: corner radius) */
static void rbox(int x, int y, int w, int h, int r, uint32_t col, int alpha) {
    Box b;
    int px, py, cx = (2 * x + w) * 8, cy = (2 * y + h) * 8, hx = w * 8 - r * 16, hy = h * 8 - r * 16;
    if (!clip(&b, x * 16, y * 16, (x + w) * 16, (y + h) * 16)) return;
    for (py = b.y0; py < b.y1; py++) {
        int qy = ly16(py) - cy;
        qy = (qy < 0 ? -qy : qy) - hy;
        for (px = b.x0; px < b.x1; px++) {
            int qx = lx16(px) - cx, d, c;
            qx = (qx < 0 ? -qx : qx) - hx;
            if (qx > 0 && qy > 0) d = isqrt(qx * qx + qy * qy) - r * 16;
            else d = (qx > qy ? qx : qy) - r * 16;
            c = cover(d);
            if (c) mix(PX(px, py), col, alpha * c >> 8);
        }
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
/* The game's own lettering (xhw_menu_set_font: the disc's Deface fonts,
 * fill and outline as the pause screen draws them), unscii-16 without it.
 * A glyph's coverage at this scale is made the first time it is drawn (4 x
 * 4 samples a device pixel, ink and outline apart) and kept while the popup
 * is up; drawing is then a copy through mix. */
static const xhw_menu_font* s_font[2];
typedef struct {
    uint8_t* cov;   /* dw * dh pairs: ink 0..16, outline 0..16; NULL: not made */
    int16_t dw, dh;
} GlyphCov;
static GlyphCov s_gc[2][95];

void xhw_menu_set_font(int which, const xhw_menu_font* f) {
    if (which < 0 || which > 1) return;
    s_font[which] = f;
    memset(s_gc[which], 0, sizeof s_gc[which]);   /* the arena is the popup's: made again */
}

/* glyph arena: allocated with the popup (xhw_menu_show), sized for both fonts */
static uint32_t gc_bytes(void) {
    uint32_t n = 0;
    int f, i;
    for (f = 0; f < 2; f++) {
        if (!s_font[f]) {
            n += GLYPHS * (uint32_t)(s_gw[f] * s_gh[f]);   /* unscii's */
            continue;
        }
        for (i = 0; i < 95; i++) {
            const xhw_menu_glyph* g = &s_font[f]->glyph[i];
            n += (uint32_t)(((g->w * s_sx + 15) / 16 + 1) * ((g->h * s_sy + 15) / 16 + 1) * 2);
        }
    }
    return n;
}

static const GlyphCov* game_glyph(int f, unsigned c) {
    const xhw_menu_glyph* g;
    GlyphCov* gc;
    int dx, dy;
    if (c < ' ' || c > '~') c = '?';
    gc = &s_gc[f][c - ' '];
    if (gc->cov) return gc;
    g = &s_font[f]->glyph[c - ' '];
    if (!g->w) return NULL;
    gc->dw = (int16_t)((g->w * s_sx + 15) / 16);
    gc->dh = (int16_t)((g->h * s_sy + 15) / 16);
    if (!s_gl_mem || s_gl_used + gc->dw * gc->dh * 2 > s_gl_cap) return NULL;
    gc->cov = s_gl_mem + s_gl_used;
    s_gl_used += gc->dw * gc->dh * 2;
    for (dy = 0; dy < gc->dh; dy++)
        for (dx = 0; dx < gc->dw; dx++) {
            int i, j, ink = 0, out = 0;
            for (j = 0; j < 4; j++) {
                int ty = ((dy * 16 + 2 + j * 4) * 16) / s_sy / 16;
                if (ty >= g->h) continue;
                for (i = 0; i < 4; i++) {
                    int tx = ((dx * 16 + 2 + i * 4) * 16) / s_sx / 16, v;
                    if (tx >= g->w) continue;
                    v = s_font[f]->cells[g->off + (uint32_t)(ty * g->w + tx)];
                    ink += v & 1;
                    out += v >> 1;
                }
            }
            gc->cov[(dy * gc->dw + dx) * 2] = (uint8_t)ink;
            gc->cov[(dy * gc->dw + dx) * 2 + 1] = (uint8_t)out;
        }
    return gc;
}

/* unscii-16 glyph c's coverage at zoom z (1 or 2), made on first use */
static const uint8_t* uglyph(int zi, unsigned c) {
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

#define C_OUTLINE rgb(22, 9, 0)

/* text width in logical px: font z - 1 (1 the text, 2 the header) */
static int tw(const char* s, int z) {
    const xhw_menu_font* f = s_font[z - 1];
    int w = 0;
    if (!f) return (int)strlen(s) * 8 * z;
    for (; *s; s++) {
        unsigned c = (unsigned char)*s;
        const xhw_menu_glyph* g = &f->glyph[(c < ' ' || c > '~' ? '?' : c) - ' '];
        w += g->a + g->b + g->c;
    }
    return w;
}

static int line_h(int z) { return s_font[z - 1] ? s_font[z - 1]->height : 16 * z; }

/* s with its top-left at logical (x, y), font z - 1, ink col */
static void text_a(const char* s, int x, int y, int z, uint32_t col, int alpha) {
    const xhw_menu_font* f = s_font[z - 1];
    if (!f) {   /* unscii: a shadow, then the ink */
        int pass;
        for (pass = 0; pass < 2; pass++) {
            int zi = z - 1, gw = s_gw[zi], gh = s_gh[zi], o = pass ? 0 : z;
            int ox = ((x + o) * s_sx + 8) >> 4, oy = ((y + o) * s_sy + 8) >> 4;
            int y0 = oy < s_by0 ? s_by0 : oy, y1 = oy + gh > s_by1 ? s_by1 : oy + gh;
            const char* p;
            uint32_t c = pass ? col : C_OUTLINE;
            int a = pass ? alpha : alpha * 200 / 255;
            if (y0 >= y1) return;
            for (p = s; *p; p++, ox += gw) {
                const uint8_t* g = uglyph(zi, (unsigned char)*p);
                int px, py, x0 = ox < 0 ? 0 : ox, x1 = ox + gw > s_dw ? s_dw : ox + gw;
                if (!g) continue;
                for (py = y0; py < y1; py++) {
                    const uint8_t* gr = g + (py - oy) * gw - ox;
                    uint32_t* d = PX(0, py);
                    for (px = x0; px < x1; px++)
                        if (gr[px]) mix(&d[px], c, (a * gr[px]) >> 4);
                }
            }
        }
        return;
    }
    {
        int cx = x, base = y + f->ascent;
        for (; *s; s++) {
            unsigned c = (unsigned char)*s;
            const xhw_menu_glyph* g = &f->glyph[(c < ' ' || c > '~' ? '?' : c) - ' '];
            const GlyphCov* gc;
            cx += g->a;
            if (g->w && (gc = game_glyph(z - 1, c)) != NULL) {
                int ox = (cx * s_sx + 8) >> 4, oy = ((base - g->top) * s_sy + 8) >> 4;
                int y0 = oy < s_by0 ? s_by0 : oy, y1 = oy + gc->dh > s_by1 ? s_by1 : oy + gc->dh;
                int x0 = ox < 0 ? 0 : ox, x1 = ox + gc->dw > s_dw ? s_dw : ox + gc->dw, px, py;
                for (py = y0; py < y1; py++) {
                    const uint8_t* gr = gc->cov + ((py - oy) * gc->dw - ox) * 2;
                    uint32_t* d = PX(0, py);
                    for (px = x0; px < x1; px++) {
                        int o = gr[px * 2 + 1], i = gr[px * 2];
                        if (o) mix(&d[px], C_OUTLINE, (alpha * o) >> 4);
                        if (i) mix(&d[px], col, (alpha * i) >> 4);
                    }
                }
            }
            cx += g->b + g->c;
        }
    }
}

static void text(const char* s, int x, int y, int z, uint32_t col) { text_a(s, x, y, z, col, 255); }

/* words of s wrapped to `maxw` logical px from line `y`, at most `max`
 * lines; returns the lines used */
static int wrap(const char* s, int x, int y, int maxw, int max, uint32_t col) {
    char ln[160];
    int lines = 0, lh = line_h(1);
    while (*s && lines < max) {
        int n = 0, cut = -1;
        while (s[n] && n < (int)sizeof ln - 1) {
            if (s[n] == ' ') {
                memcpy(ln, s, (size_t)n);
                ln[n] = '\0';
                if (tw(ln, 1) > maxw) break;
                cut = n;
            }
            n++;
        }
        if (!s[n] || n >= (int)sizeof ln - 1) {   /* the rest fits, or a last word */
            memcpy(ln, s, (size_t)n);
            ln[n] = '\0';
            if (tw(ln, 1) > maxw && cut > 0) n = cut;
        } else if (cut > 0) {
            n = cut;
        }
        memcpy(ln, s, (size_t)n);
        ln[n] = '\0';
        text(ln, x, y + lines * lh, 1, col);
        lines++;
        s += n;
        while (*s == ' ') s++;
    }
    return lines;
}

/* ------------------------------------------------------------- the colours */
/* Prime's own pause Options (the user picked mockup A, 2026-11-02): an amber
 * hologram over the game, angular frames, warm lettering */
#define C_AMBER rgb(255, 150, 40)
#define C_TEXT rgb(255, 228, 176)      /* selected, and the help's first line */
#define C_LABEL rgb(236, 148, 54)      /* a row's label */
#define C_VALUE rgb(255, 182, 84)
#define C_DIM rgb(176, 98, 32)
#define C_HELP rgb(255, 164, 64)
#define C_MARK rgb(255, 238, 150)      /* not the default: pale yellow */
#define C_TINT rgb(62, 30, 4)          /* the glass over the game */
#define C_SEL_FILL rgb(140, 70, 8)
#define C_BOX_FILL rgb(40, 20, 2)
#define C_DEEP rgb(28, 13, 0)

/* an axis-aligned box in logical px, solid (no rounding: cheap) */
static void fill(int x, int y, int w, int h, uint32_t col, int alpha) {
    int x0 = (x * s_sx + 8) >> 4, x1 = ((x + w) * s_sx + 8) >> 4, y0 = (y * s_sy + 8) >> 4, y1 = ((y + h) * s_sy + 8) >> 4;
    int px, py;
    if (x1 <= x0) x1 = x0 + 1;
    if (y1 <= y0) y1 = y0 + 1;
    if (x0 < 0) x0 = 0;
    if (x1 > s_dw) x1 = s_dw;
    if (y0 < s_by0) y0 = s_by0;
    if (y1 > s_by1) y1 = s_by1;
    for (py = y0; py < y1; py++) {
        uint32_t* d = PX(0, py);
        if (alpha >= 255)
            for (px = x0; px < x1; px++) d[px] = 0xFF000000u | col;
        else
            for (px = x0; px < x1; px++) mix(&d[px], col, alpha);
    }
}

/* A box with its corners cut by `c` logical px: a fill (row spans, cheap)
 * and the eight edges of its outline. fa or la 0: none. */
static void cbox(int x, int y, int w, int h, int c, uint32_t fc, int fa, uint32_t lc, int la, int w16) {
    if (fa > 0) {
        int py, y0 = (y * s_sy + 8) >> 4, y1 = ((y + h) * s_sy + 8) >> 4;
        if (y0 < s_by0) y0 = s_by0;
        if (y1 > s_by1) y1 = s_by1;
        for (py = y0; py < y1; py++) {
            int ly = ly16(py) - y * 16, cut = 0, x0, x1, px;   /* L16 below the top */
            uint32_t* d = PX(0, py);
            if (ly < c * 16) cut = c * 16 - ly;
            else if (ly > (h - c) * 16) cut = ly - (h - c) * 16;
            x0 = ((x * 16 + cut) * s_sx + 128) >> 8;
            x1 = (((x + w) * 16 - cut) * s_sx + 128) >> 8;
            if (x0 < 0) x0 = 0;
            if (x1 > s_dw) x1 = s_dw;
            if (fa >= 255)
                for (px = x0; px < x1; px++) d[px] = 0xFF000000u | fc;
            else
                for (px = x0; px < x1; px++) mix(&d[px], fc, fa);
        }
    }
    if (la > 0) {
        int l = x * 16, r = (x + w) * 16, t = y * 16, b = (y + h) * 16, k = c * 16;
        line(l + k, t, r - k, t, w16, lc, la);
        line(l + k, b, r - k, b, w16, lc, la);
        line(l, t + k, l, b - k, w16, lc, la);
        line(r, t + k, r, b - k, w16, lc, la);
        line(l, t + k, l + k, t, w16, lc, la);
        line(r - k, t, r, t + k, w16, lc, la);
        line(l, b - k, l + k, b, w16, lc, la);
        line(r - k, b, r, b - k, w16, lc, la);
    }
}

/* an Xbox button's badge: its colour, its name */
static uint32_t badge_col(const char* b) {
    if (!strcmp(b, "A")) return rgb(70, 190, 70);
    if (!strcmp(b, "B")) return rgb(220, 50, 50);
    if (!strcmp(b, "X")) return rgb(50, 110, 230);
    if (!strcmp(b, "Y")) return rgb(235, 200, 40);
    if (!strcmp(b, "BK")) return rgb(20, 20, 24);
    if (!strcmp(b, "WH")) return rgb(232, 232, 236);
    return rgb(120, 70, 20);
}

static int badge(const char* b, int x, int y) {   /* returns its width */
    uint32_t c = badge_col(b);
    int w = tw(b, 1) + 10, light = R_(c) + G_(c) + B_(c) > 420;
    rbox(x, y, w, 17, 8, c, 255);
    if (R_(c) + G_(c) + B_(c) < 120) {   /* BLACK: a rim so it reads on the glass */
        fill(x + 4, y, w - 8, 1, C_DIM, 255);
        fill(x + 4, y + 16, w - 8, 1, C_DIM, 255);
    }
    text(b, x + 5, y + (17 - line_h(1)) / 2, 1, light ? rgb(20, 22, 30) : rgb(255, 250, 240));
    return w;
}

/* ------------------------------------------------------- the card's frame */
#define CUT 16   /* the card's cut corners, logical px */

/* What was on the screen behind the card when the popup opened (one read of
 * the frame, xhw_menu_capture): the glass is that, darkened and tinted, so
 * the card is a hologram over the game as the pause screen is. NULL (no
 * memory): an opaque amber glass. */
static uint8_t* s_back;
static int s_back_bpp;

void xhw_menu_capture(const void* fb, int w, int h, int bpp, int pitch) {
    int x0 = (w - s_dw) / 2, y0 = (h - s_dh) / 2, y, bytes = bpp / 8;
    uint32_t size = (uint32_t)s_dw * (uint32_t)s_dh * (uint32_t)bytes;
    if (!s_on || !fb || x0 < 0 || y0 < 0) return;
    if (!s_back || s_back_bpp != bpp) {
        free(s_back);
        s_back = (uint8_t*)malloc(size);
        if (!s_back && xhw_mem_reclaim(size / 1024 + 256, 0)) s_back = (uint8_t*)malloc(size);
        s_back_bpp = bpp;
        if (!s_back) {
            xhw_logf("[MENU] no memory for the glass's backdrop (%u KB): opaque", size / 1024);
            return;
        }
    }
    for (y = 0; y < s_dh; y++)   /* write-combined memory: a row at a time, once */
        memcpy(s_back + (size_t)y * (size_t)s_dw * (size_t)bytes,
               (const uint8_t*)fb + (size_t)(y0 + y) * (size_t)pitch + (size_t)x0 * (size_t)bytes,
               (size_t)s_dw * (size_t)bytes);
}

/* the glass: each row of the card, its ends cut at the corners; the game
 * behind at 22%, the amber tint over it, Prime's scanlines every third row */
static void glass(void) {
    int py;
    const int tr = R_(C_TINT) * 200, tg = G_(C_TINT) * 200, tb = B_(C_TINT) * 200;
    for (py = s_by0; py < s_by1; py++) {
        int ly = ly16(py), c = 0, x0, x1, px, k = (py % 3) == 0 ? 44 : 56;   /* the game's share, /256 */
        uint32_t* d = PX(0, py);
        if (ly < CUT * 16) c = CUT * 16 - ly;
        else if (ly > (CH - CUT) * 16) c = ly - (CH - CUT) * 16;
        x0 = (c * s_sx + 128) >> 8;
        x1 = ((CW * 16 - c) * s_sx + 128) >> 8;
        if (x1 > s_dw) x1 = s_dw;
        if (!s_back) {
            uint32_t col = rgb((tr + 10 * k) / 255, (tg + 6 * k) / 255, (tb + 2 * k) / 255);
            for (px = x0; px < x1; px++) d[px] = 0xFF000000u | col;
            continue;
        }
        if (s_back_bpp == 16) {
            const uint16_t* s = (const uint16_t*)s_back + (size_t)py * (size_t)s_dw;
            for (px = x0; px < x1; px++) {
                uint32_t v = s[px];
                int r = (int)((v >> 8) & 0xF8), g = (int)((v >> 3) & 0xFC), b = (int)((v << 3) & 0xF8);
                d[px] = 0xFF000000u | (uint32_t)((tr + r * k) >> 8) << 16 | (uint32_t)((tg + g * k) >> 8) << 8 |
                        (uint32_t)((tb + b * k) >> 8);
            }
        } else {
            const uint32_t* s = (const uint32_t*)s_back + (size_t)py * (size_t)s_dw;
            for (px = x0; px < x1; px++) {
                uint32_t v = s[px];
                d[px] = 0xFF000000u | (uint32_t)((tr + R_(v) * k) >> 8) << 16 | (uint32_t)((tg + G_(v) * k) >> 8) << 8 |
                        (uint32_t)((tb + B_(v) * k) >> 8);
            }
        }
    }
}

static void draw_frame(const xhw_menu_view* v) {
    int i;
    glass();
    cbox(0, 0, CW, CH, CUT, 0, 0, C_AMBER, 255, 32);
    cbox(6, 6, CW - 12, CH - 12, CUT - 4, 0, 0, C_AMBER, 90, 20);

    /* the header: "[ Xbox Port Options ]", as the pause screen's "[ Options ]" */
    {
        const char* title = "[ Xbox Port Options ]";
        text(title, (CW - tw(title, 2)) / 2, 8, 2, C_AMBER);
        /* the mode now: right, under the title's line (beside it, the
         * Deface title left it no room) */
        text(v->status, CW - 26 - tw(v->status, 1), 36, 1, C_DIM);
    }

    /* the tabs between LT and RT */
    badge("LT", 20, 62);
    badge("RT", CW - 20 - (tw("RT", 1) + 10), 62);
    {   /* each tab as wide as its name, the spare room shared out */
        const int n = v->tabs < XHW_MENU_TABS ? v->tabs : XHW_MENU_TABS, left = 58, right = CW - 58, gap = 4;
        int widths = 0, extra, x0 = left;
        for (i = 0; i < n; i++) widths += tw(v->tab_name[i], 1);
        extra = n ? (right - left - widths - gap * (n - 1)) / n : 0;
        if (extra < 6) extra = 6;
        for (i = 0; i < n; i++) {
            int w = tw(v->tab_name[i], 1) + extra, sel = i == v->tab;
            cbox(x0, 56, w, 28, 6, sel ? C_SEL_FILL : C_BOX_FILL, sel ? 220 : 150, C_AMBER, sel ? 255 : 110, 20);
            text(v->tab_name[i], x0 + extra / 2, 56 + (28 - line_h(1)) / 2, 1, sel ? C_TEXT : C_LABEL);
            x0 += w + gap;
        }
    }

    /* the footer: what the buttons do */
    {
        /* the Buttons page: A and X step the action on and back (left and
         * right move between the columns there) */
        static const char* const k_rows_b[] = { "A", "B", "Y" };
        static const char* const k_rows_t[] = { "Change", "Close", "Defaults" };
        static const char* const k_pad_b[] = { "A", "X", "B", "Y" };
        static const char* const k_pad_t[] = { "Next", "Previous", "Close", "Defaults" };
        const char* const* k_b = v->page ? k_pad_b : k_rows_b;
        const char* const* k_t = v->page ? k_pad_t : k_rows_t;
        const int n = v->page ? 4 : 3, fy = CH - 30, ty = fy + (17 - line_h(1)) / 2;
        int x = 28;
        for (i = 0; i < n; i++) {
            x += badge(k_b[i], x, fy) + 6;
            text(k_t[i], x, ty, 1, C_LABEL);
            x += tw(k_t[i], 1) + 18;
        }
        /* the D-pad: a little cross */
        fill(x + 6, fy, 5, 17, C_AMBER, 255);
        fill(x, fy + 6, 17, 5, C_AMBER, 255);
        x += 24;
        text("Select", x, ty, 1, C_LABEL);
    }
}

/* the box under the rows: what the selection does, what its value does, a note */
static void draw_help(const xhw_menu_view* v, int y, int h) {
    int lines, lh = line_h(1), max = (h - 10) / lh;
    cbox(26, y, CW - 52, h, 8, C_DEEP, 170, C_AMBER, 150, 20);
    lines = wrap(v->help, 40, y + 6, CW - 80, max - (v->note[0] ? 1 : 0), C_TEXT);
    lines += wrap(v->value_help, 40, y + 6 + lines * lh, CW - 80, max - lines - (v->note[0] ? 1 : 0), C_HELP);
    if (v->note[0]) text(v->note, 40, y + 6 + lines * lh, 1, C_MARK);   /* right under: never over a line */
}

/* ----------------------------------------------------------- the rows page */
#define ROW_Y 92
#define ROW_H 30
#define VAL_X0 330
#define VAL_X1 524
#define ROWS_FIT 6   /* rows that fit above the help box (a tab has at most 6) */

static void draw_rows(const xhw_menu_view* v) {
    int r, lh = line_h(1);
    for (r = 0; r < v->rows && r < ROWS_FIT; r++) {
        int y = ROW_Y + r * ROW_H, sel = r == v->sel, f = v->flags[r], mid = (VAL_X0 + VAL_X1) / 2;
        int ty = y + (ROW_H - 4 - lh) / 2;
        uint32_t vc = f & XHW_MENU_ROW_CHANGED ? C_MARK : C_VALUE;
        char lab[40];
        cbox(26, y, CW - 52, ROW_H - 4, 6, sel ? C_SEL_FILL : C_BOX_FILL, sel ? 210 : 110, C_AMBER, sel ? 255 : 80, 20);
        strcpy(lab, v->label[r]);
        if (f & XHW_MENU_ROW_RESTART) strcat(lab, " *");
        text(lab, 40, ty, 1, sel ? C_TEXT : C_LABEL);
        if (f & XHW_MENU_ROW_ACTION) {
            text(v->value[r], VAL_X1 - tw(v->value[r], 1), ty, 1, sel ? C_TEXT : C_VALUE);
            continue;
        }
        arrow(VAL_X0 + 6, y + (ROW_H - 4) / 2, 5, -1, C_AMBER, 255);
        arrow(VAL_X1 + 6, y + (ROW_H - 4) / 2, 5, 1, C_AMBER, 255);
        text(v->value[r], mid + 6 - tw(v->value[r], 1) / 2, ty - 2, 1, sel ? (f & XHW_MENU_ROW_CHANGED ? C_MARK : rgb(255, 255, 255)) : vc);
        if (v->choices[r] > 1) {   /* the pips: how many values, which one */
            int n = v->choices[r], i, x0 = mid + 6 - (n - 1) * 5 - 2;
            for (i = 0; i < n; i++)
                fill(x0 + i * 10, y + ROW_H - 9, 4, 2, i == v->choice[r] ? (sel ? rgb(255, 255, 255) : C_AMBER) : C_DIM, 255);
        }
        if (f & XHW_MENU_ROW_CHANGED) fill(VAL_X0 - 14, y + 10, 5, 5, C_MARK, 255);
    }
}

/* ------------------------------------------------------- the Buttons page */
#define CX 288   /* the controller's centre on the card */
#define CY 190
#define BOX_W 140   /* "Morph Ball" in Deface and the changed mark */
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

/* The controller: a flat black silhouette with an amber rim (N64Recomp-X's
 * shape: a rounded box and two grips), solid so it stands out from the glass
 * (the user, 2026-11-02: "make the controller black ... easier to see"),
 * drawn from a few shapes each time, no shading and no cache */
static void hologram(void) {
    const uint32_t rim = C_AMBER, body = rgb(14, 14, 16);
    int pass;
    for (pass = 0; pass < 2; pass++) {   /* the rims, then the bodies over them */
        int g = pass ? 0 : 2;
        uint32_t c = pass ? body : rim;
        rbox(CX - 98 - g, CY - 72 - g, 56 + 2 * g, 24 + 2 * g, 9 + g, c, 255);   /* the triggers */
        rbox(CX + 42 - g, CY - 72 - g, 56 + 2 * g, 24 + 2 * g, 9 + g, c, 255);
    }
    for (pass = 0; pass < 2; pass++) {
        int g = pass ? 0 : 2;
        uint32_t c = pass ? body : rim;
        rbox(CX - 92 - g, CY - 60 - g, 184 + 2 * g, 88 + 2 * g, 36 + g, c, 255);
        line((CX - 74) * 16, (CY - 6) * 16, (CX - 96) * 16, (CY + 68) * 16, (60 + 2 * g) * 16, c, 255);
        line((CX + 74) * 16, (CY - 6) * 16, (CX + 96) * 16, (CY + 68) * 16, (60 + 2 * g) * 16, c, 255);
    }
}

static void face(int x, int y, int r, uint32_t c) {
    disc(x * 16, y * 16, r * 16, shade(c, 210), 255);
    ring(x * 16, y * 16, r * 16 - 6, 12, shade(c, 330), 220);
}

static void stick(int x, int y, int r) {
    disc(x * 16, y * 16, r * 16, rgb(4, 4, 6), 255);
    ring(x * 16, y * 16, r * 16 - 8, 16, rgb(110, 110, 118), 230);
    disc(x * 16, y * 16, (r - 8) * 16, rgb(64, 64, 70), 255);
}

static void dpad(int x, int y) {
    fill(x - 5, y - 13, 10, 26, rgb(96, 96, 104), 255);
    fill(x - 13, y - 5, 26, 10, rgb(96, 96, 104), 255);
}

static void callout(const xhw_menu_view* v, int in) {
    int col = k_call[in].col, bx = col ? COL_R : COL_L, by = k_call[in].y, sel = in == v->pad_sel;
    int fixed = in >= XHW_MENU_IN_MAPPABLE, bw = tw(k_badge[in], 1) + 10;
    uint32_t tc;
    cbox(bx, by, BOX_W, BOX_H, 4, sel ? C_SEL_FILL : C_BOX_FILL, sel ? 230 : 190, C_AMBER, sel ? 255 : 100, 16);
    badge(k_badge[in], bx + 3, by);
    tc = sel ? C_TEXT : fixed ? C_DIM : v->pad_changed[in] ? C_MARK : C_VALUE;
    text(v->pad_map[in], bx + bw + 8, by + (BOX_H - line_h(1)) / 2, 1, tc);
    if (v->pad_changed[in] && !fixed) fill(bx + BOX_W - 10, by + 7, 4, 4, C_MARK, 255);
}

static void draw_pad(const xhw_menu_view* v) {
    int in;
    hologram();
    /* the leader lines */
    for (in = 0; in < XHW_MENU_IN_COUNT; in++) {
        int col = k_call[in].col, ax = CX + k_spot[in].x, ay = CY + k_spot[in].y + (in == XHW_MENU_IN_LT || in == XHW_MENU_IN_RT ? -4 : 0);
        int by = k_call[in].y + BOX_H / 2, bx = col ? COL_R : COL_L + BOX_W, ex = col ? bx - 12 : bx + 12;
        int sel = in == v->pad_sel;
        uint32_t lc = sel ? rgb(255, 230, 160) : C_AMBER;
        int la = sel ? 255 : 110;
        line(bx * 16, by * 16, ex * 16, by * 16, 20, lc, la);
        line(ex * 16, by * 16, ax * 16, ay * 16, 20, lc, la);
        disc(ax * 16, ay * 16, sel ? 40 : 26, lc, 255);
    }
    dpad(CX + k_spot[XHW_MENU_IN_DPAD].x, CY + k_spot[XHW_MENU_IN_DPAD].y);
    stick(CX + k_spot[XHW_MENU_IN_LSTICK].x, CY + k_spot[XHW_MENU_IN_LSTICK].y, k_spot[XHW_MENU_IN_LSTICK].r);
    stick(CX + k_spot[XHW_MENU_IN_RSTICK].x, CY + k_spot[XHW_MENU_IN_RSTICK].y, k_spot[XHW_MENU_IN_RSTICK].r);
    for (in = XHW_MENU_IN_A; in <= XHW_MENU_IN_BLACK; in++)
        face(CX + k_spot[in].x, CY + k_spot[in].y, k_spot[in].r, badge_col(k_badge[in]));
    fill(CX + k_spot[XHW_MENU_IN_BACK].x - 6, CY + k_spot[XHW_MENU_IN_BACK].y - 3, 12, 6, rgb(150, 150, 158), 255);
    fill(CX + k_spot[XHW_MENU_IN_START].x - 6, CY + k_spot[XHW_MENU_IN_START].y - 3, 12, 6, rgb(150, 150, 158), 255);
    /* the selected input: a ring around it */
    if (v->pad_sel >= 0 && v->pad_sel < XHW_MENU_IN_COUNT) {
        const Spot* s = &k_spot[v->pad_sel];
        int r = s->r ? s->r + 5 : 10, sx = CX + s->x, sy = CY + s->y;
        ring(sx * 16, sy * 16, r * 16, 36, rgb(255, 230, 160), 240);
    }
    for (in = 0; in < XHW_MENU_IN_COUNT; in++) callout(v, in);
}

/* ------------------------------------------------------------ one band */
static void draw_band(const xhw_menu_view* v) {
    draw_frame(v);
    if (v->page == 1) {
        draw_pad(v);
        draw_help(v, 296, CH - 38 - 296);
    } else {
        draw_rows(v);
        draw_help(v, ROW_Y + ROWS_FIT * ROW_H + 2, CH - 38 - (ROW_Y + ROWS_FIT * ROW_H + 2));
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
    free(s_back);
    s_back = NULL;
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
        return;
    }
    if (DIFF(help) || DIFF(value_help) || DIFF(note)) dirty(v->page ? 296 : ROW_Y + ROWS_FIT * ROW_H, CH - 34);
    if (v->page == 1) {
        int in;
        if (o->pad_ok != v->pad_ok) {
            dirty(84, 296);
            return;
        }
        /* a move or a change: the two callouts' rows and the two spots on
         * the controller, not the whole page (the body is drawn from shapes) */
        for (in = 0; in < XHW_MENU_IN_COUNT; in++) {
            if ((in == o->pad_sel) != (in == v->pad_sel) || DIFF(pad_map[in]) ||
                o->pad_changed[in] != v->pad_changed[in]) {
                const Spot* sp = &k_spot[in];
                int r = (sp->r ? sp->r + 5 : 10) + 4;
                dirty(k_call[in].y - 2, k_call[in].y + BOX_H + 2);
                dirty(CY + sp->y - r, CY + sp->y + r);
                /* its leader line runs between the two */
                dirty(k_call[in].y < CY + sp->y ? k_call[in].y : CY + sp->y,
                      (k_call[in].y > CY + sp->y ? k_call[in].y : CY + sp->y) + BOX_H);
            }
        }
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
        geometry();
        s_gl_cap = (int)gc_bytes();
        s_gl_mem = (uint8_t*)malloc((size_t)s_gl_cap);
        if (!s_logged) {
            s_logged = 1;
            xhw_logf("[MENU] card %dx%d at %d-bit, glyph cache %s (%u KB free)", s_dw, s_dh, s_bpp,
                     s_gl_mem ? "on" : "off", xhw_mem_free_kb());
        }
        s_on = 1;
        s_view = *v;
        s_pend0 = 0, s_pend1 = s_dh;
    } else if (memcmp(v, &s_view, sizeof *v)) {
        dirty_diff(&s_view, v);
        s_view = *v;
    }
}

int xhw_menu_active(void) { return s_on; }
int xhw_menu_busy(void) { return s_on && s_pend0 < s_pend1; }
void xhw_menu_redraw(void) { if (s_on) s_pend0 = 0, s_pend1 = s_dh; }

/* What changed, straight into `fb` at the card's place (the frame on screen,
 * the game held: port_menu.cpp's modal loop). Returns the microseconds it took. */
unsigned xhw_menu_draw_changes(void* fb, int w, int h, int bpp, int pitch) {
    uint64_t t0 = xhw_time_ns();
    uint8_t* d;
    int x0, y0;
    if (!s_on || !fb || bpp != s_bpp || s_pend0 >= s_pend1) return 0;
    x0 = (w - s_dw) / 2;
    y0 = (h - s_dh) / 2;
    if (x0 < 0 || y0 < 0) return 0;
    d = (uint8_t*)fb + (size_t)y0 * (size_t)pitch + (size_t)x0 * (size_t)(bpp / 8);
    do {
        render_band(d, pitch, s_pend0);
        s_pend0 += BAND;
    } while (s_pend0 < s_pend1 && xhw_time_ns() - t0 < (uint64_t)XHW_MENU_BUDGET_US * 1000u);
    if (s_pend0 >= s_pend1) s_pend0 = s_pend1 = 0;
    __asm__ volatile("sfence" ::: "memory");
    return (unsigned)((xhw_time_ns() - t0) / 1000u);
}

/* nv2a.c: a presented frame while the card is up, which the modal loop
 * leaves none of; should one come, the whole card goes on it */
void xhw_menu_draw(void* fb, int w, int h, int bpp, int pitch) {
    if (!s_on) return;
    s_pend0 = 0, s_pend1 = s_dh;
    xhw_menu_draw_changes(fb, w, h, bpp, pitch);
}

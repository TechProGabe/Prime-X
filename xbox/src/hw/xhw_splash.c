/* xhw_splash.c - the boot screen: a haunted CRT terminal (docs/ui.md "Boot
 * splash", the user's "E", 2026-10-27), after Melee-X's title card (from
 * OpenCrossing-Xbox) and N64Recomp-X's loading screen.
 *
 *   TechProGabe Presents...           small, above
 *   PRIME-X                           the headline, typed on, chromatic
 *                                     fringes, a block cursor
 *   METROID PRIME - GM8E01 - USA 1.00
 *   ~~~ a signal trace ~~~            twitching now and then
 *   VERIFYING DISC IMAGE   27%        what it is doing, the second line
 *   [segmented bar]
 *   > Metroid4.pak ... CRC ... OK     a detail line and a hex dump (the
 *                                     first boot's disc check)
 *   notes; the safe-video hint
 * round it: glyph rain in phosphor green, a boot log typed top left with
 * what the boot found (CPU, memory, disc, checks), a red // SIGNAL tag,
 * scan brackets, and CRT scanlines, dark corners and a passing glitch band.
 *
 * Drawn by the CPU into the linear framebuffer XVideoSetMode() hands back,
 * before the renderer takes the display: no GPU state, no assets. Cheap on
 * purpose (it runs beside the disc check): the glass, the card's panel and
 * a per-pixel shade (scanlines, corners) are made once; each frame copies
 * that, draws only what moves through the shade, and is copied out, ~25 fps
 * from a normal-priority thread. No XVideoWaitForVBlank (it hooks the GPU
 * interrupt and pb_init then fails with -4, OpenCrossing's finding).
 *
 *   xhw_splash_show()             the mode and the thread; returns at once
 *   xhw_splash_log(line)          a boot-log line (up to SPLASH_LOG_LINES)
 *   xhw_splash_stage(t, n1, n2)   the task line and two notes under the bar
 *   xhw_splash_detail(l1, l2)     the detail line and the hex line
 *   xhw_splash_progress(f)        the bar, 0..1; it fills towards f at least
 *                                 BAR_FILL_MS for the whole width, so steps
 *                                 read as a load, not a jump
 *   xhw_splash_skip_pressed()     B pressed (polled by the boot thread)
 *   xhw_splash_release()          the minimum hold (any button skips) and
 *                                 the bar full for a beat, then the thread
 *                                 stops: the mode is changing
 *   xhw_splash_card(t, lines)     an error card in the same dress (orange)
 *
 * Font: unscii-16 (public domain), the 8x16 face nxdk's debug console uses.
 * Kill switch -DXHW_NO_SPLASH; minimum hold -DXHW_SPLASH_MS=<n>;
 * -DXHW_SPLASH_DUMP: [FBDUMP]s of the settled screen and of a quarter-done
 * stage. Mock-ups: tools/xbox/splash_mockups.py (option E). */
#include <hal/video.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xhw.h"
#include "xhw_internal.h"

#ifndef XHW_SPLASH_MS
#define XHW_SPLASH_MS 2000
#endif

#define SCR_W 640
#define SCR_H 480
#define FRAME_MS 40
#define BAR_FILL_MS 1000   /* the slowest the bar fills, the whole width */
#define BAR_HOLD_MS 300    /* full, before the mode change ends it */

const unsigned char xhw_font16[256 * 16] = {   /* also the overlay's (xhw_overlay.c) */
#include <hal/font_unscii_16.h>
};
#define GLYPH_W 8
#define GLYPH_H 16

/* the mock-up's palette (0xRRGGBB) */
#define COL_CYAN 0x7fe9ffu
#define COL_PHOS 0x4dffc4u
#define COL_DIM 0x1f5a5eu
#define COL_TEXT 0xbff7f0u
#define COL_HINT 0x4f8f8au
#define COL_RED 0xff3b3bu
#define COL_ORANGE 0xff8a2au
#define COL_GLASS 0x010606u
#define COL_PANEL 0x010708u

/* layout */
#define CARD_X0 70
#define CARD_Y0 118
#define CARD_X1 (SCR_W - 70)
#define CARD_Y1 404
#define HEAD_TEXT "PRIME-X"
#define HEAD_ZOOM 5
#define HEAD_Y 152
#define TASK_Y 278
#define BAR_X 120
#define BAR_Y 312
#define BAR_SEGS 40
#define LOG_X 40
#define LOG_Y 14
#define SPLASH_LOG_LINES 6
#define HINT_TEXT "Hold BACK for safe video (480i)"

static unsigned int* s_fb;      /* the visible framebuffer */
static unsigned int* s_back;    /* each frame is drawn here, then copied out */
static unsigned int* s_bg;      /* the static layer */
static unsigned char* s_shade;  /* per pixel, 0..255: scanlines and the CRT's corners */
static int s_ready;
static volatile int s_run, s_stopped;
static DWORD s_t0;

/* written by the boot thread, read by the splash thread under s_cs */
static CRITICAL_SECTION s_cs;
static char s_log[SPLASH_LOG_LINES][48];
static DWORD s_log_t[SPLASH_LOG_LINES];   /* when each line started typing */
static int s_nlog;
static char s_task[32] = "SCANNING";
static char s_note1[80], s_note2[80], s_det1[64], s_det2[64];
static volatile float s_progress;
static volatile float s_shown;   /* what the bar shows: follows s_progress (splash thread) */
static volatile DWORD s_full_t;  /* when it reached 1, or 0 */
static volatile int s_card;   /* an error card owns the screen */
static unsigned s_frames, s_frame_us;   /* the cost, for [SPLASH] at release */

/* ---- colour ---- */
static unsigned int rgb(int r, int g, int b) {
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    return 0xFF000000u | ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b;
}
/* c (0xRRGGBB) scaled by a/256 */
static unsigned int scale(unsigned int c, int a) {
    return rgb((int)((c >> 16) & 255) * a >> 8, (int)((c >> 8) & 255) * a >> 8, (int)(c & 255) * a >> 8);
}

/* a small LCG: the rain and the glitches */
static unsigned int s_rng = 0x1979u;
static unsigned int rnd(void) {
    s_rng = s_rng * 1664525u + 1013904223u;
    return s_rng >> 8;
}

/* ---- drawing through the shade ---- */
static void put(unsigned int* fb, int x, int y, unsigned int c, int a) {
    if ((unsigned)x >= SCR_W || (unsigned)y >= SCR_H) return;
    fb[y * SCR_W + x] = scale(c, a * s_shade[y * SCR_W + x] >> 8);
}

static void text(unsigned int* fb, const char* s, int n, int x, int y, int zoom, unsigned int c, int a) {
    for (; *s && n-- > 0; s++, x += GLYPH_W * zoom) {
        const unsigned char* g = xhw_font16 + (unsigned char)*s * GLYPH_H;
        int gy, gx, zy, zx;
        for (gy = 0; gy < GLYPH_H; gy++) {
            unsigned char bits = g[gy];
            if (!bits) continue;
            for (gx = 0; gx < GLYPH_W; gx++) {
                if (!(bits & (0x80 >> gx))) continue;
                for (zy = 0; zy < zoom; zy++)
                    for (zx = 0; zx < zoom; zx++) put(fb, x + gx * zoom + zx, y + gy * zoom + zy, c, a);
            }
        }
    }
}

static int text_w(const char* s, int zoom) { return (int)strlen(s) * GLYPH_W * zoom; }

static void centered(unsigned int* fb, const char* s, int y, int zoom, unsigned int c, int a) {
    text(fb, s, 1 << 30, (SCR_W - text_w(s, zoom)) / 2, y, zoom, c, a);
}

/* a 1 px glow round the text, then the text */
static void glow(unsigned int* fb, const char* s, int n, int x, int y, int zoom, unsigned int c, unsigned int g,
                 int a) {
    text(fb, s, n, x - 1, y, zoom, g, a);
    text(fb, s, n, x + 1, y, zoom, g, a);
    text(fb, s, n, x, y - 1, zoom, g, a);
    text(fb, s, n, x, y + 1, zoom, g, a);
    text(fb, s, n, x, y, zoom, c, a);
}

static void rect(unsigned int* fb, int x, int y, int w, int h, unsigned int c, int a) {
    int i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++) put(fb, i, j, c, a);
}

/* one scan bracket: an L of two 3 px strokes, its corner at (x, y), arms
 * towards (sx, sy) */
static void bracket(unsigned int* fb, int x, int y, int sx, int sy, unsigned int c) {
    const int arm = 26, t = 3;
    rect(fb, sx > 0 ? x : x - arm + 1, sy > 0 ? y : y - t + 1, arm, t, c, 256);
    rect(fb, sx > 0 ? x : x - t + 1, sy > 0 ? y : y - arm + 1, t, arm, c, 256);
}

/* ---- the static layer: glass, panels, frame lines, the shade ---- */
static void make_static(void) {
    int x, y;
    for (y = 0; y < SCR_H; y++) {
        for (x = 0; x < SCR_W; x++) {
            /* corners: (dx^4 + dy^4) past 0.8 goes dark; a light overall falloff */
            int dx = (x - SCR_W / 2) * 256 / (SCR_W / 2), dy = (y - SCR_H / 2) * 256 / (SCR_H / 2);
            int dx2 = dx * dx >> 8, dy2 = dy * dy >> 8;
            int corner = ((dx2 * dx2 >> 8) + (dy2 * dy2 >> 8)) - 256;
            int v = 256 - (dx2 + dy2) * 46 / 512;
            if (corner > 0) v -= corner * 384 >> 8;
            if (v < 0) v = 0;
            if (y & 1) v = v * 174 >> 8;   /* scanlines */
            s_shade[y * SCR_W + x] = (unsigned char)(v > 255 ? 255 : v);
        }
    }
    for (y = 0; y < SCR_H; y++)
        for (x = 0; x < SCR_W; x++) put(s_bg, x, y, COL_GLASS, 256);
    /* the card: a darker panel, frame lines between the brackets */
    rect(s_bg, CARD_X0, CARD_Y0, CARD_X1 - CARD_X0, CARD_Y1 - CARD_Y0, COL_PANEL, 256);
    rect(s_bg, CARD_X0 + 30, CARD_Y0, CARD_X1 - CARD_X0 - 60, 1, COL_DIM, 256);
    rect(s_bg, CARD_X0 + 30, CARD_Y1 - 1, CARD_X1 - CARD_X0 - 60, 1, COL_DIM, 256);
}

/* areas the rain stays out of (the card, the log, the tag, the hint) */
static int rain_blocked(int x, int y) {
    if (x + GLYPH_W > CARD_X0 - 4 && x < CARD_X1 + 4 && y + GLYPH_H > CARD_Y0 - 4 && y < CARD_Y1 + 4) return 1;
    if (x < LOG_X + 330 && y < LOG_Y + SPLASH_LOG_LINES * 16 + 2) return 1;
    if (x > SCR_W - 140 && y < 50) return 1;
    if (y > SCR_H - 58 && y < SCR_H - 30 && x > 150 && x < SCR_W - 150) return 1;
    return 0;
}

/* ---- the rain ---- */
#define RAIN_COLS (SCR_W / 10)
#define RAIN_LEN 26
static const char k_rain_pool[] = "0123456789ABCDEF:;<>=*+#%&$@\xA4\xA7\xB1\xB5\xB6\xD0\xDE\xF0\xFE\xD8\xC6\xBF";
static struct {
    int on, len, speed;   /* speed: px per frame, x16 */
    int head;             /* head's y, x16 */
    char glyph[RAIN_LEN];
} s_rain[RAIN_COLS];

static void rain_reset(int i, int anywhere) {
    int k;
    s_rain[i].on = (rnd() % 100) >= 15;
    s_rain[i].len = 8 + (int)(rnd() % 18);
    s_rain[i].speed = 24 + (int)(rnd() % 72);
    s_rain[i].head = anywhere ? (int)(rnd() % (SCR_H + 200)) * 16 : -(int)(rnd() % 200) * 16;
    for (k = 0; k < RAIN_LEN; k++) s_rain[i].glyph[k] = k_rain_pool[rnd() % (sizeof k_rain_pool - 1)];
}

static void rain_step_and_draw(unsigned int* fb) {
    int i, k;
    for (i = 0; i < RAIN_COLS; i++) {
        int x = i * 10, mid = (x - SCR_W / 2) * 256 / (SCR_W / 2);
        int strength = 56 + (mid < 0 ? -mid : mid) * 154 / 256;   /* brighter towards the edges */
        if (!s_rain[i].on) continue;
        s_rain[i].head += s_rain[i].speed;
        if (s_rain[i].head / 16 - s_rain[i].len * GLYPH_H > SCR_H) rain_reset(i, 0);
        if (rnd() % 8 == 0) s_rain[i].glyph[rnd() % RAIN_LEN] = k_rain_pool[rnd() % (sizeof k_rain_pool - 1)];
        for (k = 0; k < s_rain[i].len; k++) {
            int y = (s_rain[i].head / 16 / GLYPH_H - k) * GLYPH_H;   /* glyphs snap to rows, as a terminal's */
            int fade = 256 - k * 256 / s_rain[i].len;
            char ch[2];
            if (y < -GLYPH_H || y >= SCR_H || rain_blocked(x, y)) continue;
            ch[0] = s_rain[i].glyph[(k + s_rain[i].head / 256) % RAIN_LEN];
            ch[1] = 0;
            text(fb, ch, 1, x, y, 1, k == 0 ? 0xe6fff5u : COL_PHOS, (fade * fade >> 8) * strength >> 8);
        }
    }
}

/* ---- one frame ---- */
static void draw_frame(DWORD ms) {
    unsigned int* fb = s_back;
    char log[SPLASH_LOG_LINES][48], task[32], note1[80], note2[80], det1[64], det2[64], pct[8];
    DWORD log_t[SPLASH_LOG_LINES];
    int nlog, i, typed, hx, hw, fade;
    float f;

    memcpy(fb, s_bg, SCR_W * SCR_H * 4);
    rain_step_and_draw(fb);

    EnterCriticalSection(&s_cs);
    nlog = s_nlog;
    memcpy(log, s_log, sizeof log);
    memcpy(log_t, s_log_t, sizeof log_t);
    memcpy(task, s_task, sizeof task);
    memcpy(note1, s_note1, sizeof note1);
    memcpy(note2, s_note2, sizeof note2);
    memcpy(det1, s_det1, sizeof det1);
    memcpy(det2, s_det2, sizeof det2);
    LeaveCriticalSection(&s_cs);

    /* the boot log, each line typed at 12 ms a character from when it came */
    for (i = 0; i < nlog; i++) {
        DWORD since = ms - (log_t[i] - s_t0);
        int n = (int)(since / 12);
        text(fb, log[i], n, LOG_X, LOG_Y + i * 16, 1, i == 0 ? COL_CYAN : COL_PHOS, 256);
        if (i == nlog - 1 && n < (int)strlen(log[i]) + 1 && (ms / 200) % 2 == 0)
            rect(fb, LOG_X + (n < (int)strlen(log[i]) ? n : (int)strlen(log[i])) * GLYPH_W, LOG_Y + i * 16 + 2, 7,
                 12, COL_PHOS, 256);
    }

    /* the tag, blinking slowly */
    if ((ms / 900) % 3 != 2) {
        int sx = SCR_W - 40 - text_w("// SIGNAL", 1);
        text(fb, "// SIGNAL", 1 << 30, sx, LOG_Y + 10, 1, COL_RED, 256);
        rect(fb, sx - 12, LOG_Y + 15, 6, 6, COL_RED, 256);
    }

    /* brackets slide in from the screen corners (400 ms, eased), orange as
     * they lock, cyan from 600 ms */
    {
        int k = ms >= 400 ? 256 : (int)(ms * 256 / 400), u = 256 - k;
        unsigned int c = ms < 400 ? COL_DIM : ms < 600 ? COL_ORANGE : COL_CYAN;
        k = 256 - (u * u >> 8) * u / 256;
        bracket(fb, CARD_X0 * k >> 8, CARD_Y0 * k >> 8, 1, 1, c);
        bracket(fb, SCR_W - 1 + ((CARD_X1 - SCR_W) * k >> 8), CARD_Y0 * k >> 8, -1, 1, c);
        bracket(fb, CARD_X0 * k >> 8, SCR_H - 1 + ((CARD_Y1 - SCR_H) * k >> 8), 1, -1, c);
        bracket(fb, SCR_W - 1 + ((CARD_X1 - SCR_W) * k >> 8), SCR_H - 1 + ((CARD_Y1 - SCR_H) * k >> 8), -1, -1, c);
    }

    /* the credit, then PRIME-X typed on (120 ms a letter from 500 ms),
     * fringed red and cyan, with a block cursor that blinks once it's done */
    centered(fb, "TechProGabe Presents...", 134, 1, COL_HINT, 256);
    typed = ms < 500 ? 0 : (int)((ms - 500) / 120);
    if (typed > (int)sizeof HEAD_TEXT - 1) typed = (int)sizeof HEAD_TEXT - 1;
    hw = text_w(HEAD_TEXT, HEAD_ZOOM);
    hx = (SCR_W - hw) / 2 - 10;
    text(fb, HEAD_TEXT, typed, hx - 3, HEAD_Y, HEAD_ZOOM, 0x5a0a0eu, 256);
    text(fb, HEAD_TEXT, typed, hx + 3, HEAD_Y, HEAD_ZOOM, 0x00465au, 256);
    glow(fb, HEAD_TEXT, typed, hx, HEAD_Y, HEAD_ZOOM, COL_CYAN, COL_DIM, 256);
    if (typed < (int)sizeof HEAD_TEXT - 1 || (ms / 530) % 2 == 0)
        rect(fb, hx + typed * GLYPH_W * HEAD_ZOOM + 6, HEAD_Y + 8, 26, 64, COL_CYAN, 256);
    centered(fb, "METROID PRIME  -  GM8E01  -  USA 1.00", 240, 1, COL_TEXT, 256);

    /* the signal trace: a slow wave with a little noise, and every few
     * seconds a burst somewhere along it */
    {
        int burst_x = 150 + (int)((ms / 3100) * 97 % 300), burst = (ms % 3100) < 600;
        int x;
        for (x = CARD_X0 + 30; x < CARD_X1 - 30; x++) {
            int v = (int)(((x * 7 + (int)ms / 9) % 64) - 32);   /* a triangle wave */
            int y = 262 + (v < 0 ? -v : v) / 8 - 2 + (int)(rnd() % 3) - 1;
            if (burst && x > burst_x && x < burst_x + 40) y += ((x * 13) % 9) - 4;
            put(fb, x, y, COL_DIM, 256);
            put(fb, x, y + 1, 0x0a282cu, 256);
        }
    }

    /* what it is doing, its bar, the detail lines and the notes. The bar
     * eases towards the progress (a quarter of the gap a frame, never slower
     * than BAR_FILL_MS for the width) and drops at once for a new stage. */
    f = s_progress;
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    {
        float shown = s_shown, step = (f - shown) * 0.25f;
        if (step < (float)FRAME_MS / BAR_FILL_MS) step = (float)FRAME_MS / BAR_FILL_MS;
        shown = f <= shown ? f : shown + step > f ? f : shown + step;
        s_shown = shown;
        if (shown >= 1.0f && !s_full_t) s_full_t = ms ? ms : 1;
        if (shown < 1.0f) s_full_t = 0;
        f = shown;
    }
    glow(fb, task, 1 << 30, BAR_X, TASK_Y, 2, COL_TEXT, 0x08282cu, 256);
    snprintf(pct, sizeof pct, "%d%%", (int)(f * 100.0f + 0.5f));
    text(fb, pct, 1 << 30, BAR_X + 400 - text_w(pct, 2), TASK_Y, 2, COL_CYAN, 256);
    for (i = 0; i < BAR_SEGS; i++)
        rect(fb, BAR_X + i * 10, BAR_Y, 8, 6, i < (int)(f * BAR_SEGS + 0.001f) ? COL_CYAN : 0x081e22u, 256);
    if (det1[0]) text(fb, det1, 1 << 30, BAR_X, BAR_Y + 14, 1, COL_PHOS, 256);
    if (det2[0]) text(fb, det2, 1 << 30, BAR_X, BAR_Y + 30, 1, COL_DIM, 256);
    if (note1[0]) centered(fb, note1, 362, 1, COL_TEXT, 256);
    if (note2[0]) centered(fb, note2, 380, 1, COL_TEXT, 256);
    centered(fb, HINT_TEXT, SCR_H - 52, 1, COL_HINT, 256);

    /* the glitch: every few seconds a thin band torn sideways, brighter */
    if (ms > 1500 && (ms % 4300) < 3 * FRAME_MS) {
        int y0 = 40 + (int)((ms / 4300) * 131 % 380), y, shift = 4 + (int)(ms / 4300 % 5);
        for (y = y0; y < y0 + 4 && y < SCR_H; y++) {
            unsigned int* row = fb + y * SCR_W;
            int x;
            memmove(row + shift, row, (SCR_W - shift) * 4);
            for (x = 0; x < SCR_W; x++) {
                unsigned int c = row[x];
                row[x] = rgb((int)((c >> 16) & 255) * 5 / 4 + 6, (int)((c >> 8) & 255) * 5 / 4 + 10,
                             (int)(c & 255) * 5 / 4 + 10);
            }
        }
    }

    /* 300 ms fade in from black */
    fade = ms >= 300 ? 256 : (int)(ms * 256 / 300);
    if (fade < 256)
        for (i = 0; i < SCR_W * SCR_H; i++) fb[i] = scale(fb[i], fade);
}

static DWORD WINAPI splash_thread(LPVOID arg) {
#ifdef XHW_SPLASH_DUMP
    int dumped = 0;
#endif
    (void)arg;
    while (s_run) {
        DWORD t = GetTickCount();
        if (!s_card) {
            uint64_t ns0 = xhw_time_ns();
            draw_frame(t - s_t0);
            memcpy(s_fb, s_back, SCR_W * SCR_H * 4);
            s_frames++;
            s_frame_us += (unsigned)((xhw_time_ns() - ns0) / 1000u);
#ifdef XHW_SPLASH_DUMP
            if (dumped == 0 && t - s_t0 > 2500) {
                xhw_fbdump(s_back, SCR_W, SCR_H, 32, SCR_W * 4);
                dumped = 1;
            } else if (dumped == 1 && s_progress >= 0.25f && s_progress < 1.0f && s_note1[0]) {
                xhw_fbdump(s_back, SCR_W, SCR_H, 32, SCR_W * 4);
                dumped = 2;
            }
#endif
        }
        t = GetTickCount() - t;
        Sleep(t < FRAME_MS ? FRAME_MS - t : 1);
    }
    s_stopped = 1;
    return 0;
}

static int init_fb(void) {
    if (s_ready) return 1;
    if (!XVideoSetMode(SCR_W, SCR_H, 32, REFRESH_DEFAULT)) return 0;
    s_fb = (unsigned int*)XVideoGetFB();
    if (!s_fb) return 0;
    s_ready = 1;
    return 1;
}

/* the CPU line: the clock measured against the tick counter (the TSC is
 * only read here, to report it: docs/boot-safety.md) */
static void log_cpu(void) {
    char line[48];
    unsigned long long t0, t1;
    DWORD ms0 = GetTickCount(), ms;
    unsigned mhz;
    __asm__ volatile("rdtsc" : "=A"(t0));
    Sleep(50);
    __asm__ volatile("rdtsc" : "=A"(t1));
    ms = GetTickCount() - ms0;
    mhz = ms ? (unsigned)((t1 - t0) / (ms * 1000ull)) : 0;
    snprintf(line, sizeof line, "> CPU   %-14s %4u MHZ .. OK", mhz > 780 ? "UPGRADED X86" : "PENTIUM III", mhz);
    xhw_splash_log(line);
}

void xhw_splash_show(void) {
#ifndef XHW_NO_SPLASH
    HANDLE h;
    int i;
    if (!init_fb()) return;
    InitializeCriticalSection(&s_cs);
    s_back = (unsigned int*)malloc(SCR_W * SCR_H * 4);
    s_bg = (unsigned int*)malloc(SCR_W * SCR_H * 4);
    s_shade = (unsigned char*)malloc(SCR_W * SCR_H);
    if (!s_back || !s_bg || !s_shade) {
        free(s_back);
        free(s_bg);
        free(s_shade);
        s_back = s_bg = NULL;
        s_shade = NULL;
        s_ready = 0;
        return;
    }
    make_static();
    for (i = 0; i < RAIN_COLS; i++) rain_reset(i, 1);
    s_t0 = GetTickCount();
    xhw_splash_log("CHOZO ARCHIVE INTERFACE  v0.1");
    s_run = 1;
    h = CreateThread(NULL, 32 * 1024, splash_thread, NULL, 0, NULL);
    if (h) {
        CloseHandle(h);   /* normal priority: it shares the CPU with the boot */
    } else {
        s_run = 0;
        s_stopped = 1;
    }
    log_cpu();
    xhw_logf("[BOOT] splash: PRIME-X (TechProGabe Presents...)");
#endif
}

/* Any button on any controller skips the rest of the minimum hold. */
static int any_button(void) {
    xhw_pad p;
    int i;
    xhw_pad_poll();
    for (i = 0; i < 4; i++)
        if (xhw_pad_get(i, &p) && (p.buttons || p.lt > 128 || p.rt > 128)) return 1;
    return 0;
}

int xhw_splash_skip_pressed(void) {
    xhw_pad p;
    int i;
    xhw_pad_poll();
    for (i = 0; i < 4; i++)
        if (xhw_pad_get(i, &p) && (p.buttons & XHW_BTN_B)) return 1;
    return 0;
}

static void stop_thread(void) {
    int i;
    if (!s_run) return;
    s_run = 0;
    for (i = 0; i < 100 && !s_stopped; i++) Sleep(5);
}

/* XVideoSetMode frees this framebuffer: hold the minimum time, stop the
 * thread, forget the framebuffer, so later calls draw nothing. */
void xhw_splash_release(void) {
#ifndef XHW_NO_SPLASH
    if (s_ready && !s_card) {
        DWORD t_end = GetTickCount() + 3000;   /* the bar's wait has a ceiling */
        while (GetTickCount() - s_t0 < XHW_SPLASH_MS) {
            if (any_button()) break;
            Sleep(16);
        }
        /* the bar fills the rest of the way and stays full a moment */
        if (s_run && s_progress >= 1.0f)
            while ((!s_full_t || GetTickCount() - s_t0 - s_full_t < BAR_HOLD_MS) && GetTickCount() < t_end) Sleep(16);
#ifdef XHW_SPLASH_DUMP
        xhw_fbdump(s_fb, SCR_W, SCR_H, 32, SCR_W * 4);
#endif
    }
    stop_thread();
    if (s_frames)
        xhw_logf("[SPLASH] %u frames, %u.%u ms a frame (drawn and copied)", s_frames, s_frame_us / s_frames / 1000,
                 s_frame_us / s_frames % 1000 / 100);
    free(s_back);
    free(s_bg);
    free(s_shade);
    s_back = s_bg = NULL;
    s_shade = NULL;
#endif
    s_ready = 0;
    s_fb = NULL;
}

void xhw_splash_progress(float f) { s_progress = f; }

static void set(char* dst, size_t cap, const char* src) { snprintf(dst, cap, "%s", src ? src : ""); }

void xhw_splash_log(const char* line) {
    if (!s_ready) return;
    EnterCriticalSection(&s_cs);
    if (s_nlog == SPLASH_LOG_LINES) {   /* scroll */
        memmove(s_log[0] + 0, s_log[1], sizeof s_log - sizeof s_log[0]);
        memmove(s_log_t, s_log_t + 1, sizeof s_log_t - sizeof s_log_t[0]);
        s_nlog--;
    }
    set(s_log[s_nlog], sizeof s_log[0], line);
    s_log_t[s_nlog] = GetTickCount();
    s_nlog++;
    LeaveCriticalSection(&s_cs);
}

void xhw_splash_stage(const char* task, const char* note1, const char* note2) {
    if (!s_ready) return;
    EnterCriticalSection(&s_cs);
    set(s_task, sizeof s_task, task);
    set(s_note1, sizeof s_note1, note1);
    set(s_note2, sizeof s_note2, note2);
    s_det1[0] = s_det2[0] = 0;
    LeaveCriticalSection(&s_cs);
}

void xhw_splash_detail(const char* line1, const char* line2) {
    if (!s_ready) return;
    EnterCriticalSection(&s_cs);
    set(s_det1, sizeof s_det1, line1);
    set(s_det2, sizeof s_det2, line2);
    LeaveCriticalSection(&s_cs);
}

/* An error in the boot screen's dress: the same glass and rain stopped,
 * orange brackets, the title in orange-red, the lines in light cyan.
 * 0 if the splash no longer owns the screen (the caller then uses the
 * debug screen). */
int xhw_splash_card(const char* title, const char* const* lines) {
    unsigned int* fb;
    int i, y;
    if (!s_ready || !s_bg) return 0;
    s_card = 1;
    stop_thread();
    fb = s_back;
    memcpy(fb, s_bg, SCR_W * SCR_H * 4);
    bracket(fb, CARD_X0, CARD_Y0 - 40, 1, 1, COL_ORANGE);
    bracket(fb, CARD_X1 - 1, CARD_Y0 - 40, -1, 1, COL_ORANGE);
    bracket(fb, CARD_X0, CARD_Y1 + 20, 1, -1, COL_ORANGE);
    bracket(fb, CARD_X1 - 1, CARD_Y1 + 20, -1, -1, COL_ORANGE);
    centered(fb, "PRIME-X  //  SIGNAL LOST", CARD_Y0 - 20, 1, COL_RED, 256);
    glow(fb, title, 1 << 30, (SCR_W - text_w(title, 2)) / 2, CARD_Y0 + 10, 2, 0xff6a3au, 0x551c0cu, 256);
    for (i = 0, y = CARD_Y0 + 64; lines && lines[i] && y < CARD_Y1; i++, y += 20)
        centered(fb, lines[i], y, 1, COL_TEXT, 256);
    memcpy(s_fb, fb, SCR_W * SCR_H * 4);
    if (XHW_TEST_BUILD) xhw_fbdump(s_fb, SCR_W, SCR_H, 32, SCR_W * 4);   /* the rejection screens in xemu */
    return 1;
}

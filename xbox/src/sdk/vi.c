/* vi.c - VI on the Xbox. The game waits in VIWaitForRetrace once per frame
 * (CGraphics::EndScene), so that call is the frame boundary
 * (melee-pc's model, src/pc/vi.c there): present the frame the GX backend
 * built, pace the simulation to 60.000 Hz, run due alarms (the pad-poll
 * alarm reads the controllers), then the retrace callbacks. */
#include <dolphin/gx.h>
#include <dolphin/os.h>
#include <dolphin/vi.h>
#include <stdlib.h>

#include "xgx.h"
#include "xhw.h"
#include "xsdk.h"

static u32 s_retrace;
static VIRetraceCallback s_pre_cb, s_post_cb;
static void* s_next_fb;
static void* s_cur_fb;
static BOOL s_black = TRUE;


GXRenderModeObj GXNtsc480IntDf = {
    VI_TVMODE_NTSC_INT, 640, 480, 480, 40, 0, 640, 480, VI_XFBMODE_DF, 0, 0,
    { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 },
    { 8, 8, 10, 12, 10, 8, 8 },
};
GXRenderModeObj GXNtsc480Int = {
    VI_TVMODE_NTSC_INT, 640, 480, 480, 40, 0, 640, 480, VI_XFBMODE_DF, 0, 0,
    { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 },
    { 0, 0, 21, 22, 21, 0, 0 },
};

static u64 sim_period_ns(void) { return 1000000000ull / 60; }

unsigned xsdk_frame_count(void) { return s_retrace; }

void VIInit(void) {}
/* MetroidPrimePort's widescreen widens the framebuffer (fbWidth 854 at
 * 16:9, CGraphics::PortResizeFrameBuffer): the back end maps that EFB to the
 * screen (xgx_set_efb_width) */
void VIConfigure(const GXRenderModeObj* rm) {
    if (rm) xgx_set_efb_width(rm->fbWidth);
}
void VIConfigurePan(u16 x, u16 y, u16 w, u16 h) { (void)x; (void)y; (void)w; (void)h; }
void VIFlush(void) {}
u32 VIGetRetraceCount(void) { return s_retrace; }
u32 VIGetNextField(void) { return s_retrace & 1; }
u32 VIGetDTVStatus(void) { return xhw_video()->progressive; }
u32 VIGetTvFormat(void) { return VI_NTSC; }
u32 VIGetCurrentLine(void) { return 0; }
void* VIGetCurrentFrameBuffer(void) { return s_cur_fb; }
void* VIGetNextFrameBuffer(void) { return s_next_fb; }
void VISetNextFrameBuffer(void* fb) { s_next_fb = fb; }
void VISetBlack(BOOL black) { s_black = black; }
int xsdk_vi_black(void) { return s_black; }
u16 VIPadFrameBufferWidth(u16 width) { return (u16)((width + 15) & ~15); }

VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback cb) {
    VIRetraceCallback old = s_pre_cb;
    s_pre_cb = cb;
    return old;
}

VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback cb) {
    VIRetraceCallback old = s_post_cb;
    s_post_cb = cb;
    return old;
}

void xsdk_frame_boundary(void) {
    static u64 next_ns;
    const u64 period = sim_period_ns();
    u64 now;

    /* the flip itself happens at GXCopyDisp (gx_copy.c) */
    xsdk_widescreen_update();
#if XHW_PGO
    {
        /* an XBOX_PGO=gen build: the counters so far every 30 s of frames
         * (cumulative; tools/xbox/pgo_raw.py takes the last whole dump) */
        static u32 s_pgo_frames;
        if (++s_pgo_frames % 1800 == 0) xhw_pgo_dump("30 s");
    }
#endif

    /* 60.000 Hz regardless of the output: 50 Hz PAL 480i consoles too */
    now = xhw_time_ns();
    if (xsdk_lockstep()) {
        xsdk_lockstep_advance(1000000000u / 60);   /* os.c: the game's clock, one frame on */
        xgx_set_fps_overlay(0);   /* real time: it would differ between builds' shots */
    } else if (next_ns == 0 || now > next_ns + period * 2) {
        next_ns = now;   /* a hitch (loading): don't try to catch up */
    } else {
        int pf = xhw_perf_enter(XHW_PERF_VSYNC);
        while (now + 1500000ull < next_ns) {
            xhw_sleep_ms(1);
            now = xhw_time_ns();
        }
        while (now < next_ns) now = xhw_time_ns();
        xhw_perf_leave(pf);
    }
    next_ns += period;

    s_retrace++;
    xhw_mem_tick();   /* low on memory: ARAM chunks to the disc or the swap file */
    if (s_retrace % 1800 == 0) {   /* every 30 s */
        xsdk_aram_log();
        xhw_mem_log_reclaim();
    }
    xsdk_run_alarms();
    if (s_pre_cb) s_pre_cb(s_retrace);
    s_cur_fb = s_next_fb;
    if (s_post_cb) s_post_cb(s_retrace);
}

void VIWaitForRetrace(void) { xsdk_frame_boundary(); }

/* Prime's EndScene (DolphinCGraphics.cpp) swaps its XFBs in the retrace
 * callbacks after the GP breakpoint at the frame's end; the flip already
 * happened at GXCopyDisp, so the swap completes at once (prime_sdk.c). */
void xsdk_vi_flip_now(void) {
    if (s_pre_cb) s_pre_cb(s_retrace);
    s_cur_fb = s_next_fb;
    if (s_post_cb) s_post_cb(s_retrace);
}

/* gmscene.c's frame loop (PORT: there) brackets its render pass with these,
 * so [PERF] tells simulation from rendering */
static int s_render_prev = -1;

void xsdk_perf_render_begin(int ticks) {
    xhw_perf_ticks((uint32_t)ticks);
    s_render_prev = xhw_perf_enter(XHW_PERF_RENDER);
}

/* Material and texture tracks that flip between two values every frame
 * (strobes: Brinstar's background pieces) show the mean of the two (fobj.c):
 * the GameCube's 480i on a CRT shows one value per field, a steady
 * half-tone; whole frames (progressive, a modern TV, under 60 fps) flickered.
 * -DXSDK_STROBE_BLEND=0 keeps the per-frame values; test builds also take
 * env PX_STROBE_BLEND=0|1. */
#ifndef XSDK_STROBE_BLEND
#define XSDK_STROBE_BLEND 1
#endif
int xsdk_strobe_blend(void) {
    static int on = -1;
    if (on < 0) {
        const char* e = XHW_TEST_BUILD ? getenv("PX_STROBE_BLEND") : NULL;
        on = e ? atoi(e) != 0 : XSDK_STROBE_BLEND;
        if (on != XSDK_STROBE_BLEND)
            xhw_logf("[ANIM] strobe tracks %s (PX_STROBE_BLEND)", on ? "blended" : "per frame");
    }
    return on;
}

void xsdk_perf_render_end(void) {
    if (s_render_prev >= 0) xhw_perf_leave(s_render_prev);
    s_render_prev = -1;
}

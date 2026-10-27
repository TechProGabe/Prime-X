/* boot.c - from the Xbox entry point (xbox/src/hw/xhw_main.c) into the game
 * (src/MetroidPrime/main.cpp's metroid_main, which MetroidPrimePort's
 * platform/main.cpp calls once its window and disc are up). */
#include <dolphin/dvd.h>
#include <dolphin/os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xhw.h"
#include "xsdk.h"
#include "xsdk_settings.h"

int metroid_main(int argc, char** argv);
void px_port_boot(void);   /* xbox/src/port: the game-side settings (aspect, FOV) */

/* BACK held on any controller while Prime-X starts (the splash says so):
 * 480i for a TV that doesn't show the mode the settings ask for. Read
 * before the settings load. */
static int safe_video_held(void) {
    xhw_pad p;
    int i;
    xhw_pad_poll();
    for (i = 0; i < 4; i++)
        if (xhw_pad_get(i, &p) && (p.buttons & XHW_BTN_BACK)) return 1;
    return 0;
}

/* before the video mode is chosen */
void xsdk_early(void) {
    int safe = safe_video_held();
    /* 128 MB consoles always run in 64 MB (docs/boot-safety.md): held
     * before the settings file's allocations */
    uint32_t held = xhw_mem_hold_upper();
    if (held) xhw_logf("[MEM] 128 MB console: running in 64 MB, %u KB above it held back", held);
    xsdk_settings_load();
    if (safe) {
        /* saved, so the next boot stays visible too; the Options rows turn them back on */
        g_xsdk_settings.video_720p = 0;
        g_xsdk_settings.progressive = 0;
        xsdk_settings_save();
        g_xsdk_settings_boot = g_xsdk_settings;
        xhw_logf("[VIDEO] BACK held at boot: 480i (720p and progressive off in settings.ini)");
    }
    xhw_video_set_pref_720p(g_xsdk_settings.video_720p);
    xhw_video_set_pref_480p(g_xsdk_settings.progressive);
}

/* the splash's bar from 0.3 to 0.95 while every file is checked */
static void full_check_progress(float f) { xhw_splash_progress(0.3f + 0.65f * f); }

/* A: 1, B (or a minute of nothing): 0 */
static int ask_a_or_b(void) {
    int i, t;
    for (t = 0; t < 60 * 60; t++) {
        xhw_pad p;
        xhw_pad_poll();
        for (i = 0; i < 4; i++) {
            if (!xhw_pad_get(i, &p)) continue;
            if (p.buttons & XHW_BTN_A) return 1;
            if (p.buttons & XHW_BTN_B) return 0;
        }
        xhw_sleep_ms(16);
    }
    return 0;
}

/* Every file's CRC on the first boot of an image (docs/boot-safety.md).
 * Test builds can skip it with env PX_DISC_CHECK=quick (autopad.txt). */
static void disc_full_check(void) {
    char first[96] = "?", line[160];
    u32 bad_off = 0;
    const char* mode = XHW_TEST_BUILD ? getenv("PX_DISC_CHECK") : NULL;
    int bad;
    if (mode && strcmp(mode, "quick") == 0) {
        xhw_logf("[DISC] full check skipped (PX_DISC_CHECK=quick)");
        return;
    }
    bad = xsdk_disc_full_check(mode && strcmp(mode, "full") == 0, full_check_progress, first, sizeof first, &bad_off);
    if (bad < 0) {
        snprintf(line, sizeof line, "The image couldn't be read at offset %u (hard drive or disc fault).", bad_off);
        xhw_fatal("Disc image read error", line);
    }
    if (bad > 0) {
        static char l0[160];
        const char* lines[] = { l0, "The game may crash in those areas.", "", "A: play anyway    B: quit", NULL };
        snprintf(l0, sizeof l0, "%d files don't match a clean dump (first: %s).", bad, first);
        xhw_error_screen("Disc image doesn't match", lines);
        if (!ask_a_or_b()) xhw_quit_to_dashboard();
        xhw_logf("[DISC] playing anyway with %d files that differ", bad);
    }
}

void xsdk_boot(const char* disc) {
    char why[200];
    static char* argv[] = { "prime_x", NULL };

    OSInit();
    xsdk_prime_sdk_init();
    xhw_splash_progress(0.3f);
    if (!xsdk_dvd_open(disc, why, sizeof why)) xhw_fatal("Wrong or damaged disc image", why);
    disc_full_check();
    xhw_splash_progress(1.0f);

    /* The title card stays up through the loads above; the mode change
     * ends it. main.dol stays loaded: the game reads its default font from
     * it (CGameGlobalObjects::LoadDefaultFont, PortFindDolResource). */
    xhw_video_boot();
    px_port_boot();

    /* pdclib's stdout and stderr are dead handles: the game's own printf and
     * fprintf(stderr) lines go to files in the save folder instead */
    {
        static char out_buf[1024], err_buf[1024];   /* pdclib's setvbuf doesn't allocate one */
        char path[300];
        snprintf(path, sizeof path, "%sstdout.log", xhw_save_dir());
        if (freopen(path, "w", stdout)) setvbuf(stdout, out_buf, _IOLBF, sizeof out_buf);
        snprintf(path, sizeof path, "%sstderr.log", xhw_save_dir());
        if (freopen(path, "w", stderr)) setvbuf(stderr, err_buf, _IOLBF, sizeof err_buf);
    }
    xhw_logf("[BOOT] metroid_main");
    metroid_main(1, argv);
    xhw_quit_to_dashboard();
}

/* The settings menu (Melee-X's menu.c) is not ported: Prime's rows live in
 * its own Options screens (docs/ui.md, phase 5), so the pad is never held
 * back for it. */
int xsdk_menu_block(uint32_t raw_buttons) {
    (void)raw_buttons;
    return 0;
}

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

/* L + R + Y held on any controller while Prime-X starts: the full disc
 * check runs even on an image that passed it (or from a DVD). For testing
 * the check on a console; nothing on screen says so. Looked at before the
 * splash and again as the check starts, so holding through either counts. */
static int s_force_check;
static int force_check_held(void) {
    xhw_pad p;
    int i;
    xhw_pad_poll();
    for (i = 0; i < 4; i++)
        if (xhw_pad_get(i, &p) && (p.buttons & XHW_BTN_Y) && p.lt > 128 && p.rt > 128) return 1;
    return 0;
}

/* before the video mode is chosen */
void xsdk_early(void) {
    int safe = safe_video_held();
    s_force_check = force_check_held();
    /* 128 MB consoles always run in 64 MB (docs/boot-safety.md): held
     * before the settings file's allocations */
    uint32_t held = xhw_mem_hold_upper();
    if (held) xhw_logf("[MEM] 128 MB console: running in 64 MB, %u KB above it held back", held);
    xhw_splash_log(held ? "> MEM   128 MB, HELD TO 64 MB ...... OK" : "> MEM   64 MB  MEM1 24 / ARAM 16 .. OK");
    xsdk_settings_load();
    if (XHW_TEST_BUILD && getenv("PX_MEM_BALLOON_KB")) {
        /* test builds: hold this much RAM for good, to play the console's
         * low-memory moments in xemu (the reclaimers, the out of memory screen) */
        uint32_t kb = (uint32_t)atoi(getenv("PX_MEM_BALLOON_KB"));
        void* p = kb ? malloc(kb * 1024u) : NULL;
        if (p) memset(p, 0x5A, kb * 1024u);
        xhw_logf("[MEM] balloon: %u KB held (%s)", kb, p ? "ok" : "failed");
    }
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

void xhw_watchdog_busy(int on);   /* hw: a long, deliberate stall */

/* the splash's bar is the check's; B skips it (the image is checked again
 * next boot) */
static int s_check_skipped;
static int full_check_progress(float f, const char* file, u32 crc, int ok, const u8* sample, u32 off) {
    char l1[64], l2[64];
    const char* name = strrchr(file, '/');
    name = name ? name + 1 : file;
    snprintf(l1, sizeof l1, "> %-22.22s CRC %08X  %s", name, (unsigned)crc, ok ? "OK" : "BAD");
    if (sample)
        snprintf(l2, sizeof l2, "  %08X  %02X %02X %02X %02X %02X %02X %02X %02X %02X", (unsigned)off, sample[0],
                 sample[1], sample[2], sample[3], sample[4], sample[5], sample[6], sample[7], sample[8]);
    else
        l2[0] = 0;
    xhw_splash_detail(l1, l2);
    xhw_splash_progress(f);
    if (xhw_splash_skip_pressed()) s_check_skipped = 1;
    return s_check_skipped;
}

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

/* Every file's CRC on the first boot of an image (docs/boot-safety.md),
 * behind the splash: its bar reads "VERIFYING DISC IMAGE", with what it is
 * and that it happens once (the user, round hw1: the minute of waiting
 * needs saying). An image that passed is in disc.ini and never checked
 * again. B skips it for this boot. Test builds: env PX_DISC_CHECK=quick
 * skips it, =full checks even an image that passed. */
static void disc_full_check(void) {
    char first[96] = "?", line[160];
    u32 bad_off = 0;
    const char* mode = XHW_TEST_BUILD ? getenv("PX_DISC_CHECK") : NULL;
    int bad, force;
    if (s_force_check || force_check_held()) {
        xhw_logf("[DISC] L+R+Y held at boot: full check forced");
        mode = "full";
    }
    force = mode && strcmp(mode, "full") == 0;
    if (mode && strcmp(mode, "quick") == 0) {
        xhw_logf("[DISC] full check skipped (PX_DISC_CHECK=quick)");
        return;
    }
    /* A burned disc: reading 1.3 GB at DVD speed takes minutes, and the
     * image can't have changed since it was written; it is checked on the
     * PC before burning (tools/xbox/verify_image.py). The quick check (size,
     * FST, main.dol) still ran. */
    if (xhw_running_from_dvd() && !force) {
        xhw_logf("[DISC] full check skipped: running from a DVD (tools/xbox/verify_image.py checks it on the PC)");
        xhw_splash_log("> FILES ON DVD: CHECKED WHEN BURNED");
        return;
    }
    /* checked before: the bar goes on to LOADING without showing the
     * check's stage at all */
    if (xsdk_disc_passed_before() && !force) {
        xhw_logf("[DISC] full check: passed before for this image (disc.ini)");
        xhw_splash_log("> FILES 178 CHECKED BEFORE ........ OK");
        return;
    }
    /* the first boot's spectacle: the music while every file is read */
    xhw_intro_start();
    /* why, in the player's words (the user, 2026-10-27): once per image */
    xhw_splash_stage("VERIFYING DISC IMAGE", "First boot only: checking every file so a bad copy is",
                     "caught now, not as a crash later. ~1 min.   B: skip");
    xhw_splash_progress(0.0f);
    xhw_watchdog_busy(1);
    bad = xsdk_disc_full_check(force, full_check_progress, first, sizeof first, &bad_off);
    xhw_intro_stop();
    xhw_watchdog_busy(0);
    if (bad == -3) {   /* disc.ini written between the two looks: can't happen, but the same answer */
        xhw_splash_log("> FILES 178 CHECKED BEFORE ........ OK");
        xhw_splash_stage("LOADING", NULL, NULL);
        return;
    }
    if (bad == 0) xhw_splash_log("> FILES 178 / 178 ................. OK");
    if (bad == -2) xhw_splash_log("> FILES CHECK SKIPPED (NEXT BOOT)");
    if (bad > 0) xhw_splash_log("> FILES MISMATCH ................ WARN");
    if (bad < 0 && bad != -2) {
        snprintf(line, sizeof line, "The image couldn't be read at offset %u (hard drive or disc fault).", bad_off);
        xhw_fatal("Disc image read error", line);
    }
    if (bad > 0) {
        static char l0[160];
        const char* lines[] = { l0, "The game may crash in those areas (boot.log lists them).", "",
                                "A: play anyway    B: quit", NULL };
        snprintf(l0, sizeof l0, "%d %s a clean dump (first: %s).", bad,
                 bad == 1 ? "file doesn't match" : "files don't match", first);
        xhw_choice_screen("Disc image doesn't match", lines);
        if (!ask_a_or_b()) xhw_quit_to_dashboard();
        xhw_logf("[DISC] playing anyway with %d files that differ", bad);
    }
    xhw_splash_stage("LOADING", NULL, NULL);
}

void xsdk_boot(const char* disc) {
    char why[200];
    static char* argv[] = { "prime_x", NULL };

    OSInit();
    xsdk_prime_sdk_init();
    xhw_splash_stage("SCANNING", NULL, NULL);
    xhw_splash_progress(0.3f);
    if (!xsdk_dvd_open(disc, why, sizeof why)) xhw_fatal("Wrong or damaged disc image", why);
    xhw_splash_progress(0.6f);
    xhw_splash_log("> DISC  GM8E01  REV 00 ......... FOUND");
    xhw_splash_log("> IMAGE SIZE  FST  MAIN.DOL ....... OK");
    disc_full_check();
    xhw_splash_progress(1.0f);   /* xhw_splash_release waits for the bar to get there */

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

/* xsdk_menu_block: the Port Options popup's, in xbox/src/port/port_menu.cpp */

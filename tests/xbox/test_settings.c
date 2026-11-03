/* test_settings.c - settings.c's reading of a hand-edited settings.ini, on the
 * host (tools/xbox/test_settings.py builds it with settings.c included).
 * What a player's hand edit can get wrong: typos in values, unknown keys,
 * out-of-range numbers, a BOM, a file saved as UTF-16; an older file's
 * button layout (gone since 2026-11-03) dropped quietly; and that a save
 * reads back as it was written. */
#include <assert.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32   /* the CI's Linux: pdclib's names for these */
#include <strings.h>
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#endif

#include "settings.c"

static char g_dir[512];
static char g_notice[2][128];
static int g_notices;

const char* xhw_save_dir(void) { return g_dir; }
void xhw_logf(const char* fmt, ...) {
    va_list ap;
    if (!getenv("TEST_VERBOSE")) return;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}
int xhw_replace_file(const char* from, const char* to) {
    remove(to);
    return rename(from, to) == 0 ? XHW_REPLACE_OK : XHW_REPLACE_FAILED;
}
int xhw_copy_file(const char* from, const char* to) {
    FILE *a = fopen(from, "rb"), *b;
    int c;
    if (!a) return 0;
    b = fopen(to, "wb");
    if (!b) return fclose(a), 0;
    while ((c = fgetc(a)) != EOF) fputc(c, b);
    fclose(a);
    fclose(b);
    return 1;
}
void xhw_notice(const char* l1, const char* l2) {
    snprintf(g_notice[0], sizeof g_notice[0], "%s", l1);
    snprintf(g_notice[1], sizeof g_notice[1], "%s", l2);
    g_notices++;
}
void xhw_flush(void* f) { fflush((FILE*)f); }
void xhw_watchdog_busy(int on) { (void)on; }
void xgx_set_fps_overlay(int on) { (void)on; }
void xhw_pad_set_shots(int on) { (void)on; }
void xhw_led_enable(int on) { (void)on; }
void xhw_log_set_file(int on) { (void)on; }

static void write_file(const char* name, const char* body, size_t n) {
    char p[600];
    FILE* f;
    snprintf(p, sizeof p, "%s%s", g_dir, name);
    f = fopen(p, "wb");
    assert(f);
    fwrite(body, 1, n ? n : strlen(body), f);
    fclose(f);
}

static int file_has(const char* name, const char* what) {
    char p[600], buf[16384];
    size_t n;
    FILE* f;
    snprintf(p, sizeof p, "%s%s", g_dir, name);
    f = fopen(p, "rb");
    if (!f) return 0;
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return strstr(buf, what) != NULL;
}

static void load(const char* body) {
    char p[600];
    snprintf(p, sizeof p, "%ssettings.bad.ini", g_dir);
    remove(p);
    g_notices = 0;
    write_file("settings.ini", body, 0);
    xsdk_settings_load();
}

static int fails;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            printf("FAIL line %d: %s\n", __LINE__, #c);                  \
            fails++;                                                     \
        }                                                                \
    } while (0)

int main(int argc, char** argv) {
    xsdk_settings d;
    const xsdk_settings* st = &g_xsdk_settings;
    snprintf(g_dir, sizeof g_dir, "%s/", argc > 1 ? argv[1] : ".");
    xsdk_settings_defaults(&d);

    /* a first boot writes the defaults; they read back with no problem */
    {
        char p[600];
        snprintf(p, sizeof p, "%ssettings.ini", g_dir);
        remove(p);
        g_notices = 0;
        xsdk_settings_load();
        CHECK(g_notices == 0);
        g_notices = 0;
        xsdk_settings_load();
        CHECK(g_notices == 0);
        CHECK(xsdk_settings_equal(st, &d));
        CHECK(file_has("settings.ini", "aim = dual"));
        CHECK(!file_has("settings.ini", "\na = "));
    }

    /* good hand edits: words for flags, a %, # comments, a BOM */
    load("\xEF\xBB\xBF[video]\nfps = yes\nwidescreen=2 # always 16:9\n[game]\nfov = 65\n"
         "[input]\nrumble = 40%\naim = GameCube\n[port1]\ncstick_deadzone = 30\n");
    CHECK(g_notices == 0);
    CHECK(st->fps == 1 && st->widescreen == 2 && st->fov == 65);
    CHECK(pct(st->rumble) == 40 && st->aim == XSDK_AIM_GAMECUBE);
    CHECK(pct(st->port[0].cstick_deadzone) == 30);

    /* typos keep the default; out of range takes the nearest; all reported */
    load("[video]\nfps = maybe\n[game]\nfov = wide\n[port1]\ntrigger_click = 0\nstick_deadzone = 95\n"
         "zz = A\n[videos]\nfoo = 1\n[game]\nbar = 2\nno equals here\n[input]\naim = triple\n");
    CHECK(st->fps == d.fps);
    CHECK(st->fov == 55);
    CHECK(st->port[0].trigger_click == 64);
    CHECK(pct(st->port[0].stick_deadzone) == 60);
    CHECK(st->aim == XSDK_AIM_DUAL);   /* "triple": the default kept */
    CHECK(g_notices == 1);
    if (!strstr(g_notice[0], "9 lines fixed")) printf("notice: %s | %s\n", g_notice[0], g_notice[1]);
    CHECK(strstr(g_notice[0], "9 lines fixed") != NULL);
    CHECK(file_has("settings.bad.ini", "fov = wide"));
    CHECK(file_has("settings.ini", "fov = wide: not a number"));
    /* the fixed file loads clean */
    g_notices = 0;
    xsdk_settings_load();
    CHECK(g_notices == 0);

    /* a file from before 2026-11-03: its button layout (a = ..., lt, rt,
     * trigger_z) is skipped without a word and the file written again
     * without it; the layout comes from aim (pad.c) */
    load("[input]\naim = gamecube\n[port1]\ntrigger_click = 200\na = Jump\nb = Fire\nlt = Z\nrt = NONE\n"
         "trigger_z = left\nblack = BEAM   ; Beam Select\n[port2]\nstart = NONE\n");
    CHECK(g_notices == 0);
    CHECK(st->aim == XSDK_AIM_GAMECUBE && st->port[0].trigger_click == 200);
    CHECK(!file_has("settings.ini", "\na = ") && !file_has("settings.ini", "trigger_z") &&
          !file_has("settings.ini", "\nrt = ") && !file_has("settings.bad.ini", "a = Jump"));
    CHECK(file_has("settings.ini", "trigger_click = 200"));
    g_notices = 0;
    xsdk_settings_load();
    CHECK(g_notices == 0 && st->aim == XSDK_AIM_GAMECUBE);

    /* Notepad's "Unicode": defaults, reported */
    {
        static const char u16[] = "\xFF\xFE[\0v\0i\0d\0e\0o\0]\0\n\0f\0p\0s\0 \0=\0 \0\x31\0\n\0";
        char p[600];
        g_notices = 0;
        snprintf(p, sizeof p, "%ssettings.bad.ini", g_dir);
        remove(p);
        write_file("settings.ini", u16, sizeof u16 - 1);
        xsdk_settings_load();
        CHECK(g_notices == 1 && strstr(g_notice[1], "Unicode") != NULL);
        CHECK(xsdk_settings_equal(st, &d));
    }

    /* Melee-X's old keys are dropped quietly */
    load("[system]\nled_effects = 1\n[input]\ncstick_1p = 1\n");
    CHECK(g_notices == 0);

    /* a line too long for the buffer */
    {
        char big[600];
        memset(big, 'x', sizeof big);
        memcpy(big, "[game]\nfov = 70\n;", 17);
        big[sizeof big - 2] = '\n';
        big[sizeof big - 1] = 0;
        load(big);
        CHECK(st->fov == 70);
        CHECK(g_notices == 0);   /* a long comment is still a comment */
    }

    /* every value a save writes reads back the same */
    {
        xsdk_settings_defaults(&g_xsdk_settings);
        g_xsdk_settings.fov = 75;
        g_xsdk_settings.aim = XSDK_AIM_GAMECUBE;
        g_xsdk_settings.aim_speed = XSDK_AIM_SPEED_HIGH;
        g_xsdk_settings.port[3].trigger_click = 180;
        g_xsdk_settings.port[3].stick_deadzone = 0.35f;
        CHECK(xsdk_settings_save());
        d = g_xsdk_settings;
        g_notices = 0;
        xsdk_settings_load();
        CHECK(g_notices == 0);
        CHECK(xsdk_settings_equal(st, &d));
    }

    printf(fails ? "test_settings: %d failed\n" : "test_settings: all passed\n", fails);
    return fails != 0;
}

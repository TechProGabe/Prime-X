/* settings.c - E:\UDATA\<title>\settings.ini: Prime-X's own settings
 * ([video], [system], [game], [input], [port1]..[port4]; write_all below is
 * the file's documentation, comments and all).
 *
 * Written with the defaults on first boot so it is there to edit, and by the
 * Port Options popup (xbox/src/port/port_menu.cpp). A hand edit is checked
 * line by line (parse): a value it can't read keeps the default, a number
 * out of range takes the nearest, a layout with no Fire or no Pause goes
 * back to the default, and anything wrong is reported on screen, at the top
 * of the settings.ini written back, and in boot.log, with the file as it was
 * kept as settings.bad.ini. A save writes settings.tmp, ending in an
 * END_MARK line, and reads it back; only a copy that is complete and parses
 * to the same settings replaces settings.ini (xhw_replace_file), so a failed
 * write or a power cut leaves the old file. A settings.tmp found without a
 * settings.ini is taken if it is complete: a save whose rename was cut off
 * after settings.ini had been deleted. The writer regenerates the whole
 * file: comments and keys it doesn't know are not kept (Melee-X's
 * led_effects and cstick_1p, v52's ram128). */
#include <dolphin/pad.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xgx.h"
#include "xhw.h"
#include "xsdk_settings.h"

/* the frame-rate counter's default when settings.ini has no fps line: on in
 * test builds, off in a release (XHW_TEST_BUILD, xhw.h) */
#ifndef XSDK_FPS_DEFAULT
#define XSDK_FPS_DEFAULT XHW_TEST_BUILD
#endif

xsdk_settings g_xsdk_settings;
xsdk_settings g_xsdk_settings_boot;

static const struct { const char* name; uint32_t bit; } k_xbox[] = {
    { "a", XHW_BTN_A }, { "b", XHW_BTN_B }, { "x", XHW_BTN_X }, { "y", XHW_BTN_Y },
    { "white", XHW_BTN_WHITE }, { "black", XHW_BTN_BLACK }, { "start", XHW_BTN_START },
    { "back", XHW_BTN_BACK }, { "lstick", XHW_BTN_LSTICK }, { "rstick", XHW_BTN_RSTICK },
    { "up", XHW_BTN_UP }, { "down", XHW_BTN_DOWN }, { "left", XHW_BTN_LEFT }, { "right", XHW_BTN_RIGHT },
};
#define N_XBOX (int)(sizeof k_xbox / sizeof k_xbox[0])

static const struct { const char* name; uint16_t bits; } k_gc[] = {
    { "NONE", 0 }, { "A", PAD_BUTTON_A }, { "B", PAD_BUTTON_B }, { "X", PAD_BUTTON_X },
    { "Y", PAD_BUTTON_Y }, { "Z", PAD_TRIGGER_Z }, { "L", PAD_TRIGGER_L }, { "R", PAD_TRIGGER_R },
    { "START", PAD_BUTTON_START }, { "UP", PAD_BUTTON_UP }, { "DOWN", PAD_BUTTON_DOWN },
    { "LEFT", PAD_BUTTON_LEFT }, { "RIGHT", PAD_BUTTON_RIGHT },
};
#define N_GC (int)(sizeof k_gc / sizeof k_gc[0])

/* the default layout: GameCube-like by position */
static const char* const k_default[N_XBOX] = {
    "A", "X", "B", "Y", "Z", "Z", "START", "NONE", "NONE", "NONE", "UP", "DOWN", "LEFT", "RIGHT",
};

static uint16_t gc_bits(const char* name) {
    int i;
    for (i = 0; i < N_GC; i++)
        if (_stricmp(name, k_gc[i].name) == 0) return k_gc[i].bits;
    return 0;
}

static const char* gc_name(uint16_t bits) {
    int i;
    for (i = 0; i < N_GC; i++)
        if (k_gc[i].bits == bits) return k_gc[i].name;
    return "NONE";
}

static void defaults(xsdk_settings* st) {
    int p, i;
    memset(st, 0, sizeof *st);
    st->video_720p = 1;   /* only where the dashboard allows 720p (xhw_video.c); BACK at boot: 480i */
    st->progressive = 1;
    st->widescreen = 1;
    st->fps = XSDK_FPS_DEFAULT;
    st->shots = XHW_TEST_BUILD;
    st->led = 0;   /* a Kronos-modded console needed a Cerbios recovery (docs/decisions.md) */
    st->log = 0;   /* release builds write no boot.log unless asked (test builds always do) */
    st->rumble = 1.0f;
    st->hud_wide = 0;
    st->fov = 55;
    st->skip_cutscenes = 0;
    st->sim_mode = XSDK_SIM_AUTO;
    for (p = 0; p < 4; p++) {
        xsdk_port_settings* ps = &st->port[p];
        ps->stick_deadzone = 0.20f;
        ps->cstick_deadzone = 0.25f;
        ps->trigger_click = 230;
        for (i = 0; i < N_XBOX; i++) {
            ps->bind[i].xbox = k_xbox[i].bit;
            ps->bind[i].gc = gc_bits(k_default[i]);
        }
    }
}

void xsdk_settings_defaults(xsdk_settings* st) { defaults(st); }

static void path(char* out, size_t cap) { snprintf(out, cap, "%ssettings.ini", xhw_save_dir()); }
static void tmp_path(char* out, size_t cap) { snprintf(out, cap, "%ssettings.tmp", xhw_save_dir()); }

static char* trim(char* s) {
    char* e;
    while (isspace((unsigned char)*s)) s++;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int pct(float f) { return (int)(f * 100.0f + 0.5f); }

/* SAW_RAM128: an old file's ram128 = 1, only logged (the key is gone) */
enum { SAW_FPS = 1, SAW_PROGRESSIVE = 2, SAW_RAM128 = 4, SAW_END = 8, SAW_UNICODE = 16 };
#define SAW_KEYS (SAW_FPS | SAW_PROGRESSIVE)   /* a file without one of these is rewritten */
#define END_MARK "; end of settings"   /* the writer's last line: the file is whole */

/* What a hand edit got wrong, found by the load (a read-back records none).
 * Release builds keep no boot.log by default, so the player hears of it
 * three ways: a notice on screen, the lines at the top of the settings.ini
 * written in place of the bad one, and the bad one kept as settings.bad.ini.
 * (A player who edited by hand saw "weird stuff", 2026-11-01: atoi made a
 * typo 0, an unknown button name unbound the button, a trigger_click near 0
 * held lock-on, and the file's own comments still described Melee-X's keys.) */
#define MAX_PROBLEMS 8
static char s_problem[MAX_PROBLEMS][112];
static int s_problems, s_problems_more, s_record, s_write_problems;

static void problem(int line, const char* fmt, const char* a, const char* b) {
    char what[100];
    if (!s_record) return;
    if (s_problems >= MAX_PROBLEMS) {
        s_problems_more++;
        return;
    }
    snprintf(what, sizeof what, fmt, a, b);
    if (line > 0) snprintf(s_problem[s_problems], sizeof s_problem[0], "line %d: %s", line, what);
    else snprintf(s_problem[s_problems], sizeof s_problem[0], "%s", what);
    s_problems++;
}

/* a whole number, nothing after it but a % */
static int num(const char* v, int* out) {
    char* end;
    long n;
    if (!*v) return 0;
    n = strtol(v, &end, 10);
    if (end == v) return 0;
    while (*end == ' ' || *end == '\t') end++;
    if (*end == '%') end++;
    if (*end) return 0;
    *out = n > 100000 ? 100000 : n < -100000 ? -100000 : (int)n;
    return 1;
}

/* 0/1, off/on, no/yes, false/true */
static int flag(const char* v, int* out) {
    static const char* const k_off[] = { "0", "off", "no", "false" };
    static const char* const k_on[] = { "1", "on", "yes", "true" };
    int i;
    for (i = 0; i < 4; i++) {
        if (_stricmp(v, k_off[i]) == 0) return *out = 0, 1;
        if (_stricmp(v, k_on[i]) == 0) return *out = 1, 1;
    }
    return 0;
}

static void set_flag(int line, const char* key, const char* val, int* dst) {
    int v;
    if (flag(val, &v)) *dst = v;
    else problem(line, "%s = %s: use 0 or 1, the default kept", key, val);
}

static void set_num(int line, const char* key, const char* val, int lo, int hi, int* dst) {
    int v;
    char range[40];
    if (!num(val, &v)) {
        problem(line, "%s = %s: not a number, the default kept", key, val);
        return;
    }
    if (v < lo || v > hi) {
        snprintf(range, sizeof range, "%d to %d", lo, hi);
        problem(line, "%s: goes from %s, the nearest used", key, range);
        v = clampi(v, lo, hi);
    }
    *dst = v;
}

/* a button's action: settings.ini's GameCube names, or the Port Options
 * popup's (Fire, Jump...), which a hand edit is as likely to use */
static int gc_lookup(const char* name, uint16_t* out) {
    static const struct { const char* name; uint16_t bits; } k_alias[] = {
        { "fire", PAD_BUTTON_A }, { "jump", PAD_BUTTON_B }, { "morph", PAD_BUTTON_X },
        { "morph ball", PAD_BUTTON_X }, { "missile", PAD_BUTTON_Y }, { "map", PAD_TRIGGER_Z },
        { "lock", PAD_TRIGGER_L }, { "lock on", PAD_TRIGGER_L }, { "aim", PAD_TRIGGER_R },
        { "free aim", PAD_TRIGGER_R }, { "pause", PAD_BUTTON_START }, { "d-up", PAD_BUTTON_UP },
        { "d-down", PAD_BUTTON_DOWN }, { "d-left", PAD_BUTTON_LEFT }, { "d-right", PAD_BUTTON_RIGHT },
    };
    int i;
    for (i = 0; i < N_GC; i++)
        if (_stricmp(name, k_gc[i].name) == 0) return *out = k_gc[i].bits, 1;
    for (i = 0; i < (int)(sizeof k_alias / sizeof k_alias[0]); i++)
        if (_stricmp(name, k_alias[i].name) == 0) return *out = k_alias[i].bits, 1;
    return 0;
}

static int is_port_section(const char* s) {
    return _strnicmp(s, "port", 4) == 0 && s[4] >= '1' && s[4] <= '4' && !s[5];
}

/* the file's keys over the defaults into *st; returns the SAW_* found */
static int parse(FILE* f, xsdk_settings* st) {
    char line[256], section[32] = "";
    int saw = 0, n = 0, p;
    defaults(st);
    while (fgets(line, sizeof line, f)) {
        char *s = line, *eq, *key, *val, *cut;
        int i;
        n++;
        if (n == 1) {
            /* Notepad's UTF-8 mark; UTF-16 ("Unicode") is bytes with NULs between */
            if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) {
                s += 3;
            } else if (((unsigned char)s[0] == 0xFF && (unsigned char)s[1] == 0xFE) ||
                       ((unsigned char)s[0] == 0xFE && (unsigned char)s[1] == 0xFF)) {
                problem(0, "%s%s", "saved as Unicode (UTF-16): save it as ANSI or UTF-8", "");
                saw |= SAW_UNICODE;
                break;
            }
        }
        if (!strchr(s, '\n') && !feof(f)) {   /* longer than the buffer: the rest skipped */
            int c;
            char* t = trim(s);
            if (*t != ';' && *t != '#') problem(n, "%s%s", "too long, not read", "");   /* a long comment is fine */
            while ((c = fgetc(f)) != EOF && c != '\n') {}
            continue;
        }
        s = trim(s);
        if (strcmp(s, END_MARK) == 0) saw |= SAW_END;
        if ((cut = strchr(s, ';')) != NULL) *cut = '\0';
        if ((cut = strchr(s, '#')) != NULL) *cut = '\0';   /* the other comment mark people use */
        s = trim(s);
        if (!*s) continue;
        if (*s == '[') {
            char* end = strchr(s, ']');
            if (!end) {
                problem(n, "%s: no closing ]", s, "");
                section[0] = '\0';
                continue;
            }
            *end = '\0';
            snprintf(section, sizeof section, "%s", trim(s + 1));
            if (_stricmp(section, "video") && _stricmp(section, "system") && _stricmp(section, "game") &&
                _stricmp(section, "input") && !is_port_section(section))
                problem(n, "[%s]: no such section", section, "");
            continue;
        }
        if (!(eq = strchr(s, '='))) {
            problem(n, "%s: no = in it", s, "");
            continue;
        }
        *eq = '\0';
        key = trim(s);
        val = trim(eq + 1);
        if (_stricmp(section, "video") == 0) {
            if (_stricmp(key, "720p") == 0) set_flag(n, key, val, &st->video_720p);
            else if (_stricmp(key, "progressive") == 0) set_flag(n, key, val, &st->progressive), saw |= SAW_PROGRESSIVE;
            else if (_stricmp(key, "widescreen") == 0) set_num(n, key, val, 0, 2, &st->widescreen);
            else if (_stricmp(key, "fps") == 0) set_flag(n, key, val, &st->fps), saw |= SAW_FPS;
            else problem(n, "%s: no such setting in [video]", key, "");
        } else if (_stricmp(section, "system") == 0) {
            if (_stricmp(key, "ram128") == 0) saw |= atoi(val) != 0 ? SAW_RAM128 : 0;
            else if (_stricmp(key, "screenshots") == 0) set_flag(n, key, val, &st->shots);
            else if (_stricmp(key, "log_file") == 0) set_flag(n, key, val, &st->log);
            /* Melee-X's front LED effects (KOs, GAME!), in older files: no
             * effect in Prime, dropped at the next save */
            else if (_stricmp(key, "led_effects") == 0 || _stricmp(key, "led") == 0) {}
            else problem(n, "%s: no such setting in [system]", key, "");
        } else if (_stricmp(section, "game") == 0) {
            if (_stricmp(key, "hud_wide") == 0) set_flag(n, key, val, &st->hud_wide);
            else if (_stricmp(key, "fov") == 0) set_num(n, key, val, 45, 75, &st->fov);
            else if (_stricmp(key, "skip_cutscenes") == 0) set_flag(n, key, val, &st->skip_cutscenes);
            /* "timing" and the older "game_timing" are ignored: Game Timing
             * is always Auto since 2026-10-30 (the user's call; Fixed and
             * Flex only through the test builds' PX_SIM_AUTO=0 and
             * PX_SIM_ADAPTIVE=1, port_debug.cpp) */
            else if (_stricmp(key, "timing") == 0 || _stricmp(key, "game_timing") == 0) {}
            else problem(n, "%s: no such setting in [game]", key, "");
        } else if (_stricmp(section, "input") == 0) {
            int v;
            if (_stricmp(key, "rumble") == 0) {
                v = pct(st->rumble);
                set_num(n, key, val, 0, 100, &v);
                st->rumble = v / 100.0f;
            } else if (_stricmp(key, "cstick_1p") == 0) {
                /* Melee-X's (smash attacks in 1P modes): no effect in Prime */
            } else {
                problem(n, "%s: no such setting in [input]", key, "");
            }
        } else if (is_port_section(section)) {
            xsdk_port_settings* ps = &st->port[section[4] - '1'];
            int v;
            if (_stricmp(key, "stick_deadzone") == 0) {
                v = pct(ps->stick_deadzone);
                set_num(n, key, val, 0, 60, &v);
                ps->stick_deadzone = v / 100.0f;
            } else if (_stricmp(key, "cstick_deadzone") == 0) {
                v = pct(ps->cstick_deadzone);
                set_num(n, key, val, 0, 60, &v);
                ps->cstick_deadzone = v / 100.0f;
            } else if (_stricmp(key, "trigger_click") == 0) {
                /* below ~64 a trigger at rest can read as pulled: lock-on held */
                v = ps->trigger_click;
                set_num(n, key, val, 64, 255, &v);
                ps->trigger_click = (uint8_t)v;
            } else if (_stricmp(key, "trigger_z") == 0) {
                if (_stricmp(val, "left") == 0) ps->trigger_z = XSDK_TRIGGER_Z_LEFT;
                else if (_stricmp(val, "right") == 0) ps->trigger_z = XSDK_TRIGGER_Z_RIGHT;
                else if (_stricmp(val, "off") == 0 || _stricmp(val, "0") == 0 || _stricmp(val, "none") == 0)
                    ps->trigger_z = XSDK_TRIGGER_Z_OFF;
                else problem(n, "trigger_z = %s: use off, left or right", val, "");
            } else {
                for (i = 0; i < N_XBOX; i++)
                    if (_stricmp(key, k_xbox[i].name) == 0) break;
                if (i == N_XBOX) problem(n, "%s: not an Xbox button (a b x y white black start back...)", key, "");
                else if (!gc_lookup(val, &ps->bind[i].gc))
                    problem(n, "%s = %s: not an action (Fire Jump Morph Missile Map Lock Aim Pause NONE...)", key, val);
            }
        } else if (!section[0]) {
            problem(n, "%s: above the first [section]", key, "");
        }   /* else: the unknown section was reported once */
    }
    /* Each port's buttons keep Fire (GameCube A, also confirm in the game's
     * menus) and Pause (START, the way to Options): a layout without one is
     * reset, else a hand edit could leave no way to play or to fix it */
    for (p = 0; p < 4; p++) {
        xsdk_port_settings* ps = &st->port[p];
        int i, fire = 0, pause = 0;
        for (i = 0; i < N_XBOX; i++) {
            if (ps->bind[i].gc == PAD_BUTTON_A) fire = 1;
            if (ps->bind[i].gc == PAD_BUTTON_START) pause = 1;
        }
        if (!fire || !pause) {
            char which[16];
            snprintf(which, sizeof which, "[port%d]", p + 1);
            problem(0, "%s: no button %s; its buttons reset", which, !fire ? "is Fire (A)" : "is Pause (START)");
            for (i = 0; i < N_XBOX; i++) ps->bind[i].gc = gc_bits(k_default[i]);
        }
    }
    return saw;
}

/* equal as the file holds them (percentages rounded) */
int xsdk_settings_equal(const xsdk_settings* a, const xsdk_settings* b) {
    int p, i;
    if (a->video_720p != b->video_720p || a->progressive != b->progressive || a->widescreen != b->widescreen ||
        a->fps != b->fps || a->shots != b->shots || a->led != b->led || a->log != b->log ||
        pct(a->rumble) != pct(b->rumble) || a->cstick_1p != b->cstick_1p || a->hud_wide != b->hud_wide ||
        a->fov != b->fov || a->skip_cutscenes != b->skip_cutscenes)
        return 0;
    for (p = 0; p < 4; p++) {
        const xsdk_port_settings *x = &a->port[p], *y = &b->port[p];
        if (pct(x->stick_deadzone) != pct(y->stick_deadzone) || pct(x->cstick_deadzone) != pct(y->cstick_deadzone) ||
            x->trigger_click != y->trigger_click || x->trigger_z != y->trigger_z)
            return 0;
        for (i = 0; i < N_XBOX; i++)
            if (x->bind[i].gc != y->bind[i].gc) return 0;
    }
    return 1;
}

int xsdk_cstick_1p(void) { return g_xsdk_settings.cstick_1p; }

static void log_summary(const char* what) {
    const xsdk_settings* st = &g_xsdk_settings;
    const xsdk_port_settings* p1 = &st->port[0];
    xhw_logf("[SETTINGS] %s: 720p %d, progressive %d, widescreen %d, fps %d, screenshots %d, led %d, log %d, "
             "HUD wide %d, FOV %d, rumble %d, C-stick in 1P %d, port 1 dead zones %d/%d, trigger click %d, trigger Z %d",
             what, st->video_720p, st->progressive, st->widescreen, st->fps, st->shots, st->led, st->log,
             st->hud_wide,
             st->fov, pct(st->rumble), st->cstick_1p,
             pct(p1->stick_deadzone), pct(p1->cstick_deadzone), p1->trigger_click, p1->trigger_z);
}

static xsdk_settings s_scratch;   /* read-backs and recovery */

void xsdk_settings_load(void) {
    char p[260], t[260];
    FILE* f;
    int saw;
    path(p, sizeof p);
    tmp_path(t, sizeof t);
#ifdef XSDK_SETTINGS_RESET
    /* test builds: start from the settings.ini staged next to default.xbe
     * (PX_STAGE_EXTRA), or from none, as on a first boot */
    remove(p);
    remove(t);
    if (xhw_copy_file("D:\\settings.ini", p)) xhw_logf("[SETTINGS] %s from D:\\settings.ini (XSDK_SETTINGS_RESET)", p);
    else xhw_logf("[SETTINGS] removed %s (XSDK_SETTINGS_RESET)", p);
#endif
    f = fopen(p, "r");
    if (!f && (f = fopen(t, "r")) != NULL) {
        /* settings.ini is only deleted once settings.tmp has been read back
         * whole (xhw_replace_file's fallback): a power cut between the two.
         * A settings.tmp cut off mid-write has no END_MARK and is left. */
        int whole = (parse(f, &s_scratch) & SAW_END) != 0;
        fclose(f);
        f = NULL;
        if (!whole) xhw_logf("[SETTINGS] settings.tmp is incomplete: not used");
        else if (xhw_replace_file(t, p) == XHW_REPLACE_OK) {
            xhw_logf("[SETTINGS] %s taken from settings.tmp (an unfinished save)", p);
            f = fopen(p, "r");
        }
    }
    if (!f) {
        defaults(&g_xsdk_settings);
        xsdk_settings_save();
        g_xsdk_settings_boot = g_xsdk_settings;
        xgx_set_fps_overlay(g_xsdk_settings.fps);
        xhw_pad_set_shots(g_xsdk_settings.shots);
        xhw_led_enable(g_xsdk_settings.led);
        xhw_log_set_file(g_xsdk_settings.log);
        return;
    }
    s_record = 1;
    s_problems = s_problems_more = 0;
    saw = parse(f, &g_xsdk_settings);
    s_record = 0;
    fclose(f);
    xhw_logf("[SETTINGS] loaded %s", p);
    /* the line stays until the next save, which doesn't write it */
    if (saw & SAW_RAM128) xhw_logf("[SETTINGS] ram128 = 1 ignored: 128 MB consoles run in 64 MB");
    log_summary("in use");
    if (s_problems) {
        /* a hand edit Prime-X couldn't follow: what it used instead goes
         * back into settings.ini, with what was wrong at its top, and the
         * file as it was is kept */
        char bad[260], l1[64], l2[64];
        int i;
        for (i = 0; i < s_problems; i++) xhw_logf("[SETTINGS] %s", s_problem[i]);
        if (s_problems_more) xhw_logf("[SETTINGS] and %d more", s_problems_more);
        snprintf(bad, sizeof bad, "%ssettings.bad.ini", xhw_save_dir());
        if (xhw_copy_file(p, bad)) xhw_logf("[SETTINGS] the file as it was: %s", bad);
        s_write_problems = 1;
        xsdk_settings_save();
        s_write_problems = 0;
        snprintf(l1, sizeof l1, "settings.ini: %d line%s fixed (see the file)", s_problems + s_problems_more,
                 s_problems + s_problems_more == 1 ? "" : "s");
        snprintf(l2, sizeof l2, "%.51s", s_problem[0]);
        xhw_notice(l1, l2);
    } else if ((saw & SAW_KEYS) != SAW_KEYS) {
        xsdk_settings_save();   /* add the missing lines */
    }
    g_xsdk_settings_boot = g_xsdk_settings;
    xgx_set_fps_overlay(g_xsdk_settings.fps);
    xhw_pad_set_shots(g_xsdk_settings.shots);
    xhw_led_enable(g_xsdk_settings.led);
    xhw_log_set_file(g_xsdk_settings.log);
}

/* the action a GameCube button is in Prime (default controls), for the
 * comment after each button line */
static const char* action_name(uint16_t gc) {
    switch (gc) {
    case PAD_BUTTON_A: return "Fire";
    case PAD_BUTTON_B: return "Jump";
    case PAD_BUTTON_X: return "Morph Ball";
    case PAD_BUTTON_Y: return "Missile";
    case PAD_TRIGGER_Z: return "Map";
    case PAD_TRIGGER_L: return "Lock On";
    case PAD_TRIGGER_R: return "Free Aim";
    case PAD_BUTTON_START: return "Pause";
    case PAD_BUTTON_UP: case PAD_BUTTON_DOWN: case PAD_BUTTON_LEFT: case PAD_BUTTON_RIGHT: return "D-pad (visors)";
    default: return "nothing";
    }
}

static void write_all(FILE* f, const xsdk_settings* st) {
    int port, i;
    fprintf(f, "; Prime-X settings. Port Options (the main menu, or Options on the pause screen)\n");
    fprintf(f, "; sets all of these; edit by hand only with the game off. 0 = off, 1 = on.\n");
    fprintf(f, "; A line Prime-X can't read is reported on screen, fixed here, and the file as it was\n");
    fprintf(f, "; kept as settings.bad.ini.\n");
    if (s_write_problems) {
        fprintf(f, ";\n; Fixed at the last start (the old file is settings.bad.ini):\n");
        for (i = 0; i < s_problems; i++) fprintf(f, ";   %s\n", s_problem[i]);
        if (s_problems_more) fprintf(f, ";   and %d more\n", s_problems_more);
    }
    fprintf(f, "\n; 720p = 1: 720p when the dashboard allows it. progressive = 1: 480p when it allows that, 0: 480i.\n");
    fprintf(f, "; Hold BACK while Prime-X starts for 480i (sets both to 0).\n");
    fprintf(f, "; widescreen, at 480: 1 as the dashboard says, 0 always 4:3, 2 always 16:9. 720p is 16:9.\n");
    fprintf(f, "; fps = 1: a frame-rate counter in the top-left corner.\n");
    fprintf(f, "[video]\n720p = %d\nprogressive = %d\nwidescreen = %d\nfps = %d\n\n", st->video_720p, st->progressive,
            st->widescreen, st->fps);
    fprintf(f, "; screenshots = 1: BACK saves a screenshot (shotNN.bmp, next to this file).\n");
    fprintf(f, "; log_file = 1: boot.log in this folder, for bug reports (0: nothing written while you play).\n");
    fprintf(f, "[system]\nscreenshots = %d\nlog_file = %d\n\n", st->shots, st->log);
    fprintf(f, "; hud_wide = 1: at 16:9 the HUD reaches the screen's edges. fov: 45 to 75 degrees, 55 is the\n");
    fprintf(f, "; GameCube's. skip_cutscenes = 1: START skips any cutscene.\n");
    fprintf(f, "[game]\nhud_wide = %d\nfov = %d\nskip_cutscenes = %d\n\n", st->hud_wide, st->fov,
            st->skip_cutscenes);
    fprintf(f, "; rumble: 0 to 100 percent (rumble on or off is in the game's own Options).\n");
    fprintf(f, "[input]\nrumble = %d\n\n", pct(st->rumble));
    fprintf(f, "; Each controller port. stick_deadzone, cstick_deadzone: 0 to 60 percent.\n");
    fprintf(f, "; trigger_click: 64 to 255, how far LT and RT go in before they count as pressed.\n");
    fprintf(f, "; trigger_z = left or right: that trigger opens the map instead of locking on (left) or\n");
    fprintf(f, "; aiming (right); off: both as on the GameCube.\n");
    fprintf(f, "; Buttons: Xbox button = GameCube button: A (Fire) B (Jump) X (Morph Ball) Y (Missile)\n");
    fprintf(f, "; Z (Map) L (Lock On) R (Free Aim) START (Pause) UP DOWN LEFT RIGHT (the D-pad) NONE.\n");
    fprintf(f, "; Fire, Jump... work too. Some button must be A and some START, or the port is reset.\n");
    for (port = 0; port < 4; port++) {
        const xsdk_port_settings* ps = &st->port[port];
        fprintf(f, "[port%d]\nstick_deadzone = %d\ncstick_deadzone = %d\ntrigger_click = %d\ntrigger_z = %s\n",
                port + 1, pct(ps->stick_deadzone), pct(ps->cstick_deadzone), ps->trigger_click,
                ps->trigger_z == XSDK_TRIGGER_Z_LEFT    ? "left"
                : ps->trigger_z == XSDK_TRIGGER_Z_RIGHT ? "right"
                                                        : "off");
        for (i = 0; i < N_XBOX; i++) {
            char kv[32];
            snprintf(kv, sizeof kv, "%s = %s", k_xbox[i].name, gc_name(ps->bind[i].gc));
            fprintf(f, "%-16s; %s\n", kv, action_name(ps->bind[i].gc));
        }
        fprintf(f, "\n");
    }
    fprintf(f, END_MARK "\n");
}

int xsdk_settings_save(void) {
    char p[260], t[260];
    FILE* f;
    int ok, r = XHW_REPLACE_FAILED;
    path(p, sizeof p);
    tmp_path(t, sizeof t);
    f = fopen(t, "w");
    if (!f) {
        xhw_logf("[SETTINGS] save failed: can't create %s (errno %d)", t, errno);
        return 0;
    }
    write_all(f, &g_xsdk_settings);
    xhw_flush(f);
    ok = !ferror(f);
    if (fclose(f) != 0) ok = 0;
    /* only a complete copy replaces settings.ini */
    if (ok && (f = fopen(t, "r")) != NULL) {
        ok = (parse(f, &s_scratch) & SAW_END) && xsdk_settings_equal(&s_scratch, &g_xsdk_settings);
        fclose(f);
    } else {
        ok = 0;
    }
    if (ok) r = xhw_replace_file(t, p);
    if (r == XHW_REPLACE_LOST_TO) {
        /* settings.ini is gone, settings.tmp is complete: the next boot takes it */
        xhw_logf("[SETTINGS] save failed (rename): %s deleted, settings.tmp kept for the next boot", p);
        return 0;
    }
    if (r != XHW_REPLACE_OK) {
        xhw_logf("[SETTINGS] save failed (%s, errno %d): %s left as it was", ok ? "rename" : "write or read-back",
                 errno, p);
        remove(t);
        return 0;
    }
    log_summary("saved");
    return 1;
}

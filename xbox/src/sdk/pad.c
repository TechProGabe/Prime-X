/* pad.c - GameCube PAD on the four Xbox controller ports.
 *
 * Player N is physical port N (xbox/src/hw/xhw_pad.c). Two fixed layouts,
 * picked by Aiming (settings.ini aim); GameCube-like by position
 * (docs/architecture.md). Nothing can be remapped since 2026-11-03 (the
 * user: "remove button mapping entirely and just do two play schemes").
 *
 *   both:      A -> A (Fire)   X -> B (Jump)   B -> X (Morph Ball)
 *              Y -> Y (Missile)   White -> Z (Map)   Start -> Start
 *              D-pad -> D-pad (visors)   left stick -> control stick
 *              right stick -> C-stick   LT -> analog L (Lock On) and its
 *              click near the end of travel
 *   GameCube:  RT -> analog R (Free Aim) and its click   Black -> Z
 *   Dual Stick: RT -> A (Fire, as A: the user, 2026-11-03)   Black ->
 *              Beam Select (held, the right stick is the C-stick and picks
 *              beams; otherwise it aims, CDolphinController.cpp)
 *   BACK and the stick clicks do nothing (BACK: screenshots, xhw_pad.c).
 *
 * Sticks: a radial dead zone (worn Duke/S sticks rest well off centre;
 * OpenCrossing-Xbox measured 18-35%), then scaled so full tilt reaches the
 * GameCube's raw rim (~±104). HSD clamps to the game's 80-unit circle on its
 * own (controller.c), like a real controller. The dead zones and the
 * triggers' click point come from settings.ini (settings.c). */
#include <dolphin/pad.h>
#include <math.h>
#include <string.h>

#include "xhw.h"
#include "xsdk.h"
#include "xsdk_settings.h"

#define PORTS 4
#define STICK_RIM 104.0f

static int s_motor[PORTS];

BOOL PADInit(void) {
    xhw_pad_poll();
    return TRUE;
}

BOOL PADReset(u32 mask) { (void)mask; return TRUE; }
BOOL PADRecalibrate(u32 mask) { (void)mask; return TRUE; }
void PADSetSpec(u32 spec) { (void)spec; }
void PADSetAnalogMode(u32 mode) { (void)mode; }

static void stick(int16_t x, int16_t y, float deadzone, s8* ox, s8* oy) {
    float fx = x / 32767.0f, fy = y / 32767.0f;
    float mag = sqrtf(fx * fx + fy * fy), scale;
    if (mag <= deadzone || mag < 1e-6f) {
        *ox = *oy = 0;
        return;
    }
    if (mag > 1.0f) {
        fx /= mag;
        fy /= mag;
        mag = 1.0f;
    }
    /* rescale [deadzone, 1] -> [0, 1] along the same direction */
    scale = (mag - deadzone) / (1.0f - deadzone) / mag * STICK_RIM;
    fx *= scale;
    fy *= scale;
    *ox = (s8)(fx > 127 ? 127 : fx < -127 ? -127 : lroundf(fx));
    *oy = (s8)(fy > 127 ? 127 : fy < -127 ? -127 : lroundf(fy));
}

/* A menu is on screen: the game's menus call PortPointerMenu::Begin each
 * frame (port_overrides.cpp), which lands here. Then the Xbox B button is
 * the GameCube's B, back (the layouts put GameCube B on Xbox X, by
 * position; the user, round hw4: "use B in menus"); X is B there too. */
static unsigned s_menu_frame = 0x80000000u;
void xsdk_pad_menu_seen(void) { s_menu_frame = xsdk_frame_count(); }
static int menu_up(void) { return xsdk_frame_count() - s_menu_frame <= 2; }

/* Beam Select (Dual Stick's Black): not a GameCube button, taken out
 * before the game sees the buttons */
#define PAD_BEAM_SELECT 0x2000

/* the GameCube buttons of the Xbox buttons in `b` (the triggers apart) */
static u16 map_buttons(uint32_t b, int dual, int menu) {
    u16 out = 0;
    if (b & XHW_BTN_A) out |= PAD_BUTTON_A;
    if (b & XHW_BTN_X) out |= PAD_BUTTON_B;
    if (b & XHW_BTN_B) out |= menu ? PAD_BUTTON_B : PAD_BUTTON_X;
    if (b & XHW_BTN_Y) out |= PAD_BUTTON_Y;
    if (b & XHW_BTN_WHITE) out |= PAD_TRIGGER_Z;
    if (b & XHW_BTN_BLACK) out |= dual ? PAD_BEAM_SELECT : PAD_TRIGGER_Z;
    if (b & XHW_BTN_START) out |= PAD_BUTTON_START;
    if (b & XHW_BTN_UP) out |= PAD_BUTTON_UP;
    if (b & XHW_BTN_DOWN) out |= PAD_BUTTON_DOWN;
    if (b & XHW_BTN_LEFT) out |= PAD_BUTTON_LEFT;
    if (b & XHW_BTN_RIGHT) out |= PAD_BUTTON_RIGHT;
    return out;
}

/* Dual Stick aiming (GitHub #2): port 1's right stick at the last read,
 * past the C-stick dead zone and rescaled to -1..1 at full float precision
 * (the C-stick values are cut to the GameCube's +-59 gate by PADClamp:
 * ~60 steps for a whole half turn a second), and Beam Select held */
static float s_aim_x, s_aim_y;
static int s_aim_ok, s_beam_held;

int xsdk_pad_aim(float* x, float* y) {
    *x = s_aim_ok ? s_aim_x : 0.0f;
    *y = s_aim_ok ? s_aim_y : 0.0f;
    return s_aim_ok;
}
int xsdk_pad_beam_held(void) { return s_beam_held; }

static void aim_stick(int16_t x, int16_t y, float deadzone) {
    float fx = x / 32767.0f, fy = y / 32767.0f, mag = sqrtf(fx * fx + fy * fy), scale;
    if (mag <= deadzone || mag < 1e-6f) {
        s_aim_x = s_aim_y = 0.0f;
        return;
    }
    if (mag > 1.0f) mag = 1.0f;
    scale = (mag - deadzone) / (1.0f - deadzone) / sqrtf(fx * fx + fy * fy);
    s_aim_x = fx * scale;
    s_aim_y = fy * scale;
}

/* the controllers as PADRead last read them, for the settings menu (menu.c) */
static xhw_pad s_raw[PORTS];
static int s_raw_ok[PORTS];

const xhw_pad* xsdk_pad_raw(int port) {
    return port >= 0 && port < PORTS && s_raw_ok[port] ? &s_raw[port] : NULL;
}

/* Presses between two reads. The game reads the pad once a tick; with Game
 * Timing: Auto at 30 Hz ticks that is every other frame, and a tap shorter
 * than that fell between two reads. A frame boundary that saw no PADRead
 * since the last one samples the buttons, and the next read sees them held
 * (its release comes a read later). While reads keep up (60 Hz ticks: one
 * or more a frame) nothing is latched. -DXSDK_PAD_LATCH=0 turns it off. */
#ifndef XSDK_PAD_LATCH
#define XSDK_PAD_LATCH 1
#endif
static uint32_t s_latch[PORTS];
static int s_reads;

void xsdk_pad_frame(void) {
    int i;
    if (!XSDK_PAD_LATCH) return;
    if (s_reads) {
        s_reads = 0;
        memset(s_latch, 0, sizeof s_latch);
        return;
    }
    xhw_pad_poll();
    for (i = 0; i < PORTS; i++) {
        xhw_pad p;
        if (xhw_pad_get(i, &p)) s_latch[i] |= p.buttons;
    }
}

u32 PADRead(PADStatus* status) {
    u32 motors = 0;
    uint32_t all = 0;
    int i, block;
    xhw_pad_poll();
    for (i = 0; i < PORTS; i++) {
        s_raw_ok[i] = xhw_pad_get(i, &s_raw[i]);
        if (s_raw_ok[i]) s_raw[i].buttons |= s_latch[i];
        s_latch[i] = 0;
        if (s_raw_ok[i]) all |= s_raw[i].buttons;
    }
    s_reads++;
    /* the settings menu is up, or its closing press is still held */
    block = xsdk_menu_block(all);
    s_aim_ok = 0;
    s_beam_held = 0;
    for (i = 0; i < PORTS; i++) {
        PADStatus* s = &status[i];
        const xsdk_port_settings* ps = &g_xsdk_settings.port[i];
        xhw_pad p = s_raw[i];
        memset(s, 0, sizeof *s);
        if (!s_raw_ok[i]) {
            s->err = PAD_ERR_NO_CONTROLLER;
            continue;
        }
        s->err = PAD_ERR_NONE;
        /* in-game reset: L + R + BACK + BLACK on any controller, back to
         * the dashboard (not the usual BACK + START: L + R + START is
         * Melee's own reset from the pause menu); also with the settings
         * menu up */
        if ((p.buttons & (XHW_BTN_BACK | XHW_BTN_BLACK)) == (XHW_BTN_BACK | XHW_BTN_BLACK) && p.lt > 200 &&
            p.rt > 200) {
            xhw_logf("[PAD] port %d: in-game reset, back to the dashboard", i + 1);
            xhw_exit_reason("in-game reset (L+R+BACK+BLACK)");
            xhw_quit_to_dashboard();
        }
        if (block) {   /* connected, nothing pressed */
            motors |= 0x80000000u >> i;
            continue;
        }
        {
            const int dual = g_xsdk_settings.aim == XSDK_AIM_DUAL;
            s->button = map_buttons(p.buttons, dual, menu_up());
            if (s->button & PAD_BEAM_SELECT) {   /* not a GameCube button */
                s->button &= (u16)~PAD_BEAM_SELECT;
                if (i == 0) s_beam_held = 1;
            }
            /* LT is the GameCube's L: how far in, and its click past
             * trigger_click. RT is R the same way under GameCube aiming, and
             * Fire (A) past its click under Dual Stick. */
            s->triggerLeft = p.lt;
            if (p.lt >= ps->trigger_click) s->button |= PAD_TRIGGER_L;
            if (dual) {
                if (p.rt >= ps->trigger_click) s->button |= PAD_BUTTON_A;
            } else {
                s->triggerRight = p.rt;
                if (p.rt >= ps->trigger_click) s->button |= PAD_TRIGGER_R;
            }
            if (i == 0 && dual && !menu_up()) {
                aim_stick(p.rx, p.ry, ps->cstick_deadzone);
                s_aim_ok = 1;
            }
        }
        stick(p.lx, p.ly, ps->stick_deadzone, &s->stickX, &s->stickY);
        stick(p.rx, p.ry, ps->cstick_deadzone, &s->substickX, &s->substickY);
        s->analogA = (s->button & PAD_BUTTON_A) ? 0xFF : 0;
        s->analogB = (s->button & PAD_BUTTON_B) ? 0xFF : 0;
        if (s_motor[i] == PAD_MOTOR_RUMBLE) {
            /* 75% of the motor at rumble = 100: full strength was too much
             * on the console's controllers */
            uint16_t lvl = (uint16_t)(g_xsdk_settings.rumble * 0.75f * 65535.0f);
            xhw_pad_rumble(i, lvl, lvl);
        }
        motors |= 0x80000000u >> i;
    }
    return motors;
}

void PADControlMotor(u32 chan, u32 cmd) {
    static unsigned logged;
    if (chan >= PORTS) return;
    if (cmd == PAD_MOTOR_RUMBLE && !(logged & (1u << chan))) {   /* does the game ask at all? */
        logged |= 1u << chan;
        xhw_logf("[PAD] port %u: rumble on (strength %d%%)", (unsigned)chan + 1,
                 (int)(g_xsdk_settings.rumble * 100.0f + 0.5f));
    }
    if (s_motor[chan] == PAD_MOTOR_RUMBLE && cmd != PAD_MOTOR_RUMBLE) xhw_pad_rumble((int)chan, 0, 0);
    s_motor[chan] = (int)cmd;
}

void PADControlAllMotors(const u32* cmd) {
    int i;
    for (i = 0; i < PORTS; i++) PADControlMotor((u32)i, cmd[i]);
}

/* rumble.c (TARGET_PC) always goes through this; netplay isn't built */

/* Stick clamps from the SDK, for anything that calls them directly. */
static void clamp_stick(s8* px, s8* py, int max, int min) {
    int x = *px, y = *py, sx = x < 0 ? -1 : 1, sy = y < 0 ? -1 : 1;
    x = x * sx;
    y = y * sy;
    x = x < min ? 0 : x - min;
    y = y < min ? 0 : y - min;
    if (x > max) x = max;
    if (y > max) y = max;
    *px = (s8)(x * sx);
    *py = (s8)(y * sy);
}

void PADClamp(PADStatus* s) {
    int i;
    for (i = 0; i < PORTS; i++) {
        if (s[i].err != PAD_ERR_NONE) continue;
        clamp_stick(&s[i].stickX, &s[i].stickY, 72, 15);
        clamp_stick(&s[i].substickX, &s[i].substickY, 59, 15);
    }
}

void PADClampCircle(PADStatus* s) { PADClamp(s); }

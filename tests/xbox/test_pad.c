/* test_pad.c - pad.c's two fixed layouts on the host (tools/xbox/test_pad.py
 * builds it with pad.c included): what the game's PADRead gets for each Xbox
 * input under Dual Stick and GameCube controls (2026-11-03: no remapping,
 * Dual Stick fires with A or RT). */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pad.c"

xsdk_settings g_xsdk_settings;
static xhw_pad g_pad;
static unsigned g_frame = 1000;

void xhw_pad_poll(void) {}
int xhw_pad_get(int port, xhw_pad* out) {
    if (port != 0) return 0;
    *out = g_pad;
    return 1;
}
void xhw_pad_rumble(int port, uint16_t low, uint16_t high) { (void)port, (void)low, (void)high; }
void xhw_logf(const char* fmt, ...) { (void)fmt; }
void xhw_exit_reason(const char* why) { (void)why; }
void xhw_quit_to_dashboard(void) {}
unsigned xsdk_frame_count(void) { return g_frame; }
int xsdk_menu_block(uint32_t raw) { (void)raw; return 0; }

static int fails;
#define CHECK(c)                                                \
    do {                                                        \
        if (!(c)) {                                             \
            printf("FAIL line %d: %s\n", __LINE__, #c);         \
            fails++;                                            \
        }                                                       \
    } while (0)

/* port 1 with these buttons and triggers, as the game reads it */
static PADStatus read(uint32_t buttons, uint8_t lt, uint8_t rt) {
    PADStatus s[PORTS];
    memset(&g_pad, 0, sizeof g_pad);
    g_pad.connected = 1;
    g_pad.buttons = buttons;
    g_pad.lt = lt;
    g_pad.rt = rt;
    g_frame += 10;   /* no menu up */
    PADRead(s);
    return s[0];
}

int main(void) {
    int p;
    PADStatus s;
    memset(&g_xsdk_settings, 0, sizeof g_xsdk_settings);
    for (p = 0; p < 4; p++) {
        g_xsdk_settings.port[p].stick_deadzone = 0.2f;
        g_xsdk_settings.port[p].cstick_deadzone = 0.25f;
        g_xsdk_settings.port[p].trigger_click = 230;
    }

    /* Dual Stick: A and RT both fire; RT gives no analog R */
    g_xsdk_settings.aim = XSDK_AIM_DUAL;
    s = read(XHW_BTN_A, 0, 0);
    CHECK(s.button == PAD_BUTTON_A && s.analogA == 0xFF);
    s = read(0, 0, 255);
    CHECK(s.button == PAD_BUTTON_A && s.triggerRight == 0);
    s = read(0, 0, 200);   /* not past the click: nothing */
    CHECK(s.button == 0 && s.triggerRight == 0);
    s = read(0, 255, 0);   /* LT: Lock On, analog and click */
    CHECK(s.button == PAD_TRIGGER_L && s.triggerLeft == 255);
    s = read(XHW_BTN_X, 0, 0);
    CHECK(s.button == PAD_BUTTON_B);
    s = read(XHW_BTN_B, 0, 0);
    CHECK(s.button == PAD_BUTTON_X);
    s = read(XHW_BTN_Y | XHW_BTN_WHITE | XHW_BTN_START, 0, 0);
    CHECK(s.button == (PAD_BUTTON_Y | PAD_TRIGGER_Z | PAD_BUTTON_START));
    s = read(XHW_BTN_BLACK, 0, 0);   /* Beam Select: no GameCube button */
    CHECK(s.button == 0 && xsdk_pad_beam_held());
    s = read(XHW_BTN_BACK | XHW_BTN_LSTICK | XHW_BTN_RSTICK, 0, 0);
    CHECK(s.button == 0 && !xsdk_pad_beam_held());
    s = read(XHW_BTN_UP | XHW_BTN_LEFT, 0, 0);
    CHECK(s.button == (PAD_BUTTON_UP | PAD_BUTTON_LEFT));

    /* GameCube: RT is Free Aim (analog R and its click), Black the map */
    g_xsdk_settings.aim = XSDK_AIM_GAMECUBE;
    s = read(0, 0, 255);
    CHECK(s.button == PAD_TRIGGER_R && s.triggerRight == 255);
    s = read(0, 0, 120);
    CHECK(s.button == 0 && s.triggerRight == 120);
    s = read(XHW_BTN_A, 0, 0);
    CHECK(s.button == PAD_BUTTON_A);
    s = read(XHW_BTN_BLACK, 0, 0);
    CHECK(s.button == PAD_TRIGGER_Z && !xsdk_pad_beam_held());

    /* a menu up: B is back (GameCube B), X too */
    g_xsdk_settings.aim = XSDK_AIM_DUAL;
    xsdk_pad_menu_seen();
    {
        PADStatus st[PORTS];
        memset(&g_pad, 0, sizeof g_pad);
        g_pad.connected = 1;
        g_pad.buttons = XHW_BTN_B;
        PADRead(st);
        CHECK(st[0].button == PAD_BUTTON_B);
        g_pad.buttons = XHW_BTN_X;
        PADRead(st);
        CHECK(st[0].button == PAD_BUTTON_B);
        g_pad.buttons = 0;
        g_pad.rt = 255;   /* RT confirms there, as A */
        PADRead(st);
        CHECK(st[0].button == PAD_BUTTON_A);
    }

    printf(fails ? "test_pad: %d failed\n" : "test_pad: all passed\n", fails);
    return fails != 0;
}

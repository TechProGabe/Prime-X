/* xsdk_settings.h - settings.ini in the save folder (settings.c), and the
 * settings menu on the title screen (menu.c). */
#ifndef XSDK_SETTINGS_H
#define XSDK_SETTINGS_H
#include <stdint.h>

/* Each controller port. The buttons are fixed by Aiming (pad.c): there is
 * no remapping since 2026-11-03 (the user: "remove button mapping entirely
 * and just do two play schemes"). */
typedef struct {
    float stick_deadzone;    /* radial, fraction of full tilt */
    float cstick_deadzone;
    uint8_t trigger_click;   /* analog value from which LT and RT count as pressed */
} xsdk_port_settings;

/* Game Timing, in the Port Options row's order. Auto: 60 game updates a
 * second while the frame fits, 30 (drawn at 60, blended between updates)
 * when it doesn't. Fixed: 60 a second, as the GameCube (a slow frame runs
 * more updates). Flex: one update a frame, dt = the frame's time. */
enum { XSDK_SIM_AUTO = 0, XSDK_SIM_FIXED, XSDK_SIM_FLEX };

/* Aiming (GitHub #2), which is also the button layout (pad.c): GameCube
 * (RT and the left stick aim, the right stick picks beams) or Dual Stick
 * (the right stick aims, the left one moves and strafes, as the Remastered
 * edition; A or RT fires, Black held makes the right stick pick beams) */
enum { XSDK_AIM_GAMECUBE = 0, XSDK_AIM_DUAL };
enum { XSDK_AIM_SPEED_LOW = 0, XSDK_AIM_SPEED_MEDIUM, XSDK_AIM_SPEED_HIGH };

typedef struct {
    int video_720p;          /* 1: use 720p when the dashboard allows it (experimental) */
    int progressive;         /* 1: 480p when the dashboard allows it; 0: 480i */
    int widescreen;          /* at 480: 1 as the dashboard says, 0 always 4:3, 2 always 16:9 (720p is 16:9) */
    int fps;                 /* 1: frame-rate counter on screen */
    int shots;               /* 1: BACK takes a screenshot (shotNN.bmp in the save folder) */
    int led;                 /* 1: front LED effects (KOs, the last seconds, GAME!) */
    int log;                 /* 1: boot.log in the save folder (ship builds; test builds always) */
    float rumble;            /* 0..1 */
    int cstick_1p;           /* 1: the C-stick attacks in 1P modes too (training...), not the camera (GitHub #13) */
    int hud_wide;            /* 1: at 16:9 the HUD spreads to the screen's edges (PortDebug::HudWide) */
    int fov;                 /* first-person field of view, degrees (55: the GameCube's) */
    int skip_cutscenes;      /* 1: every cutscene can be skipped (PortSkipCutscenes) */
    int fusion_suit;         /* 1: Samus wears the Fusion Suit (the GBA-link bonus; CSystemState starts from it) */
    int sim_mode;            /* XSDK_SIM_*: Game Timing (port_debug.cpp) */
    int aim;                 /* XSDK_AIM_*: Aiming */
    int aim_speed;           /* XSDK_AIM_SPEED_*: Dual Stick's turn rate */
    xsdk_port_settings port[4];
} xsdk_settings;

extern xsdk_settings g_xsdk_settings;
extern xsdk_settings g_xsdk_settings_boot;   /* as loaded at boot: what the video mode uses */

void xsdk_settings_defaults(xsdk_settings* st);   /* what a first boot writes */
void xsdk_settings_load(void);
int xsdk_settings_save(void);   /* 0: failed, settings.ini left as it was */
int xsdk_settings_equal(const xsdk_settings* a, const xsdk_settings* b);   /* as settings.ini would hold them */
int xsdk_fusion_suit(void);     /* game code (CGameState.cpp PORT_XBOX): g_xsdk_settings.fusion_suit */
int xsdk_cstick_1p(void);       /* game code (fighter.c, camera.c PORT): g_xsdk_settings.cstick_1p */

/* pad.c, for Dual Stick aiming (CDolphinController.cpp): port 1's right
 * stick at its last PADRead, past its dead zone and rescaled, -1..1 (y up);
 * 0 while a menu is up, the popup holds the pad, or there is no controller.
 * Beam Select held at that read: the right stick stays the C-stick. */
int xsdk_pad_aim(float* x, float* y);
int xsdk_pad_beam_held(void);

/* The Port Options popup (xbox/src/port/port_menu.cpp): PADRead hands the
 * game neutral input while xsdk_menu_block says so. */
int xsdk_menu_block(uint32_t raw_buttons);

#endif

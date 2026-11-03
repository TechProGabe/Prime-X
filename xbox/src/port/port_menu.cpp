// port_menu.cpp - the Port Options popup (docs/ui.md "Port Options"): what
// it shows and what its buttons do. The card is drawn by
// xbox/src/hw/xhw_menu.c from the view filled here once a frame; the rows
// change settings through PortXboxOptions (port_xbox_options.cpp), the same
// calls the game's own Options rows made before, so every row applies at
// once (Video Mode: at the next start) and settings.ini is written once,
// when the popup closes.
//
// The user (2026-11-01): "make a clear custom menu pop up that explains
// everything. add button mapping here, we can use the button mapping tech
// from recomp-x". So: tabs (Video, Controller, Buttons, Gameplay, System),
// rows of named choices with arrows and pips, a box saying what the row does
// and what the value shown does, and on the Buttons tab an Xbox controller
// with a callout for every input (N64Recomp-X's Controls page).
//
// Input: the Xbox controllers as they are (not the remapped GameCube pad), so
// a layout can never lock the player out: up/down (D-pad or left stick,
// held to repeat) pick, left/right change, A changes too, LT/RT switch tabs,
// Y twice restores the tab's defaults, B or START close. On the Buttons tab
// the D-pad moves over the callouts as they stand (up/down in a column,
// left/right across), A steps the action on, and X binds: the next button
// or trigger pressed takes the selected input's action (B, START, LT and RT
// too; the D-pad cancels, so do 5 s without a press). It makes no
// sound (the user, 2026-11-01, when the music skipped on the consoles; the
// skips were the card's redraw, xhw_menu.c). The game reads a neutral pad
// until the buttons that closed it are let go (xsdk_menu_block, pad.c).
//
// The game is held while the popup is up (RunModal): beta2's testers found
// it "heavy and laggy" (the game ran on behind it, every frame waited for
// the GPU and copied the card in, and a change took several of the game's
// frames to appear); the user: "streamline the whole thing for performance".
// Closing with a new Video Mode asks whether to restart now (a tester's
// idea), "Later" first: a dialog of its own (page 2, the two choices side by
// side), the D-pad or the left stick picks, as on the rest of the popup.
#include "port_xbox_options.h"
#include "port_debug.h"

#include "MetroidPrime/Player/CGameState.hpp"
#include "Kyoto/CSimplePool.hpp"
#include "Kyoto/Graphics/CTexture.hpp"
#include "Kyoto/Text/CRasterFont.hpp"
#include "port_font_accent.h"

#include <vector>

#include <dolphin/pad.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xhw.h"
extern "C" {
#include "xsdk_settings.h"
void xsdk_modal_tick(void); // vi.c: a frame of the modal loop
}

namespace PortXboxOptions {
namespace {

using O = int;

// a choice row: its option, its values' names, and what each one does
struct SRow {
  const char* label;
  O option;
  int count; // values (2 or 3); 0: an action row
  const char* values[3];
  const char* help;
  const char* valueHelp[3];
  bool restart; // takes effect at the next start
};

enum { kTabVideo, kTabController, kTabButtons, kTabGameplay, kTabSystem, kTabCount };
const char* const kTabNames[kTabCount] = {"Video", "Controller", "Buttons", "Gameplay", "System"};

const O kActRestoreAll = -1; // System: every Port Options setting back to the shipped one

const SRow kVideo[] = {
    {"Video Mode",
     kXO_VideoOutput,
     3,
     {"480i", "480p", "720p"},
     "The picture signal the console sends. Hold BACK while Prime-X starts to get 480i back.",
     {"Standard definition, works on every TV and cable.",
      "Sharper standard definition. Needs component cables and 480p on in the Xbox dashboard.",
      "HD, always 16:9. Needs component cables and 720p on in the dashboard; plays at about 30 fps."},
     true},
    {"Widescreen",
     kXO_Widescreen,
     3,
     {"Auto", "4:3", "16:9"},
     "The picture's shape at 480i and 480p (720p is always 16:9).",
     {"Follows the Xbox dashboard's widescreen setting.", "The standard TV shape, as on the GameCube.",
      "Widescreen: more of the world to the sides. For a widescreen TV."},
     false},
    {"Widescreen HUD",
     kXO_WidescreenHud,
     2,
     {"Inner", "Edges"},
     "Where the visor's readouts sit on a 16:9 picture.",
     {"Kept to the middle, where they are on a 4:3 screen.", "Spread out to the sides of the screen."},
     false},
    {"Field of View",
     kXO_Fov,
     3,
     {"55", "65", "75"},
     "How wide Samus sees, in degrees.",
     {"The GameCube's view.", "A little wider.", "The widest: more on screen, more work for the console."},
     false},
    {"Frame Rate",
     kXO_FpsCounter,
     2,
     {"Hide", "Show"},
     "A frames-per-second counter in the top-left corner.",
     {"No counter.", "Shows how many frames a second the console draws."},
     false},
};
const SRow kController[] = {
    {"Aiming",
     kXO_AimMode,
     2,
     {"GameCube", "Dual Stick"},
     "How you aim and look around. Dual Stick is the modern way, as in the Remastered edition.",
     {"Hold RT and tilt the left stick to aim; the right stick picks beams.",
      "The right stick aims and looks, the left one moves and strafes. Hold Beam Select (Buttons tab) "
      "and tilt the right stick to pick a beam."},
     false},
    {"Aim Speed",
     kXO_AimSpeed,
     3,
     {"Low", "Medium", "High"},
     "How fast the right stick turns the view with Dual Stick aiming. A light tilt always aims finely.",
     {"120 degrees a second at full tilt.", "180 degrees a second at full tilt.",
      "250 degrees a second at full tilt."},
     false},
    {"Rumble Strength",
     kXO_RumbleStrength,
     3,
     {"Low", "Medium", "Full"},
     "How strongly the controller shakes. Rumble itself is turned on and off in the game's own Options.",
     {"A light buzz (40%).", "Noticeable but softer (70%).", "Full strength."},
     false},
    {"Stick Dead Zone",
     kXO_StickDeadZone,
     3,
     {"Low", "Medium", "High"},
     "How far the left stick moves before Samus does. Raise it if she drifts with the stick let go.",
     {"10%: the most responsive, for sticks in good shape.", "20%: suits most controllers.",
      "30%: for worn sticks that don't come back to the middle."},
     false},
    {"C-Stick Dead Zone",
     kXO_CStickDeadZone,
     3,
     {"Low", "Medium", "High"},
     "The same for the right stick (beams, or aiming with Dual Stick). Raise it if beams change or the view drifts on its own.",
     {"15%: the most responsive.", "25%: suits most controllers.", "35%: for worn sticks."},
     false},
    {"Trigger Click",
     kXO_TriggerClick,
     3,
     {"Light", "Medium", "Firm"},
     "How far LT and RT go in before they count as pressed (lock on, free aim).",
     {"A light pull is enough.", "About nine tenths of the way in.", "Nearly all the way in."},
     false},
};
const SRow kGameplay[] = {
    {"Skip Cutscenes",
     kXO_SkipCutscenes,
     2,
     {"Off", "On"},
     "Lets START skip any cutscene, not only the ones the GameCube lets you skip. From the next room.",
     {"Cutscenes as on the GameCube.", "START skips every cutscene."},
     false},
    {"Fusion Suit",
     kXO_FusionSuit,
     2,
     {"Off", "On"},
     "The Fusion Suit was a reward for linking a Game Boy Advance. An Xbox can't link, so it is here.",
     {"Samus's own Power Suit.", "Samus wears the Fusion Suit, in every game, after a restart too."},
     false},
};
const SRow kSystem[] = {
    {"Screen Capture",
     kXO_Screenshots,
     2,
     {"Off", "On"},
     "Screenshots of the game, saved as shotNN.bmp next to settings.ini (E:\\UDATA\\50580001).",
     {"BACK does what the Buttons tab gives it.", "Every press of BACK saves a screenshot."},
     false},
    {"Log File",
     kXO_LogFile,
     2,
     {"Off", "On"},
     "boot.log in the save folder: what Prime-X did, for bug reports.",
     {"Nothing is written while you play (best on a stock hard drive).",
      "Writes boot.log. Turn it on before reproducing a bug, then attach the file."},
     false},
    {"Restore All Defaults",
     kActRestoreAll,
     0,
     {"Press A", "", ""},
     "Every Port Options setting back to how Prime-X ships, buttons included. The game's own Options stay.",
     {"", "", ""},
     false},
};

struct STab {
  const SRow* rows;
  int count;
};
const STab kTabs[kTabCount] = {
    {kVideo, sizeof kVideo / sizeof kVideo[0]},
    {kController, sizeof kController / sizeof kController[0]},
    {nullptr, 0},
    {kGameplay, sizeof kGameplay / sizeof kGameplay[0]},
    {kSystem, sizeof kSystem / sizeof kSystem[0]},
};

// ---- the Buttons tab ----
// the remappable inputs in the card's order (xhw.h XHW_MENU_IN_*), which is
// settings.ini's binding order (settings.c k_xbox)
const char* const kButtonNames[XHW_MENU_IN_MAPPABLE] = {
    "A button", "B button",         "X button",          "Y button",     "White button", "Black button",
    "START",    "BACK",             "Left stick click",  "Right stick click", "LT",      "RT"};
struct SAction {
  uint16_t gc;
  const char* name; // fits a callout
  const char* help;
};
const SAction kActionHelp[] = {
    {0, "None", "Does nothing."},
    {PAD_BUTTON_A, "Fire", "Fires the beam; hold it to charge. A in the game's menus."},
    {PAD_BUTTON_B, "Jump", "Jumps; press again in the air once Samus has the Space Jump Boots."},
    {PAD_BUTTON_X, "Morph Ball", "Rolls into the Morph Ball and back."},
    {PAD_BUTTON_Y, "Missile", "Fires a missile."},
    {PAD_TRIGGER_Z, "Map", "Opens the map."},
    {PAD_TRIGGER_L, "Lock On", "Locks on to a target while held; with nothing to lock on to, Samus strafes."},
    {PAD_TRIGGER_R, "Free Aim", "Aims and looks around freely while held."},
    {PAD_BUTTON_START, "Pause", "The pause screen: inventory, log book, Options."},
    {PAD_BUTTON_UP, "D-Up", "The D-pad's up: picks a visor (or a beam with Swap Beam Controls)."},
    {PAD_BUTTON_DOWN, "D-Down", "The D-pad's down: picks a visor (or a beam with Swap Beam Controls)."},
    {PAD_BUTTON_LEFT, "D-Left", "The D-pad's left: picks a visor (or a beam with Swap Beam Controls)."},
    {PAD_BUTTON_RIGHT, "D-Right", "The D-pad's right: picks a visor (or a beam with Swap Beam Controls)."},
    {XSDK_PAD_BEAM, "Beams",
     "Beam Select, for Dual Stick aiming: hold it and tilt the right stick to pick a beam, as the GameCube's C-stick."},
};
const SAction& ActionOf(int gc) {
  for (const SAction& a : kActionHelp) {
    if (a.gc == gc) {
      return a;
    }
  }
  return kActionHelp[0];
}

// the Buttons page's callouts as the card lays them out (xhw_menu.c k_call):
// two columns, top to bottom, with each one's top in card pixels
struct SPadSpot {
  int in;
  int y;
};
const SPadSpot kPadLeft[] = {{XHW_MENU_IN_LT, 92},    {XHW_MENU_IN_LSTICK, 124}, {XHW_MENU_IN_LCLICK, 156},
                             {XHW_MENU_IN_BACK, 188}, {XHW_MENU_IN_START, 220},  {XHW_MENU_IN_DPAD, 252}};
const SPadSpot kPadRight[] = {{XHW_MENU_IN_RT, 92},     {XHW_MENU_IN_Y, 114},      {XHW_MENU_IN_X, 136},
                              {XHW_MENU_IN_B, 158},     {XHW_MENU_IN_A, 180},      {XHW_MENU_IN_WHITE, 202},
                              {XHW_MENU_IN_BLACK, 224}, {XHW_MENU_IN_RSTICK, 246}, {XHW_MENU_IN_RCLICK, 268}};
const int kPadLeftN = sizeof kPadLeft / sizeof kPadLeft[0];
const int kPadRightN = sizeof kPadRight / sizeof kPadRight[0];

// the input the D-pad moves to from `in`: dir 1 up, 2 down, 4 left, 8 right
int PadMove(int in, int dir) {
  for (int c = 0; c < 2; ++c) {
    const SPadSpot* col = c ? kPadRight : kPadLeft;
    const int n = c ? kPadRightN : kPadLeftN;
    for (int i = 0; i < n; ++i) {
      if (col[i].in != in) {
        continue;
      }
      if (dir & 1) {
        return col[(i + n - 1) % n].in;
      }
      if (dir & 2) {
        return col[(i + 1) % n].in;
      }
      // left from the right column, right from the left: the other column's nearest
      if ((dir & 4) ? c == 0 : c == 1) {
        return in;
      }
      const SPadSpot* other = c ? kPadLeft : kPadRight;
      const int on = c ? kPadLeftN : kPadRightN;
      int best = 0;
      for (int k = 1; k < on; ++k) {
        if (abs(other[k].y - col[i].y) < abs(other[best].y - col[i].y)) {
          best = k;
        }
      }
      return other[best].in;
    }
  }
  return XHW_MENU_IN_A;
}

// the inputs that keep their GameCube part (pad.c): what they do and why
struct SFixed {
  int in;
  const char* help;
  const char* value;
};
const SFixed kFixed[] = {
    {XHW_MENU_IN_DPAD, "D-pad: Visors. Fixed: the GameCube's D-pad.",
     "Picks a visor; with Swap Beam Controls on, it picks a beam."},
    {XHW_MENU_IN_LSTICK, "Left stick: Move. Fixed: the GameCube's control stick.",
     "Moves and turns Samus; with RT held, aims."},
    {XHW_MENU_IN_RSTICK, "Right stick: Beams. Fixed: the GameCube's C-stick.",
     "Picks a beam; with Swap Beam Controls on, it picks a visor."},
};

// ---- state ----
bool sOpen = false;
bool sAsk = false; // closing with a new Video Mode: restart now?
int sAskSel;       // 0 later, 1 now
bool sAskStick;    // the left stick was pushed last frame (a push picks once)
int sRestarting;   // "Restart now" taken: frames spent drawing "Restarting" (0: not)
bool sRelease = false; // closed: the game waits for the buttons to be let go
int sTab = kTabVideo;
int sSel[kTabCount];
uint32_t sPrev;        // buttons held last frame (every port)
int sPrevTrig;         // bit 0 LT, bit 1 RT past the click
int sDir, sDirNext;    // the held direction (bit: up 1, down 2, left 4, right 8) and its next repeat (ms)
int sDirFrames;        // frames until it may repeat (counts up to 1)
uint32_t sConfirmUntil; // Y once: a second Y before this restores (ms); 0 none
int sConfirmTab;
xsdk_settings sDefaults;
// Bind mode (the Buttons tab, X): the input whose action the next press of
// a button or trigger takes, -1 none; it gives up after kListenMs and
// kListenFrames both (by time alone, xemu's slow frames ran it out before
// a scripted press)
int sListen = -1;
uint32_t sListenUntil;
int sListenFrames;
const uint32_t kListenMs = 5000;
const int kListenFrames = 150;
xhw_pad sPad1; // port 1 this frame (the Buttons tab's live controller)
bool sPad1Ok;
xhw_menu_view sView;

uint32_t NowMs() { return static_cast< uint32_t >(xhw_time_ns() / 1000000ull); }


int RowCount(int tab) { return tab == kTabButtons ? XHW_MENU_IN_MAPPABLE : kTabs[tab].count; }

// what a row would show with the defaults: Get() reads g_xsdk_settings
int DefaultValue(O option) {
  const xsdk_settings now = g_xsdk_settings;
  g_xsdk_settings = sDefaults;
  const int v = Get(option);
  g_xsdk_settings = now;
  return v;
}

void SetValue(O option, int value) { Set(option, value); }

void RestoreTab(int tab) {
  if (tab == kTabButtons) {
    // Step swaps; set each port's bindings straight back instead (the
    // buttons, the D-pad and the triggers; RT's default follows Aiming)
    for (int p = 0; p < 4; ++p) {
      for (int b = 0; b < XSDK_BIND_COUNT; ++b) {
        g_xsdk_settings.port[p].bind[b].gc = sDefaults.port[p].bind[b].gc;
      }
    }
    for (int p = 0; p < 4; ++p) {
      g_xsdk_settings.port[p].bind[15].gc = xsdk_rt_default(g_xsdk_settings.aim); // settings.c rt
    }
    MarkChanged();
    return;
  }
  for (int r = 0; r < kTabs[tab].count; ++r) {
    const SRow& row = kTabs[tab].rows[r];
    if (row.count > 0) {
      SetValue(row.option, DefaultValue(row.option));
    }
  }
}

void RestoreAll() {
  for (int t = 0; t < kTabCount; ++t) {
    RestoreTab(t);
  }
}

// the video mode the console is in now, top right
void Status(char* out, size_t cap) {
  const xhw_video_mode* vm = xhw_video();
  const char* mode = vm->height >= 720 ? "720p" : vm->progressive ? "480p" : "480i";
  snprintf(out, cap, "%s %s", mode, PortDebug::AspectMode() == PortDebug::kAspect_16_9 ? "16:9" : "4:3");
}

void Fill() {
  xhw_menu_view& v = sView;
  memset(&v, 0, sizeof v);
  v.tabs = kTabCount;
  v.tab = sTab;
  for (int t = 0; t < kTabCount; ++t) {
    snprintf(v.tab_name[t], sizeof v.tab_name[t], "%s", kTabNames[t]);
  }
  Status(v.status, sizeof v.status);
  if (sAsk) { // closing with a new Video Mode
    static const char* const kModes[3] = {"480i", "480p", "720p"};
    const int mode = Get(kXO_VideoOutput);
    // page 2: a dialog of its own, no tabs, the two choices side by side
    // (the user, round hw19: the stick did nothing here and the rows looked
    // like settings to change, not a question)
    v.page = 2;
    v.pad_sel = -1;
    if (sRestarting != 0) {
      v.rows = 0;
      snprintf(v.help, sizeof v.help, "Restarting Prime-X in %s...", kModes[mode < 0 || mode > 2 ? 0 : mode]);
      return;
    }
    v.rows = 2;
    v.sel = sAskSel;
    snprintf(v.label[0], sizeof v.label[0], "Later");
    snprintf(v.label[1], sizeof v.label[1], "Restart now");
    snprintf(v.help, sizeof v.help, "Restart Prime-X to use %s?", kModes[mode < 0 || mode > 2 ? 0 : mode]);
    snprintf(v.value_help, sizeof v.value_help, "%s",
             sAskSel ? "Saves the settings and starts Prime-X again now. Progress since your last save is lost."
                     : "Back to the game. The new mode is used the next time Prime-X starts.");
    return;
  }
  const int sel = sSel[sTab];
  v.pad_sel = -1;
  if (sTab == kTabButtons) {
    v.page = 1;
    v.pad_sel = sel;
    for (int b = 0; b < XHW_MENU_IN_MAPPABLE; ++b) {
      const int gc = BindGc(b);
      snprintf(v.pad_map[b], sizeof v.pad_map[b], "%s", ActionOf(gc).name);
      v.pad_changed[b] = gc != DefaultBindGc(b);
    }
    // the inputs that keep their GameCube part (pad.c)
    const bool swap = gpGameState != nullptr && gpGameState->GameOptions().GetSwapBeamControls();
    const bool dual = g_xsdk_settings.aim == XSDK_AIM_DUAL;
    snprintf(v.pad_map[XHW_MENU_IN_DPAD], sizeof v.pad_map[0], "%s", swap ? "Beams" : "Visors");
    snprintf(v.pad_map[XHW_MENU_IN_LSTICK], sizeof v.pad_map[0], "Move");
    snprintf(v.pad_map[XHW_MENU_IN_RSTICK], sizeof v.pad_map[0], "%s", dual ? "Aim" : swap ? "Visors" : "Beams");
    // No live lighting of what is held: every press redrew the controller
    // (twice: down and up), the lag a tester felt moving around this page
    v.pad_ok = sPad1Ok ? 1 : 0;
    if (sel >= XHW_MENU_IN_MAPPABLE) {
      for (const SFixed& f : kFixed) {
        if (f.in == sel) {
          snprintf(v.help, sizeof v.help, "%s", f.help);
          snprintf(v.value_help, sizeof v.value_help, "%s", f.value);
        }
      }
      if (dual && sel == XHW_MENU_IN_RSTICK) {
        snprintf(v.help, sizeof v.help, "Right stick: Aim (Dual Stick, Controller tab). Fixed.");
        snprintf(v.value_help, sizeof v.value_help,
                 "Aims and looks around. Held, Beam Select makes it pick beams, as the GameCube's C-stick.");
      } else if (dual && sel == XHW_MENU_IN_LSTICK) {
        snprintf(v.value_help, sizeof v.value_help, "Moves and strafes Samus (Dual Stick aiming).");
      }
      snprintf(v.note, sizeof v.note, "This input can't be remapped.");
    } else if (sListen >= 0) { // bind mode: waiting for the press
      const SAction& a = ActionOf(BindGc(sListen));
      snprintf(v.help, sizeof v.help, "Press the button or trigger that should do %s (%s has it now).", a.name,
               kButtonNames[sListen]);
      snprintf(v.value_help, sizeof v.value_help, "%s", a.help);
      snprintf(v.note, sizeof v.note, "Waiting for a press... The D-pad cancels.");
    } else {
      const SAction& a = ActionOf(BindGc(sel));
      snprintf(v.help, sizeof v.help, "%s: %s. X, then press a button or trigger: it does %s too.",
               kButtonNames[sel], a.name, a.name);
      snprintf(v.value_help, sizeof v.value_help, "%s", a.help);
      if ((sel == XHW_MENU_IN_LT || sel == XHW_MENU_IN_RT) &&
          (BindGc(sel) == PAD_TRIGGER_L || BindGc(sel) == PAD_TRIGGER_R)) {
        snprintf(v.value_help + strlen(v.value_help), sizeof v.value_help - strlen(v.value_help),
                 " The game also reads how far the trigger is in.");
      }
      if (BindGc(sel) == PAD_BUTTON_A || BindGc(sel) == PAD_BUTTON_START) {
        snprintf(v.note, sizeof v.note, "Fire and Pause always stay on some input.");
      } else if (BindGc(sel) == XSDK_PAD_BEAM && !dual) {
        snprintf(v.note, sizeof v.note, "Beam Select is for Dual Stick aiming (Controller tab).");
      } else {
        snprintf(v.note, sizeof v.note, "An action only it has is swapped, not lost.");
      }
    }
  } else {
    const STab& tab = kTabs[sTab];
    v.rows = tab.count;
    v.sel = sel;
    bool restartPending = false;
    for (int r = 0; r < tab.count && r < XHW_MENU_ROWS; ++r) {
      const SRow& row = tab.rows[r];
      snprintf(v.label[r], sizeof v.label[r], "%s", row.label);
      if (row.count == 0) {
        v.flags[r] = XHW_MENU_ROW_ACTION;
        snprintf(v.value[r], sizeof v.value[r], "%s", row.values[0]);
        continue;
      }
      const int val = Get(row.option);
      snprintf(v.value[r], sizeof v.value[r], "%s", row.values[val < 0 ? 0 : val >= row.count ? row.count - 1 : val]);
      v.choices[r] = row.count;
      v.choice[r] = val;
      if (val != DefaultValue(row.option)) {
        v.flags[r] |= XHW_MENU_ROW_CHANGED;
      }
      if (row.restart) {
        v.flags[r] |= XHW_MENU_ROW_RESTART;
        if (row.option == kXO_VideoOutput) {
          const xsdk_settings& b = g_xsdk_settings_boot;
          const int atBoot = b.video_720p ? 2 : b.progressive ? 1 : 0;
          restartPending = restartPending || val != atBoot;
        }
      }
    }
    const SRow& row = tab.rows[sel];
    snprintf(v.help, sizeof v.help, "%s", row.help);
    if (row.count > 0) {
      const int val = Get(row.option);
      snprintf(v.value_help, sizeof v.value_help, "%s: %s", row.values[val], row.valueHelp[val]);
      if (row.option == kXO_VideoOutput) {
        // what the next start will really get (xhw_video.c falls back)
        if (val == 2 && !xhw_video_720p_allowed()) {
          snprintf(v.note, sizeof v.note, "The dashboard doesn't allow 720p: the next start uses %s.",
                   xhw_video_480p_allowed() ? "480p" : "480i");
        } else if (val == 1 && !xhw_video_480p_allowed()) {
          snprintf(v.note, sizeof v.note, "The dashboard doesn't allow 480p: the next start uses 480i.");
        }
      }
    }
    if (!v.note[0] && sTab == kTabController && g_xsdk_settings.aim == XSDK_AIM_DUAL) {
      bool beam = false;
      for (int b = 0; b < XHW_MENU_IN_MAPPABLE; ++b) {
        beam = beam || BindGc(b) == XSDK_PAD_BEAM;
      }
      if (!beam) {
        snprintf(v.note, sizeof v.note, "No button has Beam Select: give one on the Buttons tab.");
      }
    }
    if (!v.note[0] && restartPending) {
      snprintf(v.note, sizeof v.note, "* Video Mode changes the next time Prime-X starts.");
    }
  }
  if (sConfirmUntil != 0) {
    snprintf(v.note, sizeof v.note, "%s",
             sTab == kTabSystem ? "Press A again to restore every setting." : "Press Y again to restore this tab's defaults.");
  }
}

// the Video Mode chosen isn't the one Prime-X started in
bool VideoRestartPending() {
  const xsdk_settings& b = g_xsdk_settings_boot;
  const int atBoot = b.video_720p ? 2 : b.progressive ? 1 : 0;
  return Get(kXO_VideoOutput) != atBoot;
}

void Close() {
  sOpen = false;
  sAsk = false;
  sListen = -1;
  sRelease = true;
  xhw_menu_show(nullptr);
  Save(); // settings.ini, once, if anything changed
  xhw_logf("[MENU] Port Options closed");
}

// "Restart now": settings.ini, then this XBE again (xhw_main.c launch: the
// card's saves flushed, the audio, LEDs and pads shut down first)
void RestartNow() {
  Save();
  xhw_logf("[MENU] restart for Video Mode %d", Get(kXO_VideoOutput));
  xhw_exit_reason("Video Mode changed in Port Options");
  xhw_reboot_self();
}

// left/right or A on the selected row
void Change(int delta) {
  const int sel = sSel[sTab];
  if (sTab == kTabButtons) {
    if (sel >= XHW_MENU_IN_MAPPABLE) {
      return; // fixed (the box says so)
    }
    Step(BindOption(sel), delta);
    xhw_logf("[MENU] %s: %s", kButtonNames[sel], ActionOf(BindGc(sel)).name);
    return;
  }
  const SRow& row = kTabs[sTab].rows[sel];
  if (row.count == 0) {
    return;
  }
  const int val = Get(row.option);
  SetValue(row.option, ((val + delta) % row.count + row.count) % row.count);
  const int now = Get(row.option);
  xhw_logf("[MENU] %s: %s", row.label, row.values[now < 0 || now >= row.count ? 0 : now]);
}

void Act() { // A on an action row
  const SRow& row = kTabs[sTab].rows[sSel[sTab]];
  if (row.option != kActRestoreAll) {
    return;
  }
  const uint32_t now = NowMs();
  if (sConfirmUntil != 0 && sConfirmTab == sTab && now < sConfirmUntil) {
    sConfirmUntil = 0;
    RestoreAll();
    xhw_logf("[MENU] every setting restored");
    return;
  }
  sConfirmUntil = now + 3000;
  sConfirmTab = sTab;
}

void Frame() {
  // every port, as they are
  uint32_t held = 0;
  int trig = 0, lx = 0, ly = 0;
  xhw_pad_poll();
  sPad1Ok = false;
  for (int i = 0; i < 4; ++i) {
    xhw_pad p;
    if (!xhw_pad_get(i, &p)) {
      continue;
    }
    if (i == 0) {
      sPad1 = p, sPad1Ok = true;
    }
    held |= p.buttons;
    // a trigger clicks past 160 and lets go below 100
    if (p.lt > 160 || ((sPrevTrig & 1) && p.lt > 100)) trig |= 1;
    if (p.rt > 160 || ((sPrevTrig & 2) && p.rt > 100)) trig |= 2;
    if (p.lx > 16000 || p.lx < -16000) lx = p.lx;
    if (p.ly > 16000 || p.ly < -16000) ly = p.ly;
  }
  const uint32_t press = held & ~sPrev;
  const int trigPress = trig & ~sPrevTrig;
  sPrev = held;
  sPrevTrig = trig;
  const uint32_t now = NowMs();
  if (sConfirmUntil != 0 && now >= sConfirmUntil) {
    sConfirmUntil = 0;
  }

  if (sRestarting != 0) { // "Restarting" on screen first, then the restart
    // the card draws a few bands a frame: wait until it is all there (at
    // most ~2 s), then one frame more
    if (xhw_menu_busy() && sRestarting < 120) {
      ++sRestarting;
      return;
    }
    RestartNow();
    return;
  }
  if (sAsk) { // restart now or later
    if (press & (XHW_BTN_B | XHW_BTN_START)) {
      Close();
      return;
    }
    // the D-pad or the left stick, any way: there are only two
    const bool stick = lx != 0 || ly != 0;
    if ((press & (XHW_BTN_UP | XHW_BTN_DOWN | XHW_BTN_LEFT | XHW_BTN_RIGHT)) || (stick && !sAskStick)) {
      sAskSel ^= 1;
    }
    sAskStick = stick;
    if (press & XHW_BTN_A) {
      if (sAskSel) {
        // drawn before the save and the relaunch, which take seconds on a
        // slow drive (the screen sat still, round hw19)
        sRestarting = 1;
        Fill();
        xhw_menu_show(&sView);
        return;
      }
      Close();
      return;
    }
    Fill();
    xhw_menu_show(&sView);
    return;
  }
  if (sListen >= 0) {
    // Bind mode: the next button or trigger pressed takes the action (B,
    // START and the triggers too, so nothing here closes the popup or
    // turns the tab); the D-pad or kListenMs without a press cancels
    static const struct {
      uint32_t bit;
      int in;
    } kPress[] = {{XHW_BTN_A, XHW_MENU_IN_A},           {XHW_BTN_B, XHW_MENU_IN_B},
                  {XHW_BTN_X, XHW_MENU_IN_X},           {XHW_BTN_Y, XHW_MENU_IN_Y},
                  {XHW_BTN_WHITE, XHW_MENU_IN_WHITE},   {XHW_BTN_BLACK, XHW_MENU_IN_BLACK},
                  {XHW_BTN_START, XHW_MENU_IN_START},   {XHW_BTN_BACK, XHW_MENU_IN_BACK},
                  {XHW_BTN_LSTICK, XHW_MENU_IN_LCLICK}, {XHW_BTN_RSTICK, XHW_MENU_IN_RCLICK},
                  {XHW_BTN_LT, XHW_MENU_IN_LT},         {XHW_BTN_RT, XHW_MENU_IN_RT}};
    ++sListenFrames;
    const uint32_t got = press | (trigPress & 1 ? XHW_BTN_LT : 0u) | (trigPress & 2 ? XHW_BTN_RT : 0u);
    int to = -1;
    for (const auto& k : kPress) {
      if (got & k.bit) {
        to = k.in;
        break;
      }
    }
    if (to >= 0) {
      const int from = sListen;
      sListen = -1;
      BindPress(from, to);
      sSel[kTabButtons] = to; // the ring on the input just bound
      xhw_logf("[MENU] bind: %s now %s, %s %s", kButtonNames[to], ActionOf(BindGc(to)).name, kButtonNames[from],
               ActionOf(BindGc(from)).name);
    } else if ((press & (XHW_BTN_UP | XHW_BTN_DOWN | XHW_BTN_LEFT | XHW_BTN_RIGHT)) ||
               (now >= sListenUntil && sListenFrames > kListenFrames)) {
      sListen = -1;
    }
    // the direction held now isn't a move once bind mode ends (the D-pad
    // press that cancelled it moved the ring too)
    sDir = ((held & XHW_BTN_UP) || ly > 0 ? 1 : 0) | ((held & XHW_BTN_DOWN) || ly < 0 ? 2 : 0) |
           ((held & XHW_BTN_LEFT) || lx < 0 ? 4 : 0) | ((held & XHW_BTN_RIGHT) || lx > 0 ? 8 : 0);
    sDirNext = static_cast< int >(now + 300);
    sDirFrames = -8;
    Fill();
    xhw_menu_show(&sView);
    return;
  }
  if (press & (XHW_BTN_B | XHW_BTN_START)) {
    if (VideoRestartPending()) {
      sAsk = true;
      sAskSel = 0; // "later" first: a stray press mustn't restart
      sAskStick = lx != 0 || ly != 0; // a stick already pushed doesn't pick
      Fill();
      xhw_menu_show(&sView);
      return;
    }
    Close();
    return;
  }
  // the held direction, repeated: 300 ms, then every 85
  int dir = 0;
  if ((held & XHW_BTN_UP) || ly > 0) dir |= 1;
  if ((held & XHW_BTN_DOWN) || ly < 0) dir |= 2;
  if ((held & XHW_BTN_LEFT) || lx < 0) dir |= 4;
  if ((held & XHW_BTN_RIGHT) || lx > 0) dir |= 8;
  // a held direction repeats after 300 ms and 9 frames, then every 85 ms
  // and 2 frames: by time alone, a slow frame (xemu runs the title at ~6
  // fps) turned a tap into several moves
  int fire = 0;
  ++sDirFrames;
  if (dir != sDir) {
    fire = dir & ~sDir;
    sDir = dir;
    sDirNext = static_cast< int >(now + 300);
    sDirFrames = -8;
  } else if (dir && static_cast< int >(now - static_cast< uint32_t >(sDirNext)) >= 0 && sDirFrames > 0) {
    fire = dir;
    sDirNext = static_cast< int >(now + 85);
    sDirFrames = -1;
  }
  const int rows = RowCount(sTab);
  if (sTab == kTabButtons) { // over the callouts as they stand
    const int d = fire & 1 ? 1 : fire & 2 ? 2 : fire & 4 ? 4 : fire & 8 ? 8 : 0;
    if (d != 0) {
      sSel[sTab] = PadMove(sSel[sTab], d);
      sConfirmUntil = 0;
    }
    // X: bind mode for the selected input (the user, 2026-11-03: "press X to
    // start key bind, then you can press right trigger"); A steps the action
    if ((press & XHW_BTN_X) && sSel[sTab] < XHW_MENU_IN_MAPPABLE) {
      sListen = sSel[sTab];
      sListenUntil = now + kListenMs;
      sListenFrames = 0;
      sConfirmUntil = 0;
      Fill();
      xhw_menu_show(&sView);
      return;
    }
  } else if (fire & 1) {
    sSel[sTab] = (sSel[sTab] + rows - 1) % rows;
    sConfirmUntil = 0;
  } else if (fire & 2) {
    sSel[sTab] = (sSel[sTab] + 1) % rows;
    sConfirmUntil = 0;
  } else if (fire & 4) {
    Change(-1);
  } else if (fire & 8) {
    Change(1);
  }
  if (trigPress & 3) {
    sTab = (sTab + (trigPress & 1 ? kTabCount - 1 : 1)) % kTabCount;
    sConfirmUntil = 0;
  }
  if (press & XHW_BTN_A) {
    if (sTab != kTabButtons && kTabs[sTab].rows[sSel[sTab]].count == 0) {
      Act();
    } else {
      Change(1);
    }
  }
  if (press & XHW_BTN_Y) {
    if (sConfirmUntil != 0 && sConfirmTab == sTab && sTab != kTabSystem) {
      sConfirmUntil = 0;
      RestoreTab(sTab);
      xhw_logf("[MENU] %s tab restored", kTabNames[sTab]);
    } else if (sTab != kTabSystem) {
      sConfirmUntil = now + 3000;
      sConfirmTab = sTab;
    }
  }
  Fill();
  xhw_menu_show(&sView);
}

// ---- the game's lettering for the card (xhw.h xhw_menu_font) ----
// The disc's Deface fonts through the game's own loader (the pause screen's
// text and its large titles), their cells decoded once from the font
// texture (the GameCube's tiled formats, PortFontAccent::Decode) into plain
// texel values the card draws with: a few KB, kept for the run. The tokens
// are let go at once. A font that can't be had leaves the card's unscii.
const uint32_t kFontIds[2] = {0xC29C51F1, 0xFE94BD23}; // FONT_Deface14B, FONT_DefaceLarge
xhw_menu_font sFont[2];
std::vector< uint8_t > sFontCells[2];
int sFontState[2]; // 0 not tried, 1 set, -1 failed

bool LiftFont(int which) {
  if (gpSimplePool == nullptr) {
    return false;
  }
  TLockedToken< CRasterFont > font(gpSimplePool->GetObj(SObjectTag('FONT', kFontIds[which])));
  if (*font == nullptr) {
    return false;
  }
  TLockedToken< CTexture > tex(font->GetTexture());
  if (*tex == nullptr) {
    return false;
  }
  const ETexelFormat tf = tex->GetTexelFormat();
  if (tf != kTF_I4 && tf != kTF_I8 && tf != kTF_C4 && tf != kTF_C8) {
    xhw_logf("[MENU] font %08x: texel format %d, not used", (unsigned)kFontIds[which], (int)tf);
    return false;
  }
  const PortFontAccent::Format format = static_cast< PortFontAccent::Format >(tf);
  const int texW = tex->GetWidth(), texH = tex->GetHeight();
  const uint8_t* texels = static_cast< const uint8_t* >(tex->GetConstBitMapData(0));
  const size_t texSize = tex->GetMemoryAllocated();
  if (texels == nullptr) {
    return false;
  }
  xhw_menu_font& f = sFont[which];
  std::vector< uint8_t >& cells = sFontCells[which];
  memset(&f, 0, sizeof f);
  cells.clear();
  int inked = 0;
  for (int ch = ' '; ch <= '~'; ++ch) {
    const CGlyph* g = font->GetGlyph(static_cast< wchar_t >(ch));
    xhw_menu_glyph& out = f.glyph[ch - ' '];
    if (g == nullptr) {
      continue;
    }
    out.a = g->GetA();
    out.b = g->GetB();
    out.c = g->GetC();
    out.top = g->GetBaseLine();
    const int x0 = static_cast< int >(g->GetStartU() * texW + 0.5f);
    const int y0 = static_cast< int >(g->GetStartV() * texH + 0.5f);
    const int w = g->GetCellWidth(), h = g->GetCellHeight();
    if (w <= 0 || h <= 0 || w > 64 || h > 64 || x0 < 0 || y0 < 0 || x0 + w > texW || y0 + h > texH) {
      continue;
    }
    out.w = static_cast< int16_t >(w);
    out.h = static_cast< int16_t >(h);
    out.off = static_cast< uint32_t >(cells.size());
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        uint8_t v = 0;
        PortFontAccent::Decode(texels, texSize, format, texW, texH, x0 + x, y0 + y, v);
        v &= 3; // bit 0 the ink, bit 1 the outline (value 3: neither, as the renderer's palette)
        if (v == 3) {
          v = 0;
        }
        inked += v & 1;
        cells.push_back(v);
      }
    }
  }
  if (inked == 0) {
    xhw_logf("[MENU] font %08x: no ink decoded (format %d), not used", (unsigned)kFontIds[which], (int)tf);
    return false;
  }
  f.ascent = static_cast< int16_t >(font->GetBaseLine());
  f.height = static_cast< int16_t >(font->GetMonoHeight());
  f.cells = cells.data();
  xhw_logf("[MENU] font %08x: %dx%d texture (format %d), line %d, baseline %d, %u KB of cells",
           (unsigned)kFontIds[which], texW, texH, (int)tf, f.height, f.ascent,
           (unsigned)(cells.size() + 1023) / 1024);
  return true;
}

void EnsureFonts() {
  for (int i = 0; i < 2; ++i) {
    if (sFontState[i] == 0) {
      sFontState[i] = LiftFont(i) ? 1 : -1;
      xhw_menu_set_font(i, sFontState[i] > 0 ? &sFont[i] : nullptr);
    }
  }
}

// The game held until the popup closes: 60 times a second the pads are
// read, the view filled and what changed drawn onto the frame on screen;
// the disc's and ARAM's completions (the music's stream reads) and the
// alarms go on as while the game waits for a disc read. The game's next
// present, after this returns, replaces the card.
void RunModal() {
  xhw_menu_modal_begin();
  while (sOpen) {
    xsdk_modal_tick();
    Frame();
    if (sOpen) {
      xhw_menu_modal_frame();
    }
  }
  xhw_menu_modal_end();
}

} // namespace

void OpenMenu() {
  if (sOpen) {
    return;
  }
  xsdk_settings_defaults(&sDefaults);
  sOpen = true;
  sRelease = false;
  sConfirmUntil = 0;
  sListen = -1;
  sDir = 0;
  // what is held now (the A that chose the row) isn't a press
  sPrev = 0;
  sPrevTrig = 3;
  xhw_pad_poll();
  for (int i = 0; i < 4; ++i) {
    xhw_pad p;
    if (xhw_pad_get(i, &p)) {
      sPrev |= p.buttons;
    }
  }
  sDir = 15; // a held direction waits to be let go
  sAsk = false;
  sRestarting = 0;
  EnsureFonts();
  Fill();
  xhw_menu_show(&sView);
  xhw_logf("[MENU] Port Options opened");
  RunModal();
}

bool MenuOpen() { return sOpen; }

} // namespace PortXboxOptions

// vi.c, once a frame (the game thread): test builds' PX_MENU_AT. The popup
// itself runs in OpenMenu's modal loop.
extern "C" void px_menu_frame(void) {
#if XHW_TEST_BUILD
  // test builds: "env PX_MENU_AT=<frame>" in autopad.txt opens the popup at
  // that frame, wherever the game is (xemu is too slow for a script to walk
  // the main menu reliably)
  // (PX_MENU_AGAIN=<frame>: a second open, the glyph cache made again)
  static int sAt = -2, sAgain = -1;
  static unsigned sFrame;
  ++sFrame;
  if (sAt == -2) {
    const char* e = getenv("PX_MENU_AT");
    sAt = e != nullptr ? atoi(e) : -1;
    e = getenv("PX_MENU_AGAIN");
    sAgain = e != nullptr ? atoi(e) : -1;
  }
  if ((sAt > 0 && sFrame == static_cast< unsigned >(sAt)) ||
      (sAgain > 0 && sFrame == static_cast< unsigned >(sAgain))) {
    PortXboxOptions::OpenMenu();
  }
#endif
  if (PortXboxOptions::sOpen) {
    PortXboxOptions::Frame();
  }
}

// pad.c: the game's PADRead gets a neutral pad while the popup is up, and
// after it closes until every button is let go (the B that closed it would
// leave the game's own menu too)
extern "C" int xsdk_menu_block(uint32_t raw_buttons) {
  using namespace PortXboxOptions;
  if (sOpen) {
    return 1;
  }
  if (sRelease) {
    if (raw_buttons == 0) {
      sRelease = false;
      return 0;
    }
    return 1;
  }
  return 0;
}

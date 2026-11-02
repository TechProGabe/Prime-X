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
// Y twice restores the tab's defaults, B or START close. It makes no
// sound (the user, 2026-11-01, when the music skipped on the consoles; the
// skips were the card's redraw, xhw_menu.c). The game reads a neutral pad while the popup is up and
// until the buttons that closed it are let go (xsdk_menu_block, pad.c).
#include "port_xbox_options.h"
#include "port_debug.h"

#include "MetroidPrime/Player/CGameState.hpp"

#include <dolphin/pad.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xhw.h"
extern "C" {
#include "xsdk_settings.h"
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
     "The same for the right stick (the GameCube's C-stick: beams). Raise it if beams change on their own.",
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
     {"Samus's own Power Suit.", "Samus wears the Fusion Suit. Kept with the game's options in your save."},
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
    "A button", "B button", "X button", "Y button", "White button", "Black button",
    "START",    "BACK",     "Left stick click", "Right stick click"};
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
};
const SAction& ActionOf(int gc) {
  for (const SAction& a : kActionHelp) {
    if (a.gc == gc) {
      return a;
    }
  }
  return kActionHelp[0];
}

// ---- state ----
bool sOpen = false;
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
xhw_pad sPad1; // port 1 this frame (the Buttons tab's live controller)
bool sPad1Ok;
xhw_menu_view sView;

uint32_t NowMs() { return static_cast< uint32_t >(xhw_time_ns() / 1000000ull); }


int RowCount(int tab) { return tab == kTabButtons ? XHW_MENU_IN_MAPPABLE : kTabs[tab].count; }

// what a row would show with the defaults: Get() reads g_xsdk_settings
int DefaultValue(O option) {
  if (option == kXO_FusionSuit) {
    return 0;
  }
  const xsdk_settings now = g_xsdk_settings;
  g_xsdk_settings = sDefaults;
  const int v = Get(option);
  g_xsdk_settings = now;
  return v;
}

void SetValue(O option, int value) { Set(option, value); }

void RestoreTab(int tab) {
  if (tab == kTabButtons) {
    for (int b = 0; b < XHW_MENU_IN_MAPPABLE; ++b) {
      // Step swaps; set each port's binding straight back instead
      for (int p = 0; p < 4; ++p) {
        g_xsdk_settings.port[p].bind[b].gc = sDefaults.port[p].bind[b].gc;
      }
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
  snprintf(out, cap, "Now: %s %s", mode, PortDebug::AspectMode() == PortDebug::kAspect_16_9 ? "16:9" : "4:3");
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
  const int sel = sSel[sTab];
  v.pad_sel = -1;
  if (sTab == kTabButtons) {
    v.page = 1;
    v.pad_sel = sel;
    for (int b = 0; b < XHW_MENU_IN_MAPPABLE; ++b) {
      const int gc = BindGc(b);
      snprintf(v.pad_map[b], sizeof v.pad_map[b], "%s", ActionOf(gc).name);
      v.pad_changed[b] = gc != sDefaults.port[0].bind[b].gc;
    }
    // the inputs that keep their GameCube part (pad.c)
    const bool swap = gpGameState != nullptr && gpGameState->GameOptions().GetSwapBeamControls();
    snprintf(v.pad_map[XHW_MENU_IN_LT], sizeof v.pad_map[0], "Lock On");
    snprintf(v.pad_map[XHW_MENU_IN_RT], sizeof v.pad_map[0], "Free Aim");
    snprintf(v.pad_map[XHW_MENU_IN_DPAD], sizeof v.pad_map[0], "%s", swap ? "Beams" : "Visors");
    snprintf(v.pad_map[XHW_MENU_IN_LSTICK], sizeof v.pad_map[0], "Move");
    snprintf(v.pad_map[XHW_MENU_IN_RSTICK], sizeof v.pad_map[0], "%s", swap ? "Visors" : "Beams");
    if (sPad1Ok) { // port 1 as it is: what is held lights up
      v.pad_ok = 1;
      v.pad_buttons = sPad1.buttons;
      // tilted or not, pulled or not: a stick's jitter mustn't redraw the
      // card every frame (it is drawn again whenever the view changes)
      const long long lx = sPad1.lx, ly = sPad1.ly, rx = sPad1.rx, ry = sPad1.ry;
      const bool lTilt = lx * lx + ly * ly > 12000LL * 12000;
      const bool rTilt = rx * rx + ry * ry > 12000LL * 12000;
      v.pad_lx = lTilt ? 32767 : 0;
      v.pad_rx = rTilt ? 32767 : 0;
      v.pad_lt = sPad1.lt > 128 ? 255 : 0;
      v.pad_rt = sPad1.rt > 128 ? 255 : 0;
    }
    const SAction& a = ActionOf(BindGc(sel));
    snprintf(v.help, sizeof v.help, "%s: %s. Left/right pick its action (a taken one swaps).",
             kButtonNames[sel], a.name);
    snprintf(v.value_help, sizeof v.value_help, "%s", a.help);
    if (BindGc(sel) == PAD_BUTTON_A || BindGc(sel) == PAD_BUTTON_START) {
      snprintf(v.note, sizeof v.note, "Fire and Pause always stay on some button.");
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
    if (!v.note[0] && restartPending) {
      snprintf(v.note, sizeof v.note, "* Video Mode changes the next time Prime-X starts.");
    }
  }
  if (sConfirmUntil != 0) {
    snprintf(v.note, sizeof v.note, "%s",
             sTab == kTabSystem ? "Press A again to restore every setting." : "Press Y again to restore this tab's defaults.");
  }
}

void Close() {
  sOpen = false;
  sRelease = true;
  xhw_menu_show(nullptr);
  Save(); // settings.ini, once, if anything changed
  xhw_logf("[MENU] Port Options closed");
}

// left/right or A on the selected row
void Change(int delta) {
  const int sel = sSel[sTab];
  if (sTab == kTabButtons) {
    Step(kXO_BindFirst + sel, delta);
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

  if (press & (XHW_BTN_B | XHW_BTN_START)) {
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
  if (fire & 1) {
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

} // namespace

void OpenMenu() {
  if (sOpen) {
    return;
  }
  xsdk_settings_defaults(&sDefaults);
  sOpen = true;
  sRelease = false;
  sConfirmUntil = 0;
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
  Fill();
  xhw_menu_show(&sView);
  xhw_logf("[MENU] Port Options opened");
}

bool MenuOpen() { return sOpen; }

} // namespace PortXboxOptions

// vi.c, once a frame (the game thread)
extern "C" void px_menu_frame(void) {
#if XHW_TEST_BUILD
  // test builds: "env PX_MENU_AT=<frame>" in autopad.txt opens the popup at
  // that frame, wherever the game is (xemu is too slow for a script to walk
  // the main menu reliably)
  static int sAt = -2;
  static unsigned sFrame;
  ++sFrame;
  if (sAt == -2) {
    const char* e = getenv("PX_MENU_AT");
    sAt = e != nullptr ? atoi(e) : -1;
  }
  if (sAt > 0 && sFrame == static_cast< unsigned >(sAt)) {
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

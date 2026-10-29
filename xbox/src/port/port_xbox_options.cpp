// port_xbox_options.cpp - what Prime-X's Options rows do (port_xbox_options.h,
// docs/ui.md). Each row reads and writes g_xsdk_settings (settings.ini,
// xbox/src/sdk/settings.c) and, where the game has a hook for it, PortDebug.
// Changes apply at once except the video mode, which the next start takes
// (the mode is set before the game runs); settings.ini is written when the
// Options screen closes, once, so moving a slider never touches the disk.
#include "port_xbox_options.h"
#include "port_debug.h"

#include "MetroidPrime/Player/CGameState.hpp"
#include "MetroidPrime/Player/CPlayerState.hpp"
#include "MetroidPrime/Player/CSystemState.hpp"

#include <dolphin/pad.h>

#include <stdlib.h>

#include "xgx.h"
#include "xhw.h"
extern "C" {
#include "xsdk_settings.h"
int xsdk_disc_passed_before(void);   // dvd.c
void xsdk_disc_forget(void);
}

namespace PortXboxOptions {
namespace {
bool sDirty = false;

// The actions a button can do: a GameCube button, named by what it does in
// Prime with the default controls (Swap Beam Controls aside), short enough
// for the value widget's cells
struct SAction {
  const wchar_t* name;
  uint16_t gc;
};
const SAction kActions[] = {
    {L"None", 0},
    {L"Fire", PAD_BUTTON_A},
    {L"Jump", PAD_BUTTON_B},
    {L"Morph", PAD_BUTTON_X},
    {L"Missile", PAD_BUTTON_Y},
    {L"Map", PAD_TRIGGER_Z},
    {L"Lock", PAD_TRIGGER_L},
    {L"Aim", PAD_TRIGGER_R},
    {L"Pause", PAD_BUTTON_START},
    {L"Up", PAD_BUTTON_UP}, // the D-pad: the visors
    {L"Down", PAD_BUTTON_DOWN},
    {L"Left", PAD_BUTTON_LEFT},
    {L"Right", PAD_BUTTON_RIGHT},
};
const int kActionCount = static_cast< int >(sizeof kActions / sizeof kActions[0]);

// the Buttons rows, in settings.c's binding order (k_xbox there)
const wchar_t* const kButtonNames[] = {L"A Button",     L"B Button",     L"X Button",
                                       L"Y Button",     L"White Button", L"Black Button",
                                       L"Start Button", L"Back Button",  L"Left Stick Click",
                                       L"Right Stick Click"};
// GameCube by position (settings.c's k_default)
const uint16_t kDefaultBinds[] = {PAD_BUTTON_A, PAD_BUTTON_X,     PAD_BUTTON_B, PAD_BUTTON_Y, PAD_TRIGGER_Z,
                                  PAD_TRIGGER_Z, PAD_BUTTON_START, 0,            0,            0};

bool IsBind(int option) { return option >= kXO_BindFirst && option <= kXO_BindLast; }

int ActionIndex(uint16_t gc) {
  for (int i = 0; i < kActionCount; ++i) {
    if (kActions[i].gc == gc) {
      return i;
    }
  }
  return 0;
}
int sDiscPending = -1; // Check Disc: -1 not looked up yet

int Percent(float f) { return static_cast< int >(f * 100.f + 0.5f); }

void ApplyFov() { PortDebug::SetFirstPersonFov(static_cast< float >(g_xsdk_settings.fov)); }

// the game's aspect from the mode and the Widescreen row: 720p is always
// 16:9; at 480 Auto follows the dashboard. The output signal doesn't change
// (an anamorphic 16:9 TV stretches 480 lines either way), only the picture.
void ApplyAspect() {
  const xhw_video_mode* vm = xhw_video();
  int wide;
  if (vm->height >= 720) {
    wide = 1;
  } else if (g_xsdk_settings.widescreen == 2) {
    wide = 1;
  } else if (g_xsdk_settings.widescreen == 0) {
    wide = 0;
  } else {
    wide = xhw_video_widescreen_set();
  }
  PortDebug::SetAspectMode(wide ? PortDebug::kAspect_16_9 : PortDebug::kAspect_4_3);
}
} // namespace

const wchar_t* Title(int option) {
  switch (option) {
  case kXO_WidescreenHud:
    return L"Widescreen HUD";
  case kXO_Fov:
    return L"Field of View";
  case kXO_FpsCounter:
    return L"Frame Rate"; // "FPS Counter" ran past its button on the pause screen
  case kXO_VideoOutput:
    return L"Video Mode";
  case kXO_Widescreen:
    return L"Widescreen";
  case kXO_VerifyDisc:
    return L"Check Disc";
  case kXO_RumbleStrength:
    return L"Rumble Strength";
  case kXO_StickDeadZone:
    return L"Stick Dead Zone";
  case kXO_CStickDeadZone:
    return L"C-Stick Dead Zone";
  case kXO_TriggerClick:
    return L"Trigger Click";
  case kXO_Screenshots:
    return L"Screen Capture";
  case kXO_FusionSuit:
    return L"Fusion Suit";
  case kXO_SkipCutscenes:
    return L"Skip Cutscenes";
  case kXO_GameTiming:
    return L"Game Timing";
  default:
    return IsBind(option) ? kButtonNames[option - kXO_BindFirst] : L"";
  }
}

const wchar_t* Description(int option) {
  if (IsBind(option)) {
    return L"What this button does. Left and right change it.";
  }
  switch (option) {
  case kXO_Widescreen:
    return L"Auto follows the dashboard. 720p is always 16:9.";
  case kXO_WidescreenHud:
    return L"Inner keeps the HUD in the middle; Edges spreads it to the sides of a 16:9 screen.";
  case kXO_Fov:
    return L"How wide Samus sees. 55 is the GameCube's.";
  case kXO_FpsCounter:
    return L"Frames per second in the corner of the screen.";
  case kXO_VideoOutput:
    return L"Used from the next start. Hold BACK at start for 480i.";
  case kXO_RumbleStrength:
    return L"How strongly the controller shakes.";
  case kXO_StickDeadZone:
    return L"Raise it if Samus moves with the stick untouched.";
  case kXO_CStickDeadZone:
    return L"Raise it if the C-stick acts on its own.";
  case kXO_TriggerClick:
    return L"How far L and R press in before they click.";
  case kXO_Screenshots:
    return L"BACK saves a screenshot to the save folder.";
  case kXO_SkipCutscenes:
    return L"START skips any cutscene, from the next room.";
  case kXO_GameTiming:
    return L"Flex updates the game once a frame, for more frames a second. Fixed: 60 a second.";
  case kXO_FusionSuit:
    return L"The Fusion Suit, a GBA-link bonus on the GameCube.";
  default:
    return nullptr;
  }
}

bool IsList(int option) { return IsBind(option); }

const wchar_t* ListLabel(int option, int offset) {
  if (!IsBind(option)) {
    return L"";
  }
  const int i = ActionIndex(g_xsdk_settings.port[0].bind[option - kXO_BindFirst].gc);
  return kActions[((i + offset) % kActionCount + kActionCount) % kActionCount].name;
}

void Step(int option, int delta) {
  if (!IsBind(option)) {
    return;
  }
  const int b = option - kXO_BindFirst;
  const int i = ActionIndex(g_xsdk_settings.port[0].bind[b].gc);
  const uint16_t gc = kActions[((i + delta) % kActionCount + kActionCount) % kActionCount].gc;
  // every port the same: Prime is one player
  for (int p = 0; p < 4; ++p) {
    g_xsdk_settings.port[p].bind[b].gc = gc;
  }
  sDirty = true;
}

// The rows that were sliders are three choices: the title screen's frame
// draws no slider, and three named steps are plainer anyway (the user,
// round hw4: "how do I change it? there's no indicator"). settings.ini
// keeps the number; a row shows the nearest step.
struct SPreset {
  int option;
  int values[3];
  const wchar_t* labels[3];
};
const SPreset kPresets[] = {
    {kXO_Fov, {55, 65, 75}, {L"55", L"65", L"75"}},
    {kXO_RumbleStrength, {40, 70, 100}, {L"Low", L"Medium", L"Full"}},
    {kXO_StickDeadZone, {10, 20, 30}, {L"Low", L"Medium", L"High"}},
    {kXO_CStickDeadZone, {15, 25, 35}, {L"Low", L"Medium", L"High"}},
    {kXO_TriggerClick, {180, 230, 250}, {L"Light", L"Medium", L"Firm"}},
};

const SPreset* PresetOf(int option) {
  for (const SPreset& p : kPresets) {
    if (p.option == option) {
      return &p;
    }
  }
  return nullptr;
}

// the step nearest to a stored value
int Nearest(const SPreset& p, int value) {
  int best = 0;
  for (int i = 1; i < 3; ++i) {
    const int d = value - p.values[i], db = value - p.values[best];
    if ((d < 0 ? -d : d) < (db < 0 ? -db : db)) {
      best = i;
    }
  }
  return best;
}

const wchar_t* ValueLabel(int option, int cell) {
  static const wchar_t* const kVideo[] = {L"480i", L"480p", L"720p"};
  static const wchar_t* const kWide[] = {L"Auto", L"4:3", L"16:9"};
  if (cell < 0 || cell >= 3) {
    return nullptr;
  }
  if (const SPreset* p = PresetOf(option)) {
    return p->labels[cell];
  }
  // two-choice rows whose Off / On wouldn't say what they do (the user,
  // round hw4: "what does on or off mean")
  static const wchar_t* const kFps[] = {L"Hide", L"Show"};
  static const wchar_t* const kHud[] = {L"Inner", L"Edges"};
  static const wchar_t* const kDisc[] = {L"No", L"Yes"};
  static const wchar_t* const kTiming[] = {L"Fixed", L"Flex"};   // short: the title's cells fit "Off"
  if (cell < 2) {
    if (option == kXO_FpsCounter) {
      return kFps[cell];
    }
    if (option == kXO_WidescreenHud) {
      return kHud[cell];
    }
    if (option == kXO_VerifyDisc) {
      return kDisc[cell];
    }
    if (option == kXO_GameTiming) {
      return kTiming[cell];
    }
  }
  if (option == kXO_VideoOutput) {
    return kVideo[cell];
  }
  if (option == kXO_Widescreen) {
    return kWide[cell];
  }
  return nullptr;
}

int Get(int option) {
  const xsdk_settings& st = g_xsdk_settings;
  switch (option) {
  case kXO_WidescreenHud:
    return st.hud_wide ? 1 : 0;
  case kXO_Fov:
    return Nearest(*PresetOf(option), st.fov);
  case kXO_FpsCounter:
    return st.fps ? 1 : 0;
  case kXO_VideoOutput:
    return st.video_720p ? 2 : st.progressive ? 1 : 0;
  case kXO_Widescreen:
    // cells Auto, 4:3, 16:9; settings.ini 1, 0, 2
    return st.widescreen == 1 ? 0 : st.widescreen == 0 ? 1 : 2;
  case kXO_VerifyDisc:
    // looked up once (a file read), then what this row did
    if (sDiscPending < 0) {
      sDiscPending = xsdk_disc_passed_before() ? 0 : 1;
    }
    return sDiscPending;
  case kXO_RumbleStrength:
    return Nearest(*PresetOf(option), Percent(st.rumble));
  case kXO_StickDeadZone:
    return Nearest(*PresetOf(option), Percent(st.port[0].stick_deadzone));
  case kXO_CStickDeadZone:
    return Nearest(*PresetOf(option), Percent(st.port[0].cstick_deadzone));
  case kXO_TriggerClick:
    return Nearest(*PresetOf(option), st.port[0].trigger_click);
  case kXO_Screenshots:
    return st.shots ? 1 : 0;
  case kXO_FusionSuit:
    return gpGameState != nullptr && gpGameState->SystemState().GetHasFusion() ? 1 : 0;
  case kXO_SkipCutscenes:
    return st.skip_cutscenes ? 1 : 0;
  case kXO_GameTiming:
    return st.sim_adaptive ? 1 : 0;
  default:
    return 0;
  }
}

void Set(int option, int value) {
  xsdk_settings& st = g_xsdk_settings;
  const xsdk_settings before = st;
  int p;
  if (const SPreset* pr = PresetOf(option)) { // a step: its number
    value = pr->values[value < 0 ? 0 : value > 2 ? 2 : value];
  }
  switch (option) {
  case kXO_WidescreenHud:
    st.hud_wide = value > 0;
    PortDebug::SetHudWide(st.hud_wide != 0);
    break;
  case kXO_Fov:
    st.fov = value < 45 ? 45 : value > 75 ? 75 : value;
    ApplyFov();
    break;
  case kXO_FpsCounter:
    st.fps = value > 0;
    xgx_set_fps_overlay(st.fps);
    break;
  case kXO_VideoOutput:
    // 720p needs the dashboard's permission at the next start (xhw_video.c);
    // without it the start falls back to 480p, then 480i
    st.video_720p = value >= 2;
    st.progressive = value >= 1;
    break;
  case kXO_Widescreen:
    st.widescreen = value <= 0 ? 1 : value == 1 ? 0 : 2;
    ApplyAspect();
    break;
  case kXO_VerifyDisc:
    // On: disc.ini is forgotten, so the next start checks every file
    // (with the boot screen's music). Off can't take that back.
    if (value > 0 && sDiscPending != 1) {
      xsdk_disc_forget();
      sDiscPending = 1;
    }
    break;
  case kXO_RumbleStrength:
    st.rumble = (value < 0 ? 0 : value > 100 ? 100 : value) / 100.f;
    break;
  case kXO_StickDeadZone:
    for (p = 0; p < 4; ++p) {
      st.port[p].stick_deadzone = (value < 0 ? 0 : value > 60 ? 60 : value) / 100.f;
    }
    break;
  case kXO_CStickDeadZone:
    for (p = 0; p < 4; ++p) {
      st.port[p].cstick_deadzone = (value < 0 ? 0 : value > 60 ? 60 : value) / 100.f;
    }
    break;
  case kXO_TriggerClick:
    for (p = 0; p < 4; ++p) {
      st.port[p].trigger_click = static_cast< uint8_t >(value < 1 ? 1 : value > 255 ? 255 : value);
    }
    break;
  case kXO_Screenshots:
    st.shots = value > 0;
    xhw_pad_set_shots(st.shots);
    break;
  case kXO_SkipCutscenes:
    st.skip_cutscenes = value > 0;
    PortDebug::SetSkippableCutscenes(st.skip_cutscenes != 0);
    break;
  case kXO_GameTiming:
    st.sim_adaptive = value > 0;   // PortDebug::SimAdaptive reads it every frame
    break;
  case kXO_FusionSuit:
    // What the Fusion bonuses screen does once a GBA has linked: the
    // system state (saved with the game's options) and the game about to
    // start. Kept in the game's save, as on the GameCube, not settings.ini.
    if (gpGameState != nullptr) {
      gpGameState->SystemState().SetHasFusion(value > 0);
      if (gpGameState->PlayerState()) {
        gpGameState->PlayerState()->SetIsFusionEnabled(value > 0);
      }
    }
    break;
  default:
    break;
  }
  if (!xsdk_settings_equal(&before, &st)) {
    sDirty = true;
  }
}

void RestoreDefaults(int category) {
  switch (category) {
  case 0:
    Set(kXO_SkipCutscenes, 0);
    break;
  case 1:
    Set(kXO_Widescreen, 0);
    Set(kXO_WidescreenHud, 0);
    Set(kXO_Fov, 0);
    Set(kXO_FpsCounter, 0);
    Set(kXO_GameTiming, 0);
    break;
  case 3:
    for (int b = 0; b <= kXO_BindLast - kXO_BindFirst; ++b) {
      for (int p = 0; p < 4; ++p) {
        if (g_xsdk_settings.port[p].bind[b].gc != kDefaultBinds[b]) {
          g_xsdk_settings.port[p].bind[b].gc = kDefaultBinds[b];
          sDirty = true;
        }
      }
    }
    Set(kXO_RumbleStrength, 2);
    Set(kXO_StickDeadZone, 1);
    Set(kXO_CStickDeadZone, 1);
    Set(kXO_TriggerClick, 1);
    Set(kXO_Screenshots, XHW_TEST_BUILD);
    break;
  default:
    break;
  }
}

void ApplyAtBoot() {
#if XHW_TEST_BUILD
  // test builds: "env PX_WIDESCREEN=0|1|2" (4:3, as the dashboard, 16:9) over settings.ini
  if (const char* w = getenv("PX_WIDESCREEN")) {
    g_xsdk_settings.widescreen = atoi(w) == 1 ? 1 : atoi(w) == 2 ? 2 : 0;
  }
#endif
  ApplyAspect();
  PortDebug::SetHudWide(g_xsdk_settings.hud_wide != 0);
  PortDebug::SetSkippableCutscenes(g_xsdk_settings.skip_cutscenes != 0);
#if XHW_TEST_BUILD
  // test builds: "env PX_SKIPPABLE=1" in autopad.txt, over settings.ini
  if (getenv("PX_SKIPPABLE")) {
    PortDebug::SetSkippableCutscenes(true);
  }
#endif
  ApplyFov();
  xhw_logf("[OPTIONS] at boot: aspect %s, HUD %s, FOV %d", PortDebug::AspectMode() == PortDebug::kAspect_16_9 ? "16:9" : "4:3",
           g_xsdk_settings.hud_wide ? "edge to edge" : "centred", g_xsdk_settings.fov);
}

namespace {
bool sFrontEndPort = false;
} // namespace

void SetFrontEndPort(bool port) { sFrontEndPort = port; }
bool FrontEndPort() { return sFrontEndPort; }

const wchar_t* FrontEndCategory(int category) {
  static const wchar_t* const kNames[] = {L"Video", L"Controller", L"Gameplay", L"System"};
  return category >= 0 && category < 4 ? kNames[category] : nullptr;
}

void Save() {
  if (!sDirty) {
    return;
  }
  sDirty = false;
  xsdk_settings_save();
}

} // namespace PortXboxOptions

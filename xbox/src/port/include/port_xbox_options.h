#pragma once
// port_xbox_options.h - Prime-X's rows in the game's own Options screen
// (docs/ui.md "Settings, inside the game's Options"). CPauseScreenFrame.cpp
// lists them after each category's retail rows (TARGET_XBOX, in place of
// MetroidPrimePort's PC rows); everything they do is here
// (xbox/src/port/port_xbox_options.cpp). Values live in settings.ini, not in
// the game's save, so they work before a save file exists.

namespace PortXboxOptions {

// Above kGO_RestoreDefaults, as MetroidPrimePort's rows: CGameOptions sends
// anything past it here.
enum EOption {
  kXO_First = 64,
  kXO_WidescreenHud = kXO_First, // Centred / Edge to edge (DoubleEnum)
  kXO_Fov,                       // degrees (slider)
  kXO_FpsCounter,                // Off / On
  kXO_VideoOutput,               // 480i / 480p / 720p (TripleEnum), at the next start
  kXO_Widescreen,                // Auto / 4:3 / 16:9 (TripleEnum); 720p is always 16:9
  kXO_VerifyDisc,                // Off / On: the full disc check at the next start
  kXO_RumbleStrength,            // percent (slider)
  kXO_StickDeadZone,             // percent of full tilt (slider)
  kXO_CStickDeadZone,            // percent (slider)
  kXO_TriggerClick,              // 0-255, where L/R click (slider)
  kXO_Screenshots,               // Off / On (Back saves shotNN.bmp)
  kXO_FusionSuit,                // Off / On: the GBA-link bonus, unlocked here (title screen)
  kXO_SkipCutscenes,             // Off / On: every cutscene skippable (from the next room)
  kXO_GameTiming,                // removed 2026-10-30: Game Timing is always Auto (port_debug.cpp)
  kXO_LogFile,                   // Off / On: boot.log in the save folder (release builds; test builds always)
  // Buttons: what each Xbox button does, a carousel of the game's actions
  // (IsList): A, B, X, Y, White, Black, Start, Back, the stick clicks
  kXO_BindFirst,
  kXO_BindLast = kXO_BindFirst + 9,
  // the pause screen's "Port Options" row: A opens the popup (port_menu.cpp)
  kXO_OpenMenu,
};

const wchar_t* Title(int option);
// a line under the value, saying what the row does (the pause screen's body
// pane, which the retail rows leave empty); nullptr: none
const wchar_t* Description(int option);
// a TripleEnum row's three cell labels; nullptr: the retail labels
const wchar_t* ValueLabel(int option, int cell);
// A list row: shown in the three-cell widget as previous | current | next,
// the middle one selected; moving left or right steps the value (Step) and
// the row is drawn again, centred
bool IsList(int option);
const wchar_t* ListLabel(int option, int offset); // offset -1, 0, +1 from the value
void Step(int option, int delta);
int Get(int option);
void Set(int option, int value);
// the screen closes: settings.ini is written if anything changed
void Save();
// at boot, after the video mode: the game-side settings (aspect, HUD, FOV)
void ApplyAtBoot();

// The Port Options popup (xbox/src/port/port_menu.cpp, docs/ui.md): a card
// over the game, opened from the main menu's "Port Options" row, the New
// Game popup's third choice and the pause screen's Options. The game sees
// no input while it is up, nor the press that closes it.
void OpenMenu();
bool MenuOpen();
// the GameCube action a Buttons row gives (port 1; 0 none)
int BindGc(int button);
// settings.ini is to be written when the popup closes (a change made
// without Set or Step: the Buttons tab's defaults)
void MarkChanged();

} // namespace PortXboxOptions

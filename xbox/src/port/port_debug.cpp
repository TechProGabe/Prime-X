// port_debug.cpp - PortDebug (port_debug.h) on the Xbox.
//
// MetroidPrimePort's PortDebug is its settings registry and F1 debug window
// (platform/debug_ui/options.cpp there). The Xbox has neither: every value is
// what that file returns under its "Original experience" switch, i.e. the
// GameCube's, except
//   - the light extras the user wants (docs/decisions.md): aspect and
//     widescreen HUD, FOV, skippable cutscenes; set from the Options rows
//     (docs/ui.md) through the setters below;
//   - the test hooks (docs/testing.md "Jumping to a room"): MP_BOOT_WORLD,
//     MP_FAST_BOOT, MP_SKIP_CUTSCENES, MP_CUTSCENE_SPEED, MP_INVULNERABLE and the
//     teleport requests, from getenv (a test build's autopad.txt "env" lines).
#include "port_debug.h"
#include "port_env.h"

#include <stdint.h>
#include <stdlib.h>

#include "xhw.h"
extern "C" {
#include "xsdk_settings.h"
}

namespace PortDebug {
namespace {
CStateManager* sStateManager = nullptr;
float sTickPeriod = 1.f / 60.f;

// the extras (Options rows)
EAspectMode sAspectMode = kAspect_4_3;
bool sHudWide = false;
float sFirstPersonFov = kFovRetail;
bool sSkippableCutscenes = false;
bool sMapScreenOpen = false;

// test hooks
int sTeleportArea = -1;
bool sWorldTeleport = false;
uint32_t sWorldTeleportWorld = 0, sWorldTeleportArea = 0;
bool sResetRequest = false;

bool EnvFlag(const char* name) { return port::EnvFlag(name, false); }
} // namespace

// ---- game state ----
void SetStateManager(CStateManager* mgr) { sStateManager = mgr; }
CStateManager* StateManager() { return sStateManager; }

// ---- boot and test hooks ----
bool FastBoot() {
  static const bool on = EnvFlag("MP_FAST_BOOT");
  return on;
}

bool BootWorld(uint32_t& worldId, uint32_t& areaAssetId) {
  static uint32_t sWorld = 0, sArea = 0;
  static const bool sSet = [] {
    const char* value = getenv("MP_BOOT_WORLD");
    if (value == nullptr || *value == 0)
      return false;
    char* end = nullptr;
    sWorld = static_cast< uint32_t >(strtoul(value, &end, 16));
    if (end != nullptr && *end == ':')
      sArea = static_cast< uint32_t >(strtoul(end + 1, nullptr, 16));
    xhw_logf("[TEST] boot world %08x area %08x", (unsigned)sWorld, (unsigned)sArea);
    return sWorld != 0;
  }();
  worldId = sWorld;
  areaAssetId = sArea;
  return sSet;
}

bool SkipCutscenes() {
  static const bool on = EnvFlag("MP_SKIP_CUTSCENES");
  return on;
}
float CutsceneSpeed() {
  static const float speed = [] {
    const char* v = getenv("MP_CUTSCENE_SPEED");
    const float f = v ? static_cast< float >(atof(v)) : 1.f;
    return f > 0.f ? f : 1.f;
  }();
  return speed;
}
bool Invulnerable() {
  static const bool on = EnvFlag("MP_INVULNERABLE");
  return on;
}

void RequestTeleport(int areaId) { sTeleportArea = areaId; }
bool ConsumeTeleportRequest(int& areaId) {
  if (sTeleportArea < 0)
    return false;
  areaId = sTeleportArea;
  sTeleportArea = -1;
  return true;
}
void RequestWorldTeleport(uint32_t worldId, uint32_t areaAssetId) {
  sWorldTeleport = true;
  sWorldTeleportWorld = worldId;
  sWorldTeleportArea = areaAssetId;
}
bool ConsumeWorldTeleportRequest(uint32_t& worldId, uint32_t& areaAssetId) {
  if (!sWorldTeleport)
    return false;
  sWorldTeleport = false;
  worldId = sWorldTeleportWorld;
  areaAssetId = sWorldTeleportArea;
  return true;
}
bool ConsumeWorldSweepRequest(CStateManager&) { return false; }
bool ConsumeResetRequest() {
  const bool r = sResetRequest;
  sResetRequest = false;
  return r;
}
bool ConsumeShowHintRequest() { return false; }

// ---- simulation timing: the GameCube's 60 Hz tick ----
unsigned SimRate() { return 60u; }
float SimPeriod() { return 1.f / 60.f; }
// One game update per frame with dt = the frame's time (MetroidPrimePort's
// adaptive mode, clamped to 1/30 s) instead of 60 a second with catch-up:
// on a console that draws ~40 fps every frame carried ~1.5 updates. The
// Port Options' "Game Timing" row (xsdk_settings sim_adaptive); test builds
// also take env PX_SIM_ADAPTIVE=0|1.
bool SimAdaptive() {
  static int sEnv = -2;
  if (sEnv == -2) {
    const char* v = XHW_TEST_BUILD ? getenv("PX_SIM_ADAPTIVE") : nullptr;
    sEnv = v != nullptr ? atoi(v) != 0 : -1;
  }
  return sEnv >= 0 ? sEnv != 0 : g_xsdk_settings.sim_adaptive != 0;
}
float TickPeriod() { return sTickPeriod; }
void SetTickPeriod(float dt) {
  if (dt > 0.f)
    sTickPeriod = dt;
}
float TickFrames() { return sTickPeriod * 60.f; }
bool FrameLimitEnabled() { return true; }
void SetFrameLimitEnabled(bool) {}
int FrameRateCap() { return 60; }
bool Turbo() { return false; }
unsigned TurboTicks() { return 1; }
bool TickHold() { return false; }
unsigned TakeHeldTicks() { return 0; }
bool ActorInterpolation() { return false; }
bool PoseInterpolation() { return false; }
bool ParticleInterpolation() { return false; }
bool PresentOverride(float&) { return false; }
void RecordFrame(unsigned long long, unsigned, bool) {}

// ---- the extras ----
EAspectMode AspectMode() { return sAspectMode; }
void SetAspectMode(EAspectMode mode) { sAspectMode = mode == kAspect_16_9 ? kAspect_16_9 : kAspect_4_3; }
bool HudWide() { return sHudWide; }
void SetHudWide(bool on) { sHudWide = on; }
float FirstPersonFov() { return sFirstPersonFov; }
void SetFirstPersonFov(float fov) {
  if (fov >= kFovMin && fov <= kFovMax)
    sFirstPersonFov = fov;
}
bool SkippableCutscenes() { return sSkippableCutscenes; }
void SetSkippableCutscenes(bool on) { sSkippableCutscenes = on; }
bool CinemaBars() { return true; }

// ---- presentation, retail ----
int HudScale() { return kHudScaleMax; }
void SetHudScale(int) {}
int HudOpacity(HudOpacityPart part) { return part == kHudOpacityEnergy ? kHudOpacityMax : kHudOpacityVisor; }
int CrosshairOpacity() { return kCrosshairOpacityMax; }
ECrosshairStyle CrosshairStyle() { return kCrosshairStyle_Classic; }
int CrosshairSize() { return kCrosshairSizeDefault; }
void SetCrosshairSize(int) {}
bool CrosshairColor(float* const) { return false; }
bool SecondaryCrosshairs() { return false; }
bool SharpScanWindow() { return false; }
int ScanSpread() { return kScanSpreadMax; }
float ScanSpeed() { return 1.f; }
bool HideHelmet() { return false; }
void SetHideHelmet(bool) {}
bool HideVisorEffects() { return false; }
void SetHideVisorEffects(bool) {}
void GunOffset(float* const offset) {
  offset[0] = offset[1] = offset[2] = 0.f;
}
int Msaa() { return 1; }
void SetMsaa(int) {}
bool SunShadows() { return false; }
EBootLogos BootLogos() { return kBootLogos_Show; }
EElevatorRide ElevatorRide() { return kElevatorRide_Original; }
void SetElevatorRide(EElevatorRide) {}
const char* TextLanguage() { return ""; }
bool WindowSize(int&, int&) { return false; }
bool OriginalExperience() { return true; }
void SetOriginalExperience(bool) {}
bool Visible() { return false; }
void DrawUI() {}
void DriverWorkaroundsStarted() {}
void RunGpuSelfTestIfRequested() {}
void SaveSettingsNow() {}
void RestoreGameOptions() {}

// ---- gameplay, retail ----
int WalkSpeed() { return kWalkSpeedDefault; }
bool FastMorph() { return false; }
void SetFastMorph(bool) {}
bool RapidCharge() { return false; }
void SetRapidCharge(bool) {}
bool StickyCharge() { return false; }
void SetStickyCharge(bool) {}
bool StackBeams() { return false; }
bool SpringBall() { return false; }
bool SpringBallRequested() { return false; }
bool SpringBallFlickPending() { return false; }
void ClearSpringBallFlick() {}
bool LockOnToggle() { return false; }
void SetLockOnToggle(bool) {}
bool LockOnFreeAim() { return false; }
bool RemasteredMovement() { return false; }
bool PhazonBeamCheat() { return false; }
bool UnlockFusionSuit() { return false; }
bool UnlockGalleries() { return false; }
bool UnlockHardMode() { return false; }
bool BeamRequested(int) { return false; }
bool VisorRequested(int) { return false; }
bool SwapScanXray() { return false; }
bool RadialMenus() { return false; }
void SetWheelState(unsigned) {}
void CaptureWheelIcons(int, CGuiModel* const*) {}
float HintDelaySeconds() { return 0.f; }
int ApSuitDamage() { return 1; }
bool SpeedrunTimer() { return false; }
void SetSpeedrunTimer(bool) {}

// ---- map screen ----
EMapPickups MapPickups() { return kMapPickups_Off; }
void SetMapPickups(EMapPickups) {}
bool MapPickupKinds() { return false; }
bool MapStationPickups() { return false; }
bool RevealMap() { return false; }
void SetRevealMap(bool) {}
bool MapScreenOpen() { return sMapScreenOpen; }
void SetMapScreenOpen(bool open) { sMapScreenOpen = open; }
void SetMinimapRect(bool, bool, float, float, float, float) {}
bool TakeMapPan(float*, float*, float*) { return false; }
float TakeMapRotate() { return 0.f; }
float TakeMapZoom() { return 0.f; }
void SetPauseScreenOpen(bool, bool) {}

// ---- aim: the pad only (no mouse, gyro, touch, twin stick) ----
bool AimCrosshair() { return false; }
bool AimInitialized() { return false; }
bool AutoAim() { return false; }
bool DirectAim() { return false; }
bool MouseAim() { return false; }
bool PointerAim() { return false; }
bool PointerAimPosition(float&, float&) { return false; }
bool PresentedAimDelta(float, float&, float&) { return false; }
bool PresentedPointerAimPosition(float, float&, float&) { return false; }
float AimPitch() { return 0.f; }
float AimYaw() { return 0.f; }
void AddStickAim(float, float, float) {}
float StickAimRate() { return 0.f; }
void SetStickAimRate(float) {}
bool UpdateMouseAim(bool, bool, float, float, float, bool, bool) { return false; }
void SynchronizeMouseAim(float, float, float) {}
void TurnMouseAim(float) {}
void ResetMouseAim() {}
bool MouseCaptured() { return false; }
void SetMouseCaptured(bool) {}
bool MouseGameplayActive() { return false; }
void SetMouseGameplayActive(bool) {}
unsigned MouseHeldButtons() { return 0; }
void AddMouseDelta(float, float) {}
void NoteMouseButton(bool, unsigned, bool) {}
void ClearMouseButtons() {}
void BeginFrameMouse() {}
bool PadTwinStick() { return false; }
bool TwinStick() { return false; }
void SetTwinStick(bool) {}
float TwinStickRightY() { return 0.f; }
void SetTwinStickRightY(float) {}
bool TakeTouchLook(float&, float&) { return false; }
bool TouchAimDown() { return false; }
bool TapUpdateToast(float, float) { return false; }
void PollGyro() {}
void UpdateControllerNav() {}

} // namespace PortDebug

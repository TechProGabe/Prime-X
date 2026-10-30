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
#include <stdio.h>
#include <stdlib.h>

#include "xhw.h"
extern "C" {
#include "xsdk_settings.h"
int xsdk_lockstep(void);
unsigned xsdk_frame_count(void);
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

// ---- simulation timing ----
// SimRate/SimPeriod stay the GameCube's 60 Hz: the projectile weapons step in
// 1/60 s substeps whatever the tick (CProjectileWeapon::GetTickPeriod).
unsigned SimRate() { return 60u; }
float SimPeriod() { return 1.f / 60.f; }

namespace {
// test builds: env PX_SIM_ADAPTIVE=0|1 (Flex), PX_SIM_AUTO=0|1 (Auto or
// Fixed), PX_SIM_RATE=30|60 (the step, held), PX_INTERP=0|1 (draw between
// ticks or not, at any step); -1 where not set
int EnvInt(const char* name) {
  const char* v = XHW_TEST_BUILD ? getenv(name) : nullptr;
  return v != nullptr && *v ? atoi(v) : -1;
}
int sEnvAdaptive = -2, sEnvAuto, sEnvRate, sEnvInterp;
void ReadEnv() {
  if (sEnvAdaptive != -2)
    return;
  sEnvAdaptive = EnvInt("PX_SIM_ADAPTIVE");
  sEnvAuto = EnvInt("PX_SIM_AUTO");
  sEnvRate = EnvInt("PX_SIM_RATE");
  sEnvInterp = EnvInt("PX_INTERP");
  if (sEnvAdaptive >= 0 || sEnvAuto >= 0 || sEnvRate >= 0 || sEnvInterp >= 0)
    xhw_logf("[TIMING] test: PX_SIM_ADAPTIVE %d PX_SIM_AUTO %d PX_SIM_RATE %d PX_INTERP %d", sEnvAdaptive,
             sEnvAuto, sEnvRate, sEnvInterp);
}

int Mode() {
  ReadEnv();
  if (sEnvAdaptive > 0)
    return XSDK_SIM_FLEX;
  if (sEnvAuto >= 0)
    return sEnvAuto ? XSDK_SIM_AUTO : XSDK_SIM_FIXED;
  if (sEnvAdaptive == 0 && g_xsdk_settings.sim_mode == XSDK_SIM_FLEX)
    return XSDK_SIM_FIXED;
  return g_xsdk_settings.sim_mode;
}

// Game Timing: Auto. Each presented frame (px_sim_frame, from vi.c) looks at
// the frame's CPU time ([PERF]'s buckets but the GPU and vsync waits):
//   - at 60 Hz ticks, 3 of the last 4 frames over 16.0 ms: the frame doesn't
//     fit, and Fixed would run more ticks the next frame (the catch-up
//     spiral): go to 30 Hz ticks, drawn at 60 blended between the last two;
//   - at 30 Hz, the cost of a 60 Hz frame (ms per tick, from frames that ran
//     one, plus the frame's draw work) under 14 ms for 30 frames running,
//     and at least a second at 30: back to 60.
// At 60 Hz it is Fixed exactly. Lockstep test runs (PX_LOCKSTEP) hold 60 Hz,
// so their frames don't depend on the host's speed; PX_SIM_RATE holds a step.
constexpr uint32_t kOverUs = 16000, kUnderUs = 14000;
constexpr int kUnderFrames = 30, kDwellFrames = 60;
bool sAuto30 = false;
unsigned sOverBits = 0;   // the last 4 frames: over 16 ms
int sUnderRun = 0, sDwell = 0;
uint32_t sTickUs = 0;     // a tick's cost, smoothed (frames that ran ticks)
uint32_t sFrames = 0, sFrames30 = 0, sSwitches = 0;   // since the last [PERF] line
char sTag[64];

const char* PerfTag() {
  if (Mode() != XSDK_SIM_AUTO || sFrames == 0)
    return "";
  snprintf(sTag, sizeof sTag, " | auto: 30 Hz %u%% of frames, %u switches", sFrames30 * 100u / sFrames, sSwitches);
  sFrames = sFrames30 = sSwitches = 0;
  return sTag;
}

void SetAuto30(bool on, uint32_t cpuUs, uint32_t estUs) {
  if (on == sAuto30)
    return;
  sAuto30 = on;
  ++sSwitches;
  sUnderRun = 0;
  sOverBits = 0;
  sDwell = on ? kDwellFrames : 0;
  static unsigned sLogged;
  if (sLogged < 40) {   // the first ones as they come; [PERF] counts the rest
    ++sLogged;
    xhw_logf("[TIMING] auto: %s at frame %u (frame %u us, a 60 Hz frame ~%u us)", on ? "30 Hz" : "60 Hz",
             xsdk_frame_count(), (unsigned)cpuUs, (unsigned)estUs);
  }
}
} // namespace

// UpdateTicks asks once a frame
double XboxTickPeriod() {
  ReadEnv();
  if (sEnvRate == 30 || sEnvRate == 60)
    return 1.0 / sEnvRate;
  return Mode() == XSDK_SIM_AUTO && sAuto30 ? 1.0 / 30.0 : 1.0 / 60.0;
}

bool XboxPresentBlend() {
  ReadEnv();
  if (sEnvInterp >= 0)
    return sEnvInterp != 0;
  return !SimAdaptive() && XboxTickPeriod() > 1.0 / 50.0;
}

unsigned XboxRenderTimingSteps(unsigned ticks, double period) {
  static double sCarry = 0.0;
  if (period > 0.99 / 60.0 && period < 1.01 / 60.0)
    return ticks;   // 60 Hz: one step a tick, as retail
  const double steps = sCarry + ticks * period * 60.0;
  const unsigned whole = static_cast< unsigned >(steps + 1e-6);
  sCarry = steps - whole;
  if (sCarry < 0.0)
    sCarry = 0.0;
  return whole;
}

// One game update per frame with dt = the frame's time (MetroidPrimePort's
// adaptive mode, clamped to 1/30 s) instead of 60 a second with catch-up:
// Game Timing: Flex. Test builds also take env PX_SIM_ADAPTIVE=0|1.
bool SimAdaptive() { return Mode() == XSDK_SIM_FLEX; }
float TickPeriod() { return sTickPeriod; }
void SetTickPeriod(float dt) {
  if (dt > 0.f)
    sTickPeriod = dt;
}
float TickFrames() { return sTickPeriod * 60.f; }
// vi.c paces every frame to 60 Hz: the clock keeps its cadence leeway
// (FixedStepClock::Advance); main.cpp draws between ticks when
// XboxPresentBlend says so.
bool FrameLimitEnabled() { return true; }
void SetFrameLimitEnabled(bool) {}
int FrameRateCap() { return 60; }
bool Turbo() { return false; }
unsigned TurboTicks() { return 1; }
bool TickHold() { return false; }
unsigned TakeHeldTicks() { return 0; }
// MetroidPrimePort's frame interpolation (docs/fps-plan.md milestone 1):
// actors, skinned poses, particles, swooshes, beams and the camera blend
// between the last two ticks while Auto runs 30 Hz ticks
bool ActorInterpolation() { return XboxPresentBlend(); }
bool PoseInterpolation() { return XboxPresentBlend(); }
bool ParticleInterpolation() { return XboxPresentBlend(); }
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

// Game Timing: Auto's controller (PortDebug, above), once a presented frame
// from vi.c's frame boundary
extern "C" void px_sim_frame(void) {
  using namespace PortDebug;
  uint32_t us[XHW_PERF_N], ticks;
  if (xhw_perf_tag == nullptr)
    xhw_perf_tag = &PerfTag;
  if (Mode() != XSDK_SIM_AUTO || sEnvRate > 0 || xsdk_lockstep()) {
    sAuto30 = false;
    return;
  }
  xhw_perf_last_frame(us, &ticks);
  // the frame's own work: the GPU and vsync waits are what's left over
  const uint32_t draw = us[XHW_PERF_RENDER] + us[XHW_PERF_DLIST] + us[XHW_PERF_DRAW] + us[XHW_PERF_TEX] +
                        us[XHW_PERF_EFB];
  const uint32_t cpu = us[XHW_PERF_LOGIC] + draw;
  if (ticks > 0) {
    const uint32_t perTick = us[XHW_PERF_LOGIC] / ticks;
    sTickUs = sTickUs ? (sTickUs * 3 + perTick) / 4 : perTick;
  }
  const uint32_t est = sTickUs + draw;   // a frame of one 60 Hz tick
  ++sFrames;
  if (sAuto30)
    ++sFrames30;
  if (!sAuto30) {
    sOverBits = ((sOverBits << 1) | (cpu > kOverUs ? 1u : 0u)) & 0xFu;
    if (__builtin_popcount(sOverBits) >= 3)
      SetAuto30(true, cpu, est);
    return;
  }
  if (sDwell > 0)
    --sDwell;
  sUnderRun = est < kUnderUs ? sUnderRun + 1 : 0;
  if (sUnderRun >= kUnderFrames && sDwell == 0)
    SetAuto30(false, cpu, est);
}

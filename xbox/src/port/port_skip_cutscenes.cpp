// port_skip_cutscenes.cpp - PortSkipCutscenes (port_skip_cutscenes.h) on the
// Xbox: Port Options' "Skip Cutscenes" (the user's extra, docs/ui.md).
//
// MetroidPrimePort's version (platform/port_skip_cutscenes.cpp there) also
// patches rooms for its randomizer and Archipelago games, which the Xbox
// doesn't have. This keeps its plain path: when the option is on, each
// room's script layers get randomprime's skippable-cutscene op stream as the
// room loads (CGameArea), applied by MetroidPrimePort's op interpreter
// (port_skip_cutscenes_ops.cpp) from its generated tables
// (port_skip_streams.cpp, port_skip_cutscenes_data.inc; imported unchanged).
// A room whose script doesn't match its stream (a modified disc) is left as
// it is. The option takes effect from the next room load; skipping a
// cinematic with START (CStateManager) follows it at once.
#include "port_skip_cutscenes.h"
#include "port_skip_streams.h"

#include "port_debug.h"
#include "port_disc.h"

#include "xhw.h"

namespace PortSkipCutscenes {
namespace {

// Builds an op stream in the generated streams' format (ApplyOps)
struct OpWriter {
  std::vector< uint8_t > ops;
  void u32(uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8)
      ops.push_back(uint8_t(v >> shift));
  }
  void conn(uint8_t op, uint32_t sender, uint32_t state, uint32_t msg, uint32_t target) {
    ops.push_back(op);
    u32(sender);
    u32(state);
    u32(msg);
    u32(target);
  }
};

const uint32_t kHallOfTheElders = 0xFB54A0CB;

// As MetroidPrimePort (its issue #44): randomprime's Hall of the Elders skip
// leaves Samus without controls after the statue's throw unless this
// releases them where retail does.
std::vector< uint8_t > HallOfTheEldersOps() {
  OpWriter w;
  w.conn(4, 0x0034004D, 3 /* Entered */, 5 /* Decrement */, 0x003402E6);
  w.conn(4, 0x0034004A, 5 /* Inactive */, 5, 0x003402E6);
  return std::move(w.ops);
}

} // namespace

bool Forced() { return false; }   // no randomizer on the Xbox

bool Active() { return PortDebug::SkippableCutscenes(); }

bool PatchArea(uint32_t mreaId, const uint8_t* scly, size_t size, std::vector< uint8_t >& out) {
  SkipStream stream;
  out.clear();
  if (!Active() || !SkipStreamFor(PortDisc::Current(), mreaId, stream))
    return false;
  const int misses = ApplyOps(scly, size, stream.ops, stream.size, out);
  if (misses != 0) {
    xhw_logf("[SKIP] room %08X doesn't match its skip (%d), left as it is", (unsigned)mreaId, misses);
    out.clear();
    return false;
  }
  xhw_logf("[SKIP] room %08X: cutscenes skippable (%u bytes of ops)", (unsigned)mreaId, (unsigned)stream.size);
  if (mreaId == kHallOfTheElders) {
    const std::vector< uint8_t > ops = HallOfTheEldersOps();
    std::vector< uint8_t > hall;
    if (ApplyOps(out.data(), out.size(), ops.data(), ops.size(), hall) == 0)
      out.swap(hall);
    else
      xhw_logf("[SKIP] Hall of the Elders control release doesn't match, left as is");
  }
  return true;
}

// randomized pickups only
bool IsPickupDependency(uint32_t) { return false; }
bool PickupPlacementFor(uint32_t, PickupPlacement&) { return false; }

} // namespace PortSkipCutscenes

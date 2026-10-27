// Prime-X twin of MetroidPrimePort's port_map_side.h (its map side files keep
// seen pickups and room marks next to the card, through std::filesystem, which
// nxdk lacks). Dropped feature on the Xbox: the same names, doing nothing, so
// CMemoryCardDriver's calls compile and the save stays the retail one.
#pragma once

#include <cstddef>
#include <cstdint>

#include "port_map_marks.h"
#include "port_map_pickups.h"

namespace PortMapSide {

constexpr size_t kSeenWords = PortMapPickups::kSeenWords;

struct Data {
  uint32_t seen[kSeenWords] = {};
  PortMapMarks::List marks;
};

struct SlotBytes {
  const uint8_t* bytes = nullptr;
  size_t size = 0;
};

inline void Stage(int, const uint8_t*, size_t, const Data&) {}
inline void StageErase(int) {}
inline int Flush(const SlotBytes*, int) { return 0; }
inline bool Apply(int, const uint8_t*, size_t, uint32_t*, PortMapMarks::List&) { return false; }

} // namespace PortMapSide

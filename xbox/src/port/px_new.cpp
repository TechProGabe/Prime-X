// px_new.cpp - the global operator new and delete, for every C++ unit in the
// XBE (game, libc++). In place of libc++'s: one place to measure and to
// choose where the objects live (docs/architecture.md "Memory").
//
// Once the game's heap is up, objects go into it (CGameAllocator in MEM1,
// as on the GameCube, plan 0.6): the game's resources and its objects then
// share one heap and its holes, where two heaps each kept their own free
// space (round hw5 ran out of Xbox memory with ~10 MB of MEM1 unused).
// When the game's heap has no room, the Xbox's heap (malloc) takes it; a
// block is freed by the heap whose address range holds it.
//
// -DPX_HEAP_PROFILE=1 (on in test builds): every block carries a small
// header naming its caller, live bytes are summed per caller, and
// px_heap_report() logs the biggest holders ([HEAP] lines, symbolize with
// tools/xbox/sym.py). Big single allocations are logged as they happen
// ([NEW], from PX_NEW_LOG_BYTES).
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

#include "xhw.h"

void* PortXboxHeapAlloc(size_t len); // src/Kyoto/Alloc/CMemory.cpp
void PortXboxHeapFree(const void* ptr);
unsigned PortXboxHeapFreeBytes();
extern "C" int xsdk_in_mem1(const void* p); // xbox/src/sdk/os.c

namespace {
void* raw_alloc(size_t n) {
  void* p = PortXboxHeapAlloc(n);
  return p != nullptr ? p : std::malloc(n);
}

void raw_free(void* p) {
  if (xsdk_in_mem1(p)) {
    PortXboxHeapFree(p);
  } else {
    std::free(p);
  }
}
} // namespace

#ifndef PX_HEAP_PROFILE
#define PX_HEAP_PROFILE XHW_TEST_BUILD
#endif
#ifndef PX_NEW_LOG_BYTES
#define PX_NEW_LOG_BYTES (XHW_TEST_BUILD ? 512u * 1024u : 0u)
#endif

#if PX_HEAP_PROFILE
namespace {
struct Header {
  uint32_t size;
  uint32_t site;  // index into s_sites
};
static_assert(sizeof(Header) == 8, "keeps 8-byte alignment");
struct Site {
  uintptr_t caller;
  uint32_t live, count;
};
constexpr uint32_t kSites = 4096;
Site s_sites[kSites];
xhw_mutex* s_lock;

uint32_t site_of(uintptr_t caller) {
  uint32_t h = static_cast< uint32_t >(caller * 2654435761u) % kSites;
  for (uint32_t i = 0; i < kSites; ++i, h = (h + 1) % kSites) {
    if (s_sites[h].caller == caller)
      return h;
    if (s_sites[h].caller == 0) {
      s_sites[h].caller = caller;
      return h;
    }
  }
  return 0;  // table full: lumped in with slot 0
}
} // namespace

extern "C" void px_heap_report(const char* why) {
  if (!s_lock)
    return;
  Site top[30] = {};
  uint64_t total = 0;
  xhw_mutex_lock(s_lock);
  for (uint32_t i = 0; i < kSites; ++i) {
    const Site& s = s_sites[i];
    total += s.live;
    for (int k = 0; k < 30; ++k) {
      if (s.live > top[k].live) {
        for (int m = 29; m > k; --m)
          top[m] = top[m - 1];
        top[k] = s;
        break;
      }
    }
  }
  xhw_mutex_unlock(s_lock);
  xhw_logf("[HEAP] %s: %u KB live through operator new, game heap %u KB free", why,
           static_cast< unsigned >(total / 1024), PortXboxHeapFreeBytes() / 1024);
  for (int k = 0; k < 30 && top[k].live; ++k)
    xhw_logf("[HEAP]   %6u KB in %6u blocks from %08x", top[k].live / 1024, top[k].count,
             static_cast< unsigned >(top[k].caller));
}
#else
extern "C" void px_heap_report(const char*) {}
#endif

static void* px_alloc(size_t n, void* caller) {
#if PX_HEAP_PROFILE
  if (!s_lock)
    s_lock = xhw_mutex_create();
  Header* h = static_cast< Header* >(raw_alloc((n ? n : 1) + sizeof(Header)));
  void* p = h ? h + 1 : nullptr;
  if (h) {
    xhw_mutex_lock(s_lock);
    const uint32_t site = site_of(reinterpret_cast< uintptr_t >(caller));
    s_sites[site].live += static_cast< uint32_t >(n);
    s_sites[site].count++;
    xhw_mutex_unlock(s_lock);
    h->size = static_cast< uint32_t >(n);
    h->site = site;
  }
#else
  void* p = raw_alloc(n ? n : 1);
#endif
  if (PX_NEW_LOG_BYTES && n >= PX_NEW_LOG_BYTES)
    xhw_logf("[NEW] %u KB at %p from %p", static_cast< unsigned >(n / 1024), p, caller);
  if (p == nullptr) {
    xhw_logf("[NEW] out of memory: %u bytes from %p (free %u KB)", static_cast< unsigned >(n), caller,
             xhw_mem_free_kb());
    xhw_fatal("Out of memory", "The game ran out of memory (boot.log has the [NEW] line).");
  }
  return p;
}

static void px_free(void* p) {
  if (p == nullptr)
    return;
#if PX_HEAP_PROFILE
  Header* h = static_cast< Header* >(p) - 1;
  xhw_mutex_lock(s_lock);
  s_sites[h->site].live -= h->size;
  s_sites[h->site].count--;
  xhw_mutex_unlock(s_lock);
  raw_free(h);
#else
  raw_free(p);
#endif
}

static void* px_alloc_aligned(size_t n, size_t align, void* caller) {
  // over-aligned objects: over-allocate and keep the raw pointer just before
  const size_t extra = align + sizeof(void*);
  char* raw = static_cast< char* >(px_alloc(n + extra, caller));
  uintptr_t p = (reinterpret_cast< uintptr_t >(raw) + sizeof(void*) + align - 1) & ~(uintptr_t)(align - 1);
  reinterpret_cast< void** >(p)[-1] = raw;
  return reinterpret_cast< void* >(p);
}

static void px_free_aligned(void* p) {
  if (p)
    px_free(reinterpret_cast< void** >(p)[-1]);
}

void* operator new(size_t n) { return px_alloc(n, __builtin_return_address(0)); }
void* operator new[](size_t n) { return px_alloc(n, __builtin_return_address(0)); }
void* operator new(size_t n, const std::nothrow_t&) noexcept { return px_alloc(n, __builtin_return_address(0)); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return px_alloc(n, __builtin_return_address(0)); }
void operator delete(void* p) noexcept { px_free(p); }
void operator delete[](void* p) noexcept { px_free(p); }
void operator delete(void* p, size_t) noexcept { px_free(p); }
void operator delete[](void* p, size_t) noexcept { px_free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { px_free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { px_free(p); }

void* operator new(size_t n, std::align_val_t a) {
  return px_alloc_aligned(n, static_cast< size_t >(a), __builtin_return_address(0));
}
void* operator new[](size_t n, std::align_val_t a) {
  return px_alloc_aligned(n, static_cast< size_t >(a), __builtin_return_address(0));
}
void operator delete(void* p, std::align_val_t) noexcept { px_free_aligned(p); }
void operator delete[](void* p, std::align_val_t) noexcept { px_free_aligned(p); }
void operator delete(void* p, size_t, std::align_val_t) noexcept { px_free_aligned(p); }
void operator delete[](void* p, size_t, std::align_val_t) noexcept { px_free_aligned(p); }

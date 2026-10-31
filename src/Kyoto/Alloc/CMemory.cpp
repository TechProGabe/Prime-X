#include "Kyoto/Alloc/CMemory.hpp"
#include "Kyoto/Alloc/CMemorySys.hpp"

#include "Kyoto/Alloc/CCallStack.hpp"
#include "Kyoto/Alloc/CGameAllocator.hpp"
#include "Kyoto/Basics/RAssertDolphin.hpp"

#include "dolphin/os.h"

#ifdef TARGET_PC
#include <mutex>

#ifdef TARGET_XBOX
// PORT_XBOX: the heap takes only HeapMutex, not OSDisableInterrupts as well.
// On the Xbox OSDisableInterrupts is a real lock (os.c), and with both the
// heap ordered heap -> interrupts against CDSPStreamManager's interrupts ->
// MusyX lock and the mixer's MusyX lock -> heap (CDSPStream::StopStream frees
// a one-shot stream's buffer from UpdateStream): the three threads' cycle
// deadlocked red when a Map Station's jingle ended (free play, 2026-10-30).
// HeapMutex alone serialises the heap; on PC OSDisableInterrupts was a no-op.
#define PX_HEAP_INTS(e) ((void)0)
#define PX_HEAP_RESTORE(e) ((void)0)
#else
#define PX_HEAP_INTS(e) (e = OSDisableInterrupts())
#define PX_HEAP_RESTORE(e) OSRestoreInterrupts(e)
#endif

// The guest serialised the heap with OSDisableInterrupts, which is a no-op on PC,
// and the heap is not only touched by the game thread here: Aurora's DVD worker
// (CDSPStream::ReadCompleted) and the MusyX mixer thread (CDSPStream::UpdateStream)
// free one-shot stream buffers. Recursive in case an out-of-memory callback
// allocates or frees again from inside the allocator. Never destroyed, so a free
// from a static destructor after main still finds it.
static std::recursive_mutex& HeapMutex() {
#ifdef TARGET_XBOX
  // PORT_XBOX: not through operator new, which comes here (PortXboxHeapAlloc)
  alignas(std::recursive_mutex) static unsigned char storage[sizeof(std::recursive_mutex)];
  static std::recursive_mutex* mutex = new (storage) std::recursive_mutex;
#else
  static std::recursive_mutex* mutex = new std::recursive_mutex;
#endif
  return *mutex;
}
#endif

static CGameAllocator gGameAllocator;
IAllocator* CMemory::mpAllocator = &gGameAllocator;
bool CMemory::mInitialized;
uint gLeakCount = 0;
uint gLeakBytes = 0;

CMemorySys::CMemorySys(COsContext& ctx, IAllocator& allocator) {
  CMemory::Startup(ctx);
  CMemory::SetAllocator(ctx, allocator);
}

CMemorySys::~CMemorySys() { CMemory::Shutdown(); }

IAllocator& CMemorySys::GetGameAllocator() { return gGameAllocator; }

#ifdef TARGET_XBOX
static bool sPortXboxHeapUp; // PORT_XBOX: CMemory's mInitialized, for operator new
#endif

void CMemory::Startup(COsContext& ctx) {
  mInitialized = mpAllocator->Initialize(ctx);
#ifdef TARGET_XBOX
  sPortXboxHeapUp = mInitialized;
#endif
}

void CMemory::SetAllocator(COsContext& ctx, IAllocator& allocator) {
  if (mpAllocator != &allocator) {
    if (mpAllocator != nullptr) {
      mpAllocator->ReleaseAll();
    }
    mpAllocator = &allocator;
    mpAllocator->Initialize(ctx);
  }
}

static bool cmemory_enum_alloc_cb(const IAllocator::SAllocInfo& info, const void* ptr) {
  if (info.x8_isAllocated && info.x9_ == 0) {
    ++gLeakCount;
    gLeakBytes += info.x4_len;
  }
  return true;
}

void CMemory::Shutdown() {
  CMemory::mInitialized = false;
#ifdef TARGET_XBOX
  sPortXboxHeapUp = false;
#endif

  if (mpAllocator->GetMetrics().x8_ != 0) {
    gLeakCount = 0;
    gLeakBytes = 0;
    mpAllocator->EnumAllocations((IAllocator::FEnumAllocationsCb)cmemory_enum_alloc_cb, nullptr,
                                 false);
  }
  mpAllocator->Shutdown();
}

void* CMemory::Alloc(size_t len, IAllocator::EHint hint, IAllocator::EScope scope,
                     IAllocator::EType type, const CCallStack& callstack) {
#ifdef TARGET_PC
  std::lock_guard< std::recursive_mutex > heapLock(HeapMutex());
  volatile bool enabled = false;
  PX_HEAP_INTS(enabled);
#else
  volatile bool enabled = OSDisableInterrupts();
#endif
  void* ret = mpAllocator->Alloc(len, hint, scope, type, callstack);
  if (ret == nullptr) {
    rs_debugger_printf("Alloc failed - Size: %zu", len);
#ifdef TARGET_PC
    PX_HEAP_RESTORE(enabled);
    throw std::bad_alloc();
#endif
  }

#ifdef TARGET_PC
  PX_HEAP_RESTORE(enabled);
#else
  OSRestoreInterrupts(enabled);
#endif
  return ret;
}

void CMemory::Free(const void* ptr) {
#ifdef TARGET_PC
  std::lock_guard< std::recursive_mutex > heapLock(HeapMutex());
  volatile bool enabled = false;
  PX_HEAP_INTS(enabled);
#else
  volatile bool enabled = OSDisableInterrupts();
#endif
  if (ptr != nullptr) {
    mpAllocator->Free(ptr);
  }
#ifdef TARGET_PC
  PX_HEAP_RESTORE(enabled);
#else
  OSRestoreInterrupts(enabled);
#endif
}

#ifdef TARGET_PC
// Port: MP_HEAP_CHECK hooks for CFrameDelayedKiller (see CGameAllocator::CheckHeap).
bool PortHeapCheckEnabled() { return CGameAllocator::HeapCheckEnabled(); }

void PortHeapCheckAll() {
  std::lock_guard< std::recursive_mutex > heapLock(HeapMutex());
  gGameAllocator.CheckHeap();
}

bool PortHeapPeekBlock(const void* ptr, const char** fileAndLine, size_t* len) {
  std::lock_guard< std::recursive_mutex > heapLock(HeapMutex());
  return gGameAllocator.PeekLiveBlock(ptr, fileAndLine, len);
}
#endif

#ifdef TARGET_XBOX
// PORT_XBOX: game `new` lives in this heap, as on the GameCube (plan 0.6,
// xbox/src/port/px_new.cpp). Null when the heap isn't up or has no room,
// never a throw: operator new then takes the Xbox's own heap.
extern bool gPortXboxQuietOom; // CGameAllocator.cpp

void* PortXboxHeapAlloc(size_t len) {
  // the flag first: HeapMutex's own first `new` comes through here
  if (!sPortXboxHeapUp) {
    return nullptr;
  }
  std::lock_guard< std::recursive_mutex > heapLock(HeapMutex());
  if (!sPortXboxHeapUp || gPortXboxQuietOom) {
    return nullptr;
  }
  gPortXboxQuietOom = true;
  void* ret = gGameAllocator.Alloc(len, IAllocator::kHI_None, IAllocator::kSC_Unk1,
                                   IAllocator::kTP_Heap, CCallStack(-1, "\?\?(\?\?)", nullptr));
  gPortXboxQuietOom = false;
  return ret;
}

void PortXboxHeapFree(const void* ptr) {
  std::lock_guard< std::recursive_mutex > heapLock(HeapMutex());
  if (sPortXboxHeapUp) {
    gGameAllocator.Free(ptr);
  }
}

unsigned PortXboxHeapFreeBytes() { return sPortXboxHeapUp ? gGameAllocator.GetMetrics().x14_heapSize2 : 0; }
#endif

void CMemory::SetOutOfMemoryCallback(IAllocator::FOutOfMemoryCb cb, const void* context) {
  mpAllocator->SetOutOfMemoryCallback(cb, context);
}

void CMemory::OffsetFakeStatics(int offset) { mpAllocator->OffsetFakeStatics(offset); }

void* operator new(size_t sz, const char* fileAndLine, const char* type) {
  return CMemory::Alloc(sz, IAllocator::kHI_None, IAllocator::kSC_Unk1, IAllocator::kTP_Heap,
                        CCallStack(-1, fileAndLine, type));
}
void* operator new[](size_t sz, const char* fileAndLine, const char* type) {
  return CMemory::Alloc(sz, IAllocator::kHI_None, IAllocator::kSC_Unk1, IAllocator::kTP_Array,
                        CCallStack(-1, fileAndLine, type));
}

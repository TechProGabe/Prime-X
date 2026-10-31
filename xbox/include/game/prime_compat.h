/* prime_compat.h - force-included ahead of every game translation unit on the
 * Xbox (xbox/CMakeLists.txt). The Xbox twin of MetroidPrimePort's
 * platform/compat.h: the same SDK-facing view, plus what nxdk's headers lack.
 * Keep it small: anything here changes every game object. */
#pragma once

/* aurora's dolphin/os.h uses va_list without including <stdarg.h>; glibc and
 * MSVC happen to provide it through other headers, pdclib does not. */
#include <stdarg.h>
#include <stdint.h>

/* Game code that throws (rstl's allocator, the resource loaders, the
 * pathfinder's bounds checks) is compiled without exceptions on the Xbox:
 * nxdk's libc++ has none. PX_THROW logs where and stops (xbox/src/port). */
#ifdef __cplusplus
extern "C" [[noreturn]] void px_fatal_throw(const char* what, const char* file, int line);
#else
_Noreturn void px_fatal_throw(const char* what, const char* file, int line);
#endif
#define PX_THROW(what) px_fatal_throw(what, __FILE__, __LINE__)

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
/* The native build uses host new/delete. CMemory allocations have a separate
 * lifetime and must be returned explicitly through CMemory::Free. */
#include <Kyoto/Alloc/CMemory.hpp>
#endif

/* MWCC-isms used throughout the decompiled code. */
#define nofralloc
#define __abs(x) ((x) < 0 ? -(x) : (x))

/* Umbrella inclusion, as the original SDK headers provided transitively. */
#include <dolphin/gx.h>
#include <dolphin/gx/GXShims.h>
#include <dolphin/si.h>
#include <dolphin/pad.h>
#include <dolphin/os.h>
#include <dolphin/card.h>

#ifdef __cplusplus
extern "C" {
#endif
/* Drains deferred ARQ completion callbacks (xbox/src/sdk/ar.c). */
void ARQPoll(void);
#ifdef __cplusplus
}
#endif

/* Aurora names the PADStatus triggers triggerLeft/triggerRight under
 * TARGET_PC; the game uses the SDK names triggerL/triggerR. */
#define triggerL triggerLeft
#define triggerR triggerRight

#ifndef AUTO
#define AUTO(name, val) auto name = val
#define AUTO_REF(name, val) auto& name = val
#define AUTO_CONST_REF(name, val) const auto& name = val
#endif

#ifdef __cplusplus
static_assert((char)-1 < 0, "plain char must be signed");
static_assert(sizeof(void*) == 4 && sizeof(wchar_t) == 2, "Xbox: 32-bit pointers, 16-bit wchar_t");
#else
_Static_assert((char)-1 < 0, "plain char must be signed");
#endif

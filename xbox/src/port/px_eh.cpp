// px_eh.cpp - what a C++ throw does on the Xbox.
//
// The game compiles with -fcxx-exceptions but without -fexceptions
// (xbox/CMakeLists.txt): `throw` and `try` are accepted, no unwind tables are
// made, and every catch arm is dead. MetroidPrimePort's throws are all
// "this resource or disc read is broken" (rstl's allocator, CInputStream,
// CResFactory, the pathfinder's bounds checks), so a throw is fatal here: the
// exception's type and message go to the log and the error screen.
//
// _CxxThrowException(object, ThrowInfo) is the MSVC C++ ABI's throw entry; the
// ThrowInfo lists the types the object can be caught as, which tells whether
// it is a std::exception and where that base sits in the object.
#include <exception>
#include <stdio.h>
#include <string.h>

#include "xhw.h"

namespace {
struct TypeDescriptor {
  const void* vftable;
  void* spare;
  char name[1];  // decorated: ".?AVruntime_error@std@@"
};
struct CatchableType {
  unsigned properties;
  const TypeDescriptor* type;
  int mdisp, pdisp, vdisp;  // where this base is in the thrown object
  int sizeOrOffset;
  void* copyFunction;
};
struct CatchableTypeArray {
  int count;
  const CatchableType* types[1];
};
struct ThrowInfo {
  unsigned attributes;
  void* unwind;
  void* forwardCompat;
  const CatchableTypeArray* catchable;
};
}  // namespace

extern "C" [[noreturn]] void px_fatal_throw(const char* what, const char* file, int line) {
  char msg[256];
  snprintf(msg, sizeof msg, "%s (%s:%d)", what ? what : "C++ exception", file ? file : "?", line);
  xhw_fatal("Game error", msg);
}

extern "C" [[noreturn]] void __stdcall _CxxThrowException(void* object, const ThrowInfo* info) {
  const char* type = "?";
  const char* what = nullptr;
  if (info != nullptr && info->catchable != nullptr && info->catchable->count > 0) {
    const CatchableTypeArray* types = info->catchable;
    type = types->types[0]->type->name;
    for (int i = 0; i < types->count; ++i) {
      const CatchableType* ct = types->types[i];
      if (object != nullptr && strcmp(ct->type->name, ".?AVexception@std@@") == 0) {
        what = reinterpret_cast< const std::exception* >(static_cast< char* >(object) + ct->mdisp)->what();
        break;
      }
    }
  } else if (object == nullptr) {
    type = "rethrow";
  }
  char msg[256];
  snprintf(msg, sizeof msg, "%s: %s", type, what ? what : "(no message)");
  xhw_logf("[THROW] %s", msg);
  xhw_fatal("Game error", msg);
}

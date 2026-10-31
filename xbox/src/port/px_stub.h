// px_stub.h - the body of a dropped Port* hook (tools/xbox/gen_port_stubs.py):
// logs its name the first time it runs and answers "off".
#pragma once

#include <type_traits>

extern "C" void xhw_logf(const char* fmt, ...);

#define PX_STUB(name)                                                                              \
  do {                                                                                             \
    static bool px_stub_seen;                                                                      \
    if (!px_stub_seen) {                                                                           \
      px_stub_seen = true;                                                                         \
      xhw_logf("[STUB] %s", name);                                                                 \
    }                                                                                              \
  } while (0)

template < typename T >
struct PxStubValue {
  static T get() { return T{}; }
};
template < typename T >
struct PxStubValue< T& > {
  static T& get() {
    static std::remove_cv_t< T > value{};
    return value;
  }
};
template < typename T >
inline T px_stub_value() {
  return PxStubValue< T >::get();
}

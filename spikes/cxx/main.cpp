// Phase 0.2 spike: real game classes on nxdk's i386-pc-win32 (MSVC C++ ABI)
// with nxdk's libc++. Prints what works over COM1 and DbgPrint.
#include <hal/debug.h>
#include <hal/video.h>
#include <windows.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <map>
#include <mutex>
#include <chrono>

#include "Kyoto/Math/CVector3f.hpp"
#include "Kyoto/Math/CTransform4f.hpp"
#include "Kyoto/Streams/CMemoryInStream.hpp"
#include "rstl/vector.hpp"
#include "rstl/string.hpp"

static inline unsigned char inb(unsigned short p) { unsigned char v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outb(unsigned short p, unsigned char v) { __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(p)); }
static void com1(const char* s) {
  for (; *s; ++s) {
    if (*s == '\n') { while (!(inb(0x3FD) & 0x20)) {} outb(0x3F8, '\r'); }
    while (!(inb(0x3FD) & 0x20)) {}
    outb(0x3F8, (unsigned char)*s);
  }
}
static void say(const char* fmt, ...) {
  char buf[512];
  va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
  com1(buf); DbgPrint("%s", buf); debugPrint("%s", buf);
}

extern "C" void px_throw_c(const char* f, int l) { say("[SPIKE] throw at %s:%d\n", f, l); for (;;) Sleep(1000); }
[[noreturn]] void px_throw(const char* f, int l) { px_throw_c(f, l); for (;;) {} }

static int s_ctor_ran;
struct StaticCtor { StaticCtor() { s_ctor_ran = 0x1234; } };
static StaticCtor s_static_ctor;
static std::vector< int > s_vec = {1, 2, 3};
static const CVector3f s_up(0.f, 0.f, 1.f);

extern int g_check_pass, g_check_total;

struct Base { virtual ~Base() {} virtual int f() const { return 1; } };
struct Derived : Base { int f() const override { return 42; } };

int main() {
  XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);
  say("[SPIKE] Prime-X C++ spike: i386-pc-win32, nxdk libc++\n");
  say("[SPIKE] static ctor %s (0x%x), std::vector static %s (%u)\n", s_ctor_ran == 0x1234 ? "ok" : "FAIL",
      s_ctor_ran, s_vec.size() == 3 ? "ok" : "FAIL", (unsigned)s_vec.size());
  say("[SPIKE] game static const CVector3f %s (%d %d %d)\n", s_up.GetZ() == 1.f ? "ok" : "FAIL",
      (int)s_up.GetX(), (int)s_up.GetY(), (int)s_up.GetZ());
  Base* b = new Derived;
  say("[SPIKE] virtual call + new: %s\n", b->f() == 42 ? "ok" : "FAIL");
  delete b;

  // big-endian disc data through the game's own stream
  static const unsigned char be[] = {0x12, 0x34, 0x56, 0x78, 0x3f, 0x80, 0x00, 0x00, 0xAB, 0xCD};
  CMemoryInStream in(be, sizeof be);
  uint l = in.ReadLong();
  float f = in.ReadFloat();
  ushort s = in.ReadShort();
  say("[SPIKE] CMemoryInStream BE reads: %s (%08x %d %04x)\n",
      l == 0x12345678 && f == 1.f && s == 0xABCD ? "ok" : "FAIL", l, (int)f, s);

  CTransform4f xf = CTransform4f::Translate(1.f, 2.f, 3.f);
  CVector3f p = xf * CVector3f(1.f, 1.f, 1.f);
  say("[SPIKE] CTransform4f * CVector3f: %s (%d %d %d)\n",
      p.GetX() == 2.f && p.GetY() == 3.f && p.GetZ() == 4.f ? "ok" : "FAIL",
      (int)p.GetX(), (int)p.GetY(), (int)p.GetZ());

  rstl::vector< int > rv;
  for (int i = 0; i < 100; ++i) rv.push_back(i);
  rstl::string rs("prime");
  say("[SPIKE] rstl::vector %s, rstl::string %s\n", rv.size() == 100 && rv[99] == 99 ? "ok" : "FAIL",
      rs.size() == 5 ? "ok" : "FAIL");

  std::map< std::string, int > m; m["samus"] = 1;
  std::recursive_mutex mu; { std::lock_guard< std::recursive_mutex > g(mu); m["ridley"] = 2; }
  auto t0 = std::chrono::steady_clock::now(); Sleep(20);
  auto ms = std::chrono::duration_cast< std::chrono::milliseconds >(std::chrono::steady_clock::now() - t0).count();
  say("[SPIKE] std::map/string %s, recursive_mutex ok, chrono %s (%d ms)\n", m.size() == 2 ? "ok" : "FAIL",
      ms >= 15 && ms < 200 ? "ok" : "FAIL", (int)ms);
  say("[SPIKE] CHECK_SIZEOF on this triple: %d / %d pass (layout build, docs/decisions.md)\n", g_check_pass,
      g_check_total);
  say("[SPIKE] done\n");
  for (;;) Sleep(1000);
}

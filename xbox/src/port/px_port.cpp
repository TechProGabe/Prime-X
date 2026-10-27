// px_port.cpp - the game-side pieces of the Xbox boot and frame that are not
// a Port* hook: the aspect the video mode asks for, the dropped Remastered
// movement entry points, and C++ runtime gaps.
#include "port_debug.h"

#include "MetroidPrime/CGameCollision.hpp"

#include <filesystem>

#include "xhw.h"

extern "C" {
// boot.c, after the video mode is set: 16:9 at 720p always, at 480 when the
// dashboard is set to widescreen (docs/architecture.md "Video")
void px_port_boot() {
  const xhw_video_mode* vm = xhw_video();
  PortDebug::SetAspectMode(vm->widescreen ? PortDebug::kAspect_16_9 : PortDebug::kAspect_4_3);
  xhw_logf("[VIDEO] game aspect %s", vm->widescreen ? "16:9" : "4:3");
}

// vi.c, every frame boundary: main.cpp's ApplyAspectMode already follows
// PortDebug::AspectMode() each frame
void xsdk_widescreen_update() {}
}

// Remastered's movement (PortDebug::RemasteredMovement(), always off here)
void CGameCollision::MoveAirborneStepped(CStateManager&, CPlayer&, float,
                                         const rstl::reserved_vector< TUniqueId, 1024 >*) {
  xhw_fatal("Game error", "Remastered movement is not built for the Xbox");
}
void CGameCollision::MoveGroundedRemastered(CStateManager&, CPlayer&, float,
                                            const rstl::reserved_vector< TUniqueId, 1024 >*) {
  xhw_fatal("Game error", "Remastered movement is not built for the Xbox");
}

// std::filesystem: port headers name it; the Xbox build never creates folders
// through it (the save folder is made at boot, xhw_main.c)
namespace std {
inline namespace __1 {
namespace __fs {
namespace filesystem {
bool __create_directories(const path&, error_code* ec) {
  if (ec)
    ec->clear();
  return false;
}
} // namespace filesystem
} // namespace __fs
} // namespace __1
} // namespace std

// Boot trace: COM1 lines around the C++ static constructors (.CRT$XCU, run
// by nxdk's CRT before main), so a fault in one is placed before or after.
extern "C" void xhw_com1_raw(const char* s, size_t n);
static void px_crt_begin() { xhw_com1_raw("[CRT] static constructors\n", 26); }
static void px_crt_end() { xhw_com1_raw("[CRT] static constructors done\n", 31); }
#pragma section(".CRT$XCB", read)
#pragma section(".CRT$XCY", read)
__declspec(allocate(".CRT$XCB")) void (*px_crt_begin_ptr)() = px_crt_begin;
__declspec(allocate(".CRT$XCY")) void (*px_crt_end_ptr)() = px_crt_end;

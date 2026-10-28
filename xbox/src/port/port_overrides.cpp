// port_overrides.cpp - dropped Port* hooks whose "off" is not zero, so the
// generated stubs (tools/xbox/gen_port_stubs.py) can't stand in for them.
// Each name here is listed in stub_exclude.txt.
#include "port_fx_debug.h"
#include "port_mods.h"
#include "port_pointer_menu.h"

namespace PortFx {
float gTimeScale = 1.f; // 1 = off (port_fx_debug.h): particles at the game's own speed
} // namespace PortFx

// the game's menus call this every frame they are up (pointer support on
// the PC): on the Xbox it tells the pad that a menu is on screen, so B is
// back there (xbox/src/sdk/pad.c)
extern "C" void xsdk_pad_menu_seen(void);
namespace PortPointerMenu {
bool Begin() {
  xsdk_pad_menu_seen();
  return false;
}
} // namespace PortPointerMenu

namespace PortMods {
// no mod gives an effect its own id: the retail asset's id
uint32_t RemapEffectId(uint32_t, uint32_t, uint32_t id) { return id; }
} // namespace PortMods

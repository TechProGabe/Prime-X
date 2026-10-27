// port_overrides.cpp - dropped Port* hooks whose "off" is not zero, so the
// generated stubs (tools/xbox/gen_port_stubs.py) can't stand in for them.
// Each name here is listed in stub_exclude.txt.
#include "port_fx_debug.h"
#include "port_mods.h"

namespace PortFx {
float gTimeScale = 1.f; // 1 = off (port_fx_debug.h): particles at the game's own speed
} // namespace PortFx

namespace PortMods {
// no mod gives an effect its own id: the retail asset's id
uint32_t RemapEffectId(uint32_t, uint32_t, uint32_t id) { return id; }
} // namespace PortMods

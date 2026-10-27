// port_gci.cpp - PortGci (port_gci.h) on the Xbox: the USA 1.00 card only.
// MetroidPrimePort's PortGci finds Dolphin's card folders, migrates PAL
// saves and offers other releases' saves; none of that applies here. The
// memory card itself is xbox/src/sdk/card.c (E:\UDATA\50580001\).
#include "port_gci.h"

namespace PortGci {

const char* GameCode() { return "GM8E"; }
std::string CardTargetDirectory(const std::string&) { return std::string(); }
bool RemountIfChanged() { return false; }
bool SyncTrilogyShare(bool) { return false; }
Report MigrateMountedLegacyPal() { return Report(); }
std::vector< OtherRelease > RestoreToOffer() { return std::vector< OtherRelease >(); }
std::string Report::Summary(const char* verb) const {
  return std::string(verb ? verb : "") + " " + std::to_string(copied);
}

} // namespace PortGci

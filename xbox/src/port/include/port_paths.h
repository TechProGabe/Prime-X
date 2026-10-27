// Prime-X twin of MetroidPrimePort's port_paths.h (the user folder found
// through SDL and std::filesystem). On the Xbox everything the game keeps lives
// in the title's save folder, E:\UDATA\50580001\, the memory card included
// (docs/decisions.md "Title ID").
#pragma once

#include <string>

namespace PortPaths {

inline const std::string& UserFolder() {
  static const std::string kFolder = "E:\\UDATA\\50580001\\";
  return kFolder;
}

inline std::string CardFolder() { return UserFolder(); }

} // namespace PortPaths

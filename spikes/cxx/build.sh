#!/usr/bin/env bash
# spikes/cxx/build.sh: configure + build the spike XBE with MSYS2 (Git Bash entry)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
exec /c/msys64/usr/bin/bash -lc '
  set -euo pipefail
  export PATH=/usr/bin:/mingw64/bin:$PATH NXDK_DIR=/c/xdev/nxdk
  eval "$($NXDK_DIR/bin/activate -s)"
  cd "$1"
  cmake -S . -B ../../build-spike/xbe -G Ninja -DCMAKE_TOOLCHAIN_FILE=$NXDK_DIR/share/toolchain-nxdk.cmake >/dev/null
  ninja -C ../../build-spike/xbe' _ "$here"

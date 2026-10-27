#!/usr/bin/env bash
# Build default.xbe -> build-xbox/xbe/default.xbe
# Needs the toolchain from tools/xbox/setup.sh (NXDK_DIR, LLVM).
#   XBOX_CFLAGS   extra C/C++ flags for every unit, e.g. "-DXHW_FBDUMP_EVERY=600";
#                 reset on every run, so a debug knob never sticks in the cache
#   XBOX_CMAKE_ARGS, XBOX_NINJA_ARGS   passed through (-DPX_GAME_ONLY=ON: objects only)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
export NXDK_DIR="${NXDK_DIR:-/opt/nxdk}"
export LLVM="${LLVM:-/opt/llvm21}"
export PATH="$LLVM/bin:$PATH"
eval "$("$NXDK_DIR/bin/activate" -s)"
cmake -S "$root" -B "$root/build-xbox" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NXDK_DIR/share/toolchain-nxdk.cmake" \
  -DCMAKE_C_FLAGS="${XBOX_CFLAGS:-}" -DCMAKE_CXX_FLAGS="${XBOX_CFLAGS:-}" \
  ${XBOX_CMAKE_ARGS:-} >/dev/null
ninja -C "$root/build-xbox" ${XBOX_NINJA_ARGS:-}
[ -f "$root/build-xbox/xbe/default.xbe" ] && ls -la "$root/build-xbox/xbe/default.xbe" || true

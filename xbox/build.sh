#!/usr/bin/env bash
# Build default.xbe -> build-xbox/xbe/default.xbe
# Needs the toolchain from tools/xbox/setup.sh (NXDK_DIR, LLVM).
#   XBOX_CFLAGS   extra C/C++ flags for every unit, e.g. "-DXHW_FBDUMP_EVERY=600";
#                 reset on every run, so a debug knob never sticks in the cache
#   XBOX_CMAKE_ARGS, XBOX_NINJA_ARGS   passed through (-DPX_GAME_ONLY=ON: objects only)
#   XBOX_LTO=1    ThinLTO over the game, port and sdk code (docs/performance.md)
#   XBOX_PGO=gen  instrumented for PGO; XBOX_PGO=<file.profdata> optimized with it (docs/pgo.md)
# Each LTO/PGO kind builds in its own folder (tools/xbox/build_dir.sh:
# build-xbox-lto, -pgogen, -lto-pgo, ...), so switching never recompiles
# another kind's objects.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
bdir="$root/$("$root/tools/xbox/build_dir.sh")"
export NXDK_DIR="${NXDK_DIR:-/opt/nxdk}"
export LLVM="${LLVM:-/opt/llvm21}"
export PATH="$LLVM/bin:$PATH"
eval "$("$NXDK_DIR/bin/activate" -s)"
pgo="${XBOX_PGO:-}"
case "$pgo" in ""|gen|/*|?:*) ;; *) pgo="$root/$pgo" ;; esac   # a profile path relative to the checkout
cmake -S "$root" -B "$bdir" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NXDK_DIR/share/toolchain-nxdk.cmake" \
  -DCMAKE_C_FLAGS="${XBOX_CFLAGS:-}" -DCMAKE_CXX_FLAGS="${XBOX_CFLAGS:-}" \
  -DPX_LTO="$([ -n "${XBOX_LTO:-}" ] && echo ON || echo OFF)" -DPX_PGO="$pgo" \
  ${XBOX_CMAKE_ARGS:-} >/dev/null
ninja -C "$bdir" ${XBOX_NINJA_ARGS:-}
[ -f "$bdir/xbe/default.xbe" ] && ls -la "$bdir/xbe/default.xbe" || true

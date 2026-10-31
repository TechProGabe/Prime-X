#!/usr/bin/env bash
# Build default.xbe inside the prime-x:sdk image (macOS, Linux, Windows Git Bash).
#   docker build -t prime-x:sdk tools/xbox/docker      # once
#   tools/xbox/docker/build.sh                          # -> build-xbox/xbe/default.xbe
#   XBOX_LTO=1 XBOX_PGO=xbox/prime.profdata tools/xbox/docker/build.sh   # a release build (build-xbox-lto-pgo)
# XBOX_CFLAGS / XBOX_CMAKE_ARGS / XBOX_NINJA_ARGS / XBOX_LTO / XBOX_PGO pass through.
set -euo pipefail
root="$(cd "$(dirname "$0")/../../.." && pwd)"
vroot="$root"
# Git Bash on Windows: no MSYS path rewriting of /src etc., and a C:/ path for -v
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) export MSYS_NO_PATHCONV=1; vroot="$(cd "$root" && pwd -W)";; esac
tty=; [ -t 1 ] && tty=-t
docker run --rm $tty -v "$vroot":/src -w /src \
  -e XBOX_CFLAGS -e XBOX_CMAKE_ARGS -e XBOX_NINJA_ARGS -e XBOX_LTO -e XBOX_PGO \
  prime-x:sdk bash -c '
  set -e
  export NXDK_DIR=/usr/src/nxdk
  xbox/build.sh'
# Dashboard icon ($$XTIMAGE section and default.tbn, from xbox/assets/logo.png)
# and the boot screen's music appended to the XBE (tools/xbox/make_intro.py).
# Host python3 + Pillow for the first, ffmpeg for the second: each is
# skipped without them (or the icon with XBOX_NO_ICON).
bdir="$root/$(XBOX_LTO="${XBOX_LTO:-}" XBOX_PGO="${XBOX_PGO:-}" "$root/tools/xbox/build_dir.sh")"
xbe="$bdir/xbe/default.xbe"
if [ -z "${XBOX_NO_ICON:-}" ] && [ -f "$xbe" ] && python3 -c "import PIL" 2>/dev/null; then
  python3 "$root/tools/xbox/xbe_title_image.py" "$xbe" "$root/xbox/assets/logo.png" "$bdir/xbe/default.tbn"
fi
[ -f "$xbe" ] && python3 "$root/tools/xbox/make_intro.py" "$xbe" || true

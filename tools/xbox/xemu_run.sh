#!/usr/bin/env bash
# Boot a build in xemu (macOS, or Windows from Git Bash) and capture COM1.
#   tools/xbox/xemu_run.sh [seconds] [stop-regex]
# Env:
#   PX_ISO    your own GM8E01 (USA 1.00) image to pack next to default.xbe ("none" = omit,
#             to exercise the missing-disc screen). Never committed.
#   PX_XBE    default: build-xbox/xbe/default.xbe
#   PX_RUN    work dir (XISO + logs), default ~/xemu/px-run
#   PX_GUI=1  leave xemu running (don't kill at timeout)
#   PX_STAGE_EXTRA  dir whose contents are also packed onto the disc
#   PX_XEMU_ARGS    extra xemu arguments (e.g. -config_path <xemu.toml>)
#   PX_XISO   a native extract-xiso instead of the docker image's (Windows
#             without Docker: /c/xdev/nxdk/tools/extract-xiso/build/extract-xiso.exe)
#   PX_XEMU   the xemu binary (default: macOS's /Applications/Xemu.app; on
#             Windows e.g. /c/xemu/xemu.exe)
#   PX_SHOTS  stop once this many [FBDUMP] screenshots are complete (default:
#             the number of SHOT lines in $PX_STAGE_EXTRA/autopad.txt; 0 = off,
#             run until the stop regex or the timeout)
# xemu needs your own MCPX ROM, BIOS and HDD image, set to 64 MB.
set -euo pipefail
# Git Bash: keep MSYS from rewriting /run, /usr/... into Windows paths for docker
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) export MSYS_NO_PATHCONV=1; win=1;; *) win=0;; esac
root="$(cd "$(dirname "$0")/../.." && pwd)"
secs="${1:-60}"; stop="${2:-__never__}"
xbe="${PX_XBE:-$root/build-xbox/xbe/default.xbe}"
run="${PX_RUN:-$HOME/xemu/px-run}"
iso="${PX_ISO:-}"
mkdir -p "$run/stage"
rm -rf "$run/stage/"*
cp "$xbe" "$run/stage/default.xbe"
[ -n "${PX_STAGE_EXTRA:-}" ] && cp -R "$PX_STAGE_EXTRA"/. "$run/stage/"
if [ -n "$iso" ] && [ "$iso" != none ]; then ln -f "$iso" "$run/stage/$(basename "$iso")" 2>/dev/null || cp "$iso" "$run/stage/"; fi
rm -f "$run/game.xiso"
vrun="$run"; [ "$win" = 1 ] && vrun="$(cd "$run" && pwd -W)"
if [ -n "${PX_XISO:-}" ]; then
  (cd "$run" && "$PX_XISO" -c stage game.xiso >/dev/null)
else
  docker run --rm -v "$vrun":/run prime-x:sdk \
    /usr/src/nxdk/tools/extract-xiso/build/extract-xiso -c /run/stage /run/game.xiso >/dev/null
fi
log="$run/serial.log"; : > "$log"
xemu="${PX_XEMU:-/Applications/Xemu.app/Contents/MacOS/xemu}"
xrun="$run"; [ "$win" = 1 ] && xrun="$(cd "$run" && pwd -W)"
"$xemu" -dvd_path "$xrun/game.xiso" \
  -device lpc47m157 -serial "file:$xrun/serial.log" ${PX_XEMU_ARGS:-} > "$run/xemu.out" 2>&1 &
pid=$!
shots="${PX_SHOTS:-}"
if [ -z "$shots" ]; then
  shots=0
  ap="${PX_STAGE_EXTRA:-}/autopad.txt"
  [ -n "${PX_STAGE_EXTRA:-}" ] && [ -f "$ap" ] && shots=$(grep -cE '^[0-9]+[[:space:]]+SHOT' "$ap" || true)
fi
for ((i=0; i<secs; i++)); do
  sleep 1
  kill -0 $pid 2>/dev/null || break
  # screenshots go out as base64 lines, which can spell a stop word by chance
  grep -av '^\[FBDUMP\]' "$log" 2>/dev/null | grep -qE "$stop" && break
  # the last screenshot is out: the rest of the scenario (results screen) is idle time
  [ "$shots" -gt 0 ] && [ "$(tr -d '\r' < "$log" | grep -cx '\[FBDUMP\] END')" -ge "$shots" ] && break
done
[ "${PX_GUI:-0}" = 1 ] || { kill $pid 2>/dev/null || true; wait $pid 2>/dev/null || true; }
echo "--- serial ($i s) ---"; tr -d '\r' < "$log"

#!/usr/bin/env bash
# A rendering sweep across the game: boot each room in xemu straight into
# play (MP_BOOT_WORLD), two shots each, two xemus at a time, and list any
# FATAL, THROW, texture drop or out-of-memory line (docs/testing.md
# "Room sweep"). Run from the checkout with xemu_run.sh's environment and
# a second slot (PX_RUN_2, PX_XEMU_ARGS_2: /c/xemu/px/env.sh):
#
#   tools/xbox/room_sweep.sh [default.xbe]     # default: build-xbox/xbe/default.xbe
#
# Shots go to build-xbox/shots/sweep/<room>_NN.png. ~40 minutes.
set -u
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
XBE="${1:-$root/build-xbox/xbe/default.xbe}"
# only an autopad build reads the script that boots each room (-DXHW_AUTOPAD=1)
grep -aqF 'D:\autopad.txt' "$XBE" || { echo "room_sweep: $XBE has no autopad (build with XBOX_CFLAGS=-DXHW_AUTOPAD=1)"; exit 1; }
OUT=build-xbox/shots/sweep
rm -rf "$OUT"; mkdir -p "$OUT"
work="$root/build-xbox/sweep"; mkdir -p "$work"
cp "$XBE" "$work/default.xbe"
rooms=(
  "landing 39F2DE28"
  "frigcrash 39F2DE28:B9ABCD56"
  "temple 39F2DE28:2398E906"
  "plaza 83F6FF6F:D5CDB809"
  "shrine 83F6FF6F:DF746AE0"
  "sunlobby 83F6FF6F:3D238FCD"
  "shorelines A8BE6291:F7285979"
  "iceruins A8BE6291:B33A0620"
  "icetemple A8BE6291:6655F51E"
  "lavalake 3EF8237C:A4719C6A"
  "twinfires 3EF8237C:E4A4462E"
  "quarry B1AC4D65:643D038F"
  "phazontunnel B1AC4D65:BBFA4AB3"
  "crater C13B09D1:93668996"
)
one() {   # name world run xemu-args
  local name=$1 world=$2 run=$3 args=$4 st="$work/$1"
  mkdir -p "$st"
  printf 'env PX_DISC_CHECK=quick\nenv MP_BOOT_WORLD=%s\n1900 SHOT\n1950 SL for 50\n2100 SHOT\n' "$world" > "$st/autopad.txt"
  PX_XBE="$work/default.xbe" PX_RUN="$run" PX_XEMU_ARGS="$args" PX_STAGE_EXTRA="$st" \
    timeout 1300 tools/xbox/xemu_run.sh 1200 'FATAL|THROW' > "$OUT/$name.out" 2>&1
  python tools/xbox/fbdump_to_png.py "$run/serial.log" "$OUT/$name" > /dev/null 2>&1
  grep -a "FATAL\|THROW\|\[TEX\] drop\|NEW\] out" "$OUT/$name.out" | head -3 | sed "s/^/$name: /"
}
i=0
while [ $i -lt ${#rooms[@]} ]; do
  set -- ${rooms[$i]}; one "$1" "$2" "${PX_RUN:?}" "${PX_XEMU_ARGS:-}" &
  i=$((i + 1))
  if [ $i -lt ${#rooms[@]} ] && [ -n "${PX_RUN_2:-}" ]; then
    set -- ${rooms[$i]}; one "$1" "$2" "$PX_RUN_2" "${PX_XEMU_ARGS_2:-}" &
    i=$((i + 1))
  fi
  wait
done
echo "room_sweep: $(ls "$OUT"/*.png 2>/dev/null | wc -l) shots in $OUT"

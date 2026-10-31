#!/usr/bin/env bash
# The build folder for this kind of build, relative to the checkout
# (xbox/build.sh, tools/xbox/msys/build.sh): build-xbox, then -lto, then
# -pgogen or -pgo, from XBOX_LTO and XBOX_PGO.
d=build-xbox
[ -n "${XBOX_LTO:-}" ] && d=$d-lto
case "${XBOX_PGO:-}" in "") ;; gen) d=$d-pgogen ;; *) d=$d-pgo ;; esac
echo "$d"

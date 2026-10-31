#!/usr/bin/env bash
# Regenerate xbox/prime.profdata and xbox/order.txt (docs/pgo.md): build the instrumented image
# (XBOX_PGO=gen), play the training scenarios in xemu, turn each run's last
# [PGOC] dump into a raw profile, merge, and write xbox/prime.profdata.txt
# (where and from what it came). Run from the checkout with xemu_run.sh's
# environment (PX_ISO, PX_XEMU, PX_XISO, PX_RUN, PX_XEMU_ARGS):
#
#   tools/xbox/pgo_train.sh [scenario...]     # names under tools/xbox/scenarios/pgo
#
# Up to three xemus at once: slot 1 is PX_RUN/PX_XEMU_ARGS, slots 2 and 3
# PX_RUN_2/PX_XEMU_ARGS_2 and PX_RUN_3/PX_XEMU_ARGS_3 when set (each its own
# run folder and xemu config with its own hard disk image). Branch counts
# don't depend on xemu's speed, so the profile is the console's too.
# PX_PGO_SKIP_BUILD=1 reuses build-xbox-pgogen as it is.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
scen=("$@")
# chozo (Main Plaza) runs out of memory instrumented: xemu gives this setup 64 MB
[ ${#scen[@]} -gt 0 ] || scen=(tallon frigate phendrana magmoor)
py=python3; python3 -c '' >/dev/null 2>&1 || py=python

[ -n "${PX_PGO_SKIP_BUILD:-}" ] || XBOX_PGO=gen XBOX_CFLAGS=-DXHW_AUTOPAD=1 tools/xbox/msys/build.sh
work="$root/build-xbox-pgogen/train"; rm -rf "$work"; mkdir -p "$work"
cp build-xbox-pgogen/xbe/default.xbe build-xbox-pgogen/prime_x.exe build-xbox-pgogen/prime_x.map "$work/"

runs=("${PX_RUN:?set PX_RUN}"); args=("${PX_XEMU_ARGS:-}")
[ -n "${PX_RUN_2:-}" ] && { runs+=("$PX_RUN_2"); args+=("${PX_XEMU_ARGS_2:-}"); }
[ -n "${PX_RUN_3:-}" ] && { runs+=("$PX_RUN_3"); args+=("${PX_XEMU_ARGS_3:-}"); }

one() {   # scenario slot
  local s=$1 k=$2
  PX_XBE="$work/default.xbe" PX_RUN="${runs[$k]}" PX_XEMU_ARGS="${args[$k]}" PX_SHOTS=0 \
    PX_STAGE_EXTRA="tools/xbox/scenarios/pgo/$s" \
    tools/xbox/xemu_run.sh 3000 '\[PGOC\] end 2:|FATAL|THROW' > "$work/$s.out" 2>&1 || true
  tr -d '\r' < "${runs[$k]}/serial.log" > "$work/$s.log"
  $py tools/xbox/pgo_raw.py --exe "$work/prime_x.exe" --map "$work/prime_x.map" "$work/$s.log" -o "$work/$s.profraw" ||
    echo "pgo_train: $s gave no profile (see $work/$s.log)"
}
i=0
while [ $i -lt ${#scen[@]} ]; do
  for ((k=0; k<${#runs[@]} && i<${#scen[@]}; k++, i++)); do one "${scen[$i]}" $k & done
  wait
done

prof=(); for s in "${scen[@]}"; do [ -f "$work/$s.profraw" ] && prof+=("$work/$s.profraw"); done
[ ${#prof[@]} -gt 0 ] || { echo "pgo_train: no profiles"; exit 1; }
pd=llvm-profdata; command -v $pd >/dev/null 2>&1 || pd=/c/msys64/mingw64/bin/llvm-profdata
"$pd" merge -o xbox/prime.profdata "${prof[@]}"
# the link order from the same counts (docs/pgo.md "Link order")
LLVM_PROFDATA="$pd" $py tools/xbox/make_order.py xbox/prime.profdata --map "$work/prime_x.map" -o xbox/order.txt
{
  echo "xbox/prime.profdata: PGO profile for XBOX_PGO=xbox/prime.profdata builds (docs/pgo.md)"
  echo "trained on: $(git rev-parse --short HEAD)$(git diff --quiet || echo ' + local changes') ($(git log -1 --format=%cs))"
  echo "scenarios: ${scen[*]} (tools/xbox/scenarios/pgo, xemu)"
  echo "llvm: $("$pd" --version 2>/dev/null | grep -io 'llvm version [0-9.]*' | head -1)"
  "$pd" show xbox/prime.profdata 2>/dev/null | grep -E '^(Total functions|Maximum function count|Total count)'
} > xbox/prime.profdata.txt
cat xbox/prime.profdata.txt

#!/usr/bin/env bash
# Clone the reference repositories Prime-X is built from into ref/ (gitignored).
#
#   tools/fetch_refs.sh            # clone what's missing
#   tools/fetch_refs.sh --update   # also fast-forward existing clones
#
# ref/ is read-only material: code is imported from it into src/, extern/ and
# xbox/ by hand (docs/sources.md says what comes from where and at which
# commit). Nothing in the build reads ref/.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
ref="$root/ref"
mkdir -p "$ref"

update=0
[ "${1:-}" = "--update" ] && update=1

# name  url  pinned commit (empty = default branch head)
repos=(
  "MetroidPrimePort https://github.com/Odrannnn/MetroidPrimePort.git 673eacaf766fe5f88f3364664fc784e8899a2b2a"
  "Melee-X https://github.com/TechProGabe/Melee-X.git"
  "OpenCrossing-Xbox https://github.com/TechProGabe/OpenCrossing-Xbox.git"
  "aurora https://github.com/encounter/aurora.git"
  "nxdk https://github.com/XboxDev/nxdk.git"
)

for line in "${repos[@]}"; do
  read -r name url pin <<<"$line"
  dir="$ref/$name"
  if [ ! -d "$dir/.git" ]; then
    echo "== clone $name"
    git clone --filter=blob:none "$url" "$dir"
  elif [ $update -eq 1 ]; then
    echo "== update $name"
    git -C "$dir" fetch --quiet origin
    git -C "$dir" merge --ff-only --quiet "@{u}" || echo "   (not fast-forward, left as is)"
  fi
  if [ -n "${pin:-}" ]; then
    git -C "$dir" cat-file -e "$pin^{commit}" 2>/dev/null || git -C "$dir" fetch --quiet origin "$pin"
    echo "   $name pinned at ${pin:0:12} (docs/sources.md); check it out with: git -C ref/$name checkout $pin"
  fi
done

# PrimeDecomp/prime itself: GitHub returned 404 and git.axiodl.com 502 on
# 2026-10-27. MetroidPrimePort carries the decomp (src/, include/, decomp/), so
# it is not needed; try again when an upstream fix has to be compared.
echo "done: $ref"

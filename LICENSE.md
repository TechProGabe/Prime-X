# Licensing

This repository mixes code under different terms. Read all of it before
redistributing any part. (Planned layout: the directories below are filled
in phase 1 of `docs/plan.md`; keep this file in step when they land.)

## 1. Game code: not licensed

`src/` and `include/` will be a decompilation of Metroid Prime from
[PrimeDecomp/prime](https://github.com/PrimeDecomp/prime), as adapted by
[MetroidPrimePort](https://github.com/Odrannnn/MetroidPrimePort)
(divergences tagged `#ifdef TARGET_PC` there and `PORT_XBOX:` here). The same
goes for any decompiled Dolphin SDK, MSL or CodeWarrior runtime code imported
from MetroidPrimePort's `extern/sdk/`. Metroid Prime is copyright Nintendo /
Retro Studios. The upstream decompilation publishes no license for the game
code and this repository cannot grant one. No permission to copy, modify or
redistribute this code is offered or implied.

No game assets are in this repository. Everything the game draws or plays is
read at runtime from a disc image the user supplies.

## 2. Port code: GPL-3.0-or-later

The Xbox layer (`xbox/`, `tools/xbox/`) is carried over from
[Melee-X](https://github.com/TechProGabe/Melee-X), whose Xbox layer is
GPL-3.0-or-later, and is released under the same terms (`COPYING`, added with
the first import). Parts of it come from
[OpenCrossing-Xbox](https://github.com/TechProGabe/OpenCrossing-Xbox) (MIT),
whose terms are compatible.

Code taken from MetroidPrimePort's own work (`platform/`, `tools/`, `tests/`,
MIT, see its `LICENSE` and `NOTICE`) keeps its MIT notice where it lands.

## 3. Third-party code

- `extern/aurora/include`: aurora's public Dolphin SDK headers, MIT.
- `extern/musyx`: MusyX, MIT (as vendored by MetroidPrimePort).
- nxdk is not vendored; it is fetched at build time under its own licenses.

## 4. Art

`docs/logo.svg` and `xbox/assets/logo.png` (made by
`tools/xbox/make_logo.py`) are drawn from scratch for this project: no game
or console art.

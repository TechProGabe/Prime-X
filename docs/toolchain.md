# Toolchain

The same toolchain as Melee-X (its `docs/toolchain.md` is the long version).
Phase 0.1 copies its scripts here; until then, build Melee-X once to prove
the machine is set up.

## Setup (once)

- **Docker**: Melee-X's `tools/xbox/docker/Dockerfile` (nxdk at
  `58427c07`, built with `NXDK_ONLY=y NXDK_SDL=y NXDK_CXX=y`, LLVM 21.1.8).
  Prime-X drops the `disc_lower` layers (no `DISC_STRUCT` lowering here).
- **Windows, native (MSYS2)**: MSYS2 at `C:\msys64` with `make git bison
  flex cmake ninja` and the `mingw-w64-x86_64-` `clang lld llvm python
  python-pillow gcc` packages; nxdk at the same commit in `C:\xdev\nxdk`,
  built once with `make tools` and `make NXDK_ONLY=y NXDK_SDL=y NXDK_CXX=y`.
  Build from Git Bash with `tools/xbox/msys/build.sh`. Melee-X's
  `docs/handoff.md` "Windows setup" has this PC's exact paths.
- **xemu**: the Windows release, with `PX_XEMU=<path to xemu.exe>`, a
  Prime-X `PX_RUN` folder and `PX_ISO` pointing at the user's USA 1.00 image
  (outside the repo).
- **Reference repos**: `tools/fetch_refs.sh`.

## Game code

To be written after phase 0.2: the triple, C++ flags (`-std=c++20` or what
the decomp needs, `-fno-exceptions -fno-rtti`, `-fsigned-char` is default on
x86, `-march=pentium3`), the force-included `prime_compat.h`, defines
(`TARGET_PC`, `TARGET_XBOX`, `VERSION=0`, which is `VERSION_GM8E_00` in `include/GameVersions.h`), MusyX's C23, and
how `compile_game.py` lists the sources.

## Platform code

As Melee-X: nxdk's `i386-pc-win32`, C, `xbox/CMakeLists.txt`.

## Release

As Melee-X (phase 7): the GitHub Actions `build` workflow on a release tag,
ThinLTO + PGO, `package_release.py` refusing a build without them.

## Icon and logo

- `docs/logo.svg`: the README logo, hand-written SVG (no font files).
- `tools/xbox/make_logo.py <out.png> [size]` draws `xbox/assets/logo.png`
  (Pillow). `xbe_title_image.py` (from Melee-X, phase 1) turns it into the
  XBE's `$$XTIMAGE`, `default.tbn` and `TitleImage.xbx`.

# Sources

Everything Prime-X is built from, where it lives, the commit it's taken at,
and what's taken. `tools/fetch_refs.sh` clones them into `ref/`
(gitignored). Code is imported into this tree by hand at the commits below.
When syncing, change the commit here and in `src/UPSTREAM_COMMIT` in the
same commit as the code.

| repo | URL | commit (2026-10-27) | license | role |
|---|---|---|---|---|
| MetroidPrimePort | <https://github.com/Odrannnn/MetroidPrimePort> (branch `port`) | `673eacaf766fe5f88f3364664fc784e8899a2b2a` (v0.34.0 era) | MIT over its own work; game code not licensed (`NOTICE`) | **the game code**: PrimeDecomp's `src/`, `include/` with the little-endian `TARGET_PC` fixes; `extern/musyx`; `extern/aurora/include`; THP and SDK pieces from `extern/sdk`; reference for the `Port*` hooks, disc verification table, save format, aspect and HUD fixes, cutscene skip |
| Melee-X | <https://github.com/TechProGabe/Melee-X> (branch `dev`) | `9860e6e8650fa2ea27718c5abcc16685b5bf3699` (v4.2) | Xbox layer GPL-3.0-or-later | **the Xbox layer**: `xbox/` (SDK, GX front end, NV2A back end, hardware), `tools/xbox/`, the build (Docker, MSYS2, CI), docs' house style, splash, settings file, logs, crash and hang reporters, code repair, 128 MB hold |
| OpenCrossing-Xbox | <https://github.com/TechProGabe/OpenCrossing-Xbox> (branch `main`) | `e6be90876f5acdf4f038ec847ebe6137d0ac20d0` | MIT | the in-game Options page to match (`xbox/src/xbox_settings_menu.c`), and where Melee-X's audio, crash and splash code first came from |
| aurora | <https://github.com/encounter/aurora> | headers only, as vendored by MetroidPrimePort (its fork `d39404c8`, upstream `011e33109` + hand-ported fixes to `8e356df6e`; `extern/README.md` there) | MIT | Dolphin SDK headers the game compiles against. Melee-X has an older snapshot (`77326d45`): diff them in phase 0.5 |
| nxdk | <https://github.com/XboxDev/nxdk> | `58427c078b4ccb0121f359b6b6c536b3b2914976` (Melee-X's and OpenCrossing's `NXDK_SHA`) | various | the Xbox SDK; built with `NXDK_SDL=y NXDK_CXX=y` |
| PrimeDecomp/prime | <https://github.com/PrimeDecomp/prime>, mirror <https://git.axiodl.com/PrimeDecomp/prime> | n/a | none for game code | upstream decomp. On 2026-10-27 GitHub answered 404 and the axiodl mirror 502. MetroidPrimePort carries it (its `decomp/` has the matching-build config for `GM8E01_00` and other revisions, and `TARGET_PC` off is the retail code) |

## What's taken from MetroidPrimePort

| path there | here | notes |
|---|---|---|
| `src/` (Collision, GuiSys, Kyoto, MetaRender, MetroidPrime, Weapons, WorldFormat, MetroTRK, NESemu) | `src/` | MetroTRK is the debugger stub: drop. NESemu (the NES Metroid unlocked with a GBA link): keep compiling, unreachable without a GBA |
| `include/` | `include/` | `static_assert.hpp`'s `CHECK_SIZEOF` turned on for the Xbox |
| `extern/musyx` | `extern/musyx` | C23 needed |
| `extern/aurora/include` | `extern/aurora/include` | headers only |
| `extern/sdk/dolphin/thp` and other pieces the game links | per phase 0.5 | decompiled SDK, not licensed (LICENSE.md) |
| `platform/compat.h` | `xbox/include/game/prime_compat.h` | force-included; Xbox twin |
| `platform/include/port_*.h` the game includes | `xbox/src/port/` | Xbox implementations (phase 0.4) |
| `platform/port_disc_verify_table.inc` | input to `tools/disc_file_table.py` | revision names and sizes for the error screens |
| `platform/port_skip_cutscenes*.cpp`, aspect, widescreen HUD, FOV | `xbox/src/port/` | the light extras |
| `decomp/config/GM8E01_00/symbols.txt` | `xbox/src/port/` (the two font addresses) | default fonts read from the disc's `main.dol` |

## What's not taken

`platform/` beyond the above (SDL, Dawn/WebGPU, ImGui, `nod`, Android,
iOS, Flatpak, the randomizer, Archipelago, Remastered and Dread imports,
mods, texture packs, savestates, third person, photo mode, Discord, the
debug console), `extern/aurora` beyond its headers, `extern/adrenotools`,
`extern/astcenc`, `extern/GamepadMotionHelpers`, `android/`, `flatpak/`,
`packaging/`, `textures/`, `assets/`.

# Architecture (planned)

## Layers

```
user's GM8E01 rev 0 disc image (.iso/.gcm/.ciso next to default.xbe)
        |
src/, include/                 game code: PrimeDecomp as adapted by MetroidPrimePort
                               (commit in src/UPSTREAM_COMMIT); C++; disc data parsed
                               big-endian through CInputStream, GPU data (display
                               lists, vertex arrays) left big-endian for GX
extern/musyx                   MusyX audio engine (C)
        |  Dolphin SDK calls (extern/aurora/include: GX OS DVD AR ARQ AI DSP PAD SI VI CARD MTX THP)
        |  Port* hooks (xbox/src/port: retail defaults, the light extras)
        v
xbox/src/sdk                   Dolphin SDK on the Xbox (Melee-X), game triple
xbox/src/hw                    kernel, pbkit (NV2A), AC97, USB pads (Melee-X), nxdk triple
        |
nxdk (pdclib, libc++, winapi, pbkit, SDL2 gamecontroller, xboxkrnl)
```

The same stack as Melee-X with the game swapped. Melee-X's `xbox/` is
game-agnostic except for a few files (`docs/reuse-map.md`).

## The C++ question

Melee-X compiles game code with `i686-pc-windows-gnu -mno-ms-bitfields`
(GameCube bit-field layout, 8-byte `long long` alignment, nxdk's calling
convention) and nxdk code with `i386-pc-win32`, and only plain C structs
(`xgx.h`, `xhw.h`) cross between them. That works because Melee is C.

Prime is C++: classes with vtables, templates (`rstl`), static
constructors, `new`/`delete`, and in MetroidPrimePort's `TARGET_PC` code some
`std::` (`vector`, `string`, `mutex`, `chrono`, `function`, `runtime_error`).
The gnu triple uses the Itanium C++ ABI; nxdk's libc++ is built for the
MSVC ABI. Phase 0.2 decides how to build it (`docs/plan.md`). Whatever
wins, these hold:

- the game/nxdk boundary stays C (`extern "C"` SDK and `xhw_*` calls,
  `xgx.h`/`xhw.h` structs), so the C++ ABI only has to be consistent inside
  the game;
- `-fno-exceptions -fno-rtti` unless the spike shows the game needs them;
  `TARGET_PC` code that throws is changed to log and fail;
- static constructors run before `main` (nxdk runs `.CRT$XC*`; check the
  game triple's `.ctors`/`.init_array` gets there too);
- the decomp's `CHECK_SIZEOF` asserts are on: with 32-bit pointers every
  game class should have its GameCube size, and an assert that fails is a
  layout difference to understand.

## Data model (32-bit)

The Xbox is 32-bit little-endian x86, the GameCube 32-bit big-endian
PowerPC. Pointers are the same size again, so MetroidPrimePort's 64-bit work
(pointer-width corrections, the 64-bit AI DMA accessor, larger native
structs) mostly falls away; its endianness work all stays. Plain `char` is
signed on x86 (MetroidPrimePort asserts it).

## Memory (64 MB): first estimate

To be measured in phase 2 and kept current, as Melee-X's table is.

| item | size | note |
|---|---|---|
| XBE image (game + MusyX + Xbox layer) | est. 8-10 MB | GameCube `main.dol` is ~4 MB of PowerPC; x86 C++ at `-O2` with ThinLTO is bigger. Measure in phase 1 |
| MEM1 (game heaps) | 32 MB reserved (the game told 24), committed on demand in 64 KB chunks, freed blocks' chunks given back | Prime's `CGameAllocator`, as on the GameCube; game `new` routed here (plan 0.6). Measured in xemu: ~20 MB live in the Landing Site, Main Plaza leaves 4.7 MB of the Xbox free at its lowest (2.6 MB with `new` in malloc) |
| ARAM | 16 MB reserved, committed on demand; ~4 MB committed in play | MusyX sample groups and the world PAKs' resource tables (`CPakFile`); disc-backed pages as Melee-X, the rest swapped to `aram.swp` in the save folder when memory runs low (`ar.c`) |
| NV2A framebuffers | 4.9 MB at 480 (32-bit x3 + Z), 9.1 MB at 720p (R5G6B5 x3 + Z24S8) | as Melee-X |
| NV2A texture pool | 6 MB at 720p, 7 MB at 480 (8 until round hw6 ran out of memory) | Prime's textures are already in MEM1 (`CTexture`); the pool is a second copy in NV2A formats |
| pushbuffer + vertex ring | 2.5 MB | |
| display-list vertex cache | 4 MB, plus 1-2 MB of dynamic templates in malloc (the second MB only while 6 MB is free) | |
| EFB copy targets | ~1-3 MB | visors, blur, shadows: new for Prime |

That adds up to more than the ~60 MB free once the kernel is up, so the
commit-on-demand and disc-backed-ARAM tricks are required, not optional.

Memory pressure (`xhw_mem_reclaim`, `xhw_sys.c`): below 3 MB free the
game thread asks the reclaimers for up to 1 MB a frame (`xhw_mem_tick`),
and a commit that fails asks them again, without blocking, before the Out
of memory screen. ARAM is the reclaimer today: chunks wholly on the disc
go back at once, the rest after a 64 KB write to the swap file; a touch
brings them back as disc pages come back. In xemu with 7 MB held back
(`env PX_MEM_BALLOON_KB=7000`, test builds) Main Plaza ran with free
memory down to 0.9 MB and no failure; ARAM went from ~3.6 MB committed to
~1.5 MB.
Melee-X's `[MEM]` lines and `crash.log` report what's committed; the burn
test (phase 7) watches it.

## Video

- 480i/480p: 640x480x32. 720p: 1280x720 R5G6B5 + Z24S8. As Melee-X.
- 16:9 at 480 and 720: hor+ (the camera's aspect widened; Prime's 3D HUD and
  helmet follow the camera, the 2D parts anchor to the edges with the
  edge-to-edge HUD option; lock-on zone scaled, MetroidPrimePort's fix).

## Input

Four ports read, port 1 plays (Prime is single-player). GameCube layout by
position (README table), triggers analog with a click point, rumble, per-port
remapping in `settings.ini`. Melee-X's `pad.c`/`xhw_pad.c`.

## Audio

MusyX (software synth, as MetroidPrimePort builds it) mixes the game's sound
effects and sequenced music; Prime also streams music from the disc as
DSP-ADPCM (`CStreamAudioManager`). Both end in AI DMA buffers that the Xbox
layer feeds to the AC97 (Melee-X's `xhw_audio.c`). Callbacks run on the game
thread.

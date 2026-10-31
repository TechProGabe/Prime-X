# Roadmap

Tick each box when its gate in `docs/plan.md` passes, with the date and
the commit. A box stays open while any part of its gate is unchecked; the
line under it says how far it got.

- [x] Plan: repo, docs, logo and icon (2026-10-27)
- [ ] Phase 0: toolchain, the C++ spike, `TARGET_PC` inventory, import
      strategy, SDK surface, memory model
      - done except the Docker half of the toolchain gate (Docker isn't
        installed on the build PC; CI covers it); plan 0.6 (game `new` in
        `CGameAllocator`) done 2026-10-28
- [ ] Phase 1: import; every game unit compiles; `default.xbe` links;
      `CHECK_SIZEOF` passes
      - compiles and links (2026-10-27); host tests pass except
        `test_vp_encoder` (wants the `nv2a_vsh` module)
- [ ] Phase 2: boot to the title screen in xemu and on red; disc
      validation, 128 MB hold, upgraded-CPU handling
      - boots to the title, the file select and into the game in xemu and
        on red; the full disc check matches the Redump hash on red (hw1).
        Doctored images in xemu (2026-10-28): wrong ID, another revision,
        trimmed and a changed file each stop at their screen
        (`build-xbox/shots/disc_*`); `XHW_CODE_REPAIR_TEST` rewrote an
        rdtsc as a BIOS would and the code check put it back. Still to
        show: the 128 MB hold (xemu's BIOS here exposes 64 MB whatever
        `mem_limit` says: needs a 128 MB BIOS)
- [ ] Phase 3: renderer parity with Dolphin in the benchmark rooms, all
      visors, 4:3 and 16:9
      - every world renders in xemu (room sweep, 14 rooms) and on red;
        visors switch; 4:3, 16:9 and 720p letterboxing work. Parity is
        unjudged: Dolphin reference frames in any room since 2026-10-30
        (`tools/xbox/dolphin_ref.py`, no save needed), the comparisons
        not made yet
- [ ] Phase 4: audio, input, saves (Frigate to the landing site, save,
      reload; Dolphin `.gci` both ways)
      - MusyX sound and music, streamed music, rumble, pads and the button
        remap work on red. The save round trip works in xemu (2026-10-29:
        Chozo's Save Station 1 saves to file A, a new boot's main menu
        lists it, Continue loads Samus back on the pad; `PX_CARD_FILE`,
        docs/testing.md); not yet on red, and Dolphin `.gci` isn't tested
- [ ] Phase 5: settings rows in the game's Options, the splash, the extras,
      the dashboard icon
      - the splash with its music, the icon, Port Options on the main menu
        and the pause screen (video, widescreen HUD, FOV, FPS counter,
        rumble, dead zones, trigger click, screenshots, Skip Cutscenes,
        Fusion Suit, button remap); each row's effect on the console is
        still to be walked through
- [ ] Phase 6: 60 fps at 480, 30+ at 720p on both consoles; ThinLTO + PGO
      - ThinLTO + PGO + profile-ordered link, -O3, Game Timing: Auto (30
        Hz ticks drawn at 60 when the frame doesn't fit), GPU skinning,
        texture-only draws. RC1c (hw18d, medians): red at 480i 39-51 fps
        (pirates 39, Main Plaza 49, Landing Site 50); gold at 720p (30 Hz
        ticks) 28-37 fps. Neither target met yet
- [ ] Phase 7: full playthrough and burn test on hardware; v1 on `main`
      - release candidate 1 (RC1c, 0ff5ed0, 2026-10-31) on red and gold
        for the user's free play

## Known issues

Open:

- Audio stuck on red ("Sound hardware is stuck", silent until a
  power-off): chained relaunches (`PX_NEXT_XBE`) in hw9, hw10, hw16 and
  hw18-hw18c (none in hw18d's chain, never on gold). The codec stops
  taking frames; restarts and the cold reset don't help (handoff, Next 2).
- The `pb_init` crash (pbkit's store to 0x80000000, an access violation):
  3 times in hw18-hw18c's ~13 chained launches, each time page 0 mapped
  read-only. Since 0ff5ed0 the guard claims page 0 or gives it its write
  bit; hw18d's 11 launches found it writable, so the repair is unproven.
- Gold's stock drive (nearly full): the first load into a world takes
  4-5 s (red ~0.8 s); a 1 MB memory reclaim 0-370 ms (was 1.1-1.7 s).
  Screenshots took 27-47 s there (fixed on `dev` after RC1c, 6963d15).
- 720p is GPU-bound in places: gold 28-37 fps (hw18d), the GPU 12-14 ms a
  frame in Phendrana (hw17).
- Fog volumes aren't drawn (`PostRenderFogs`, docs/decisions.md):
  Magmoor's lava haze and the other volume fogs (acid, water); what was
  listed as the "heat haze without its warp" is this pass. Dolphin's Lava
  Lake is dimmer for it. Needs the depth buffer read as a texture, a new
  render path (handoff, Next 6; the user's call).
- Room-entry stalls of 0.3-1.3 s (disc reads; hw10, still in hw18d's
  logs at each scene's load).
- Inside the gunship: 12-14 fps on red in hw5; the dynamic-list budget is
  doubled since, not measured.

Fixed on hardware since hw10: fights 17-18 -> 30-32 fps (Auto, GPU
skinning; hw12); the pause screen 14-17 -> 58-60 (hw12); Main Plaza at
720p 13-15 -> 36-40 (the 7 MB texture pool, hw18); the audio-lock
deadlock after a Map Station (free play 2f145b2, fixed 2026-10-30); gold's
drive stalls at reclaims (hw18d); the landing glow box, the lightning-lit
clouds, the thermal and X-ray palettes (hw10). No out of memory since
hw8.

Fixed, not yet seen on red: the charge beam (hw8), Port Options' value
cells off their rows (hw6, hw8), everything too bright after Morph Ball,
the white arm cannon (Chozo, Magmoor).

Compared with Dolphin (2026-10-30, `tools/xbox/dolphin_ref.py`): the 14
sweep rooms match, the arm cannon and particles included, but for the fog
volumes; thermal matches; X-ray isn't compared (Dolphin can't hold it
without input).

## Console history

| date | round | console | result |
|---|---|---|---|
| 2026-10-27 | hw1 | red, 480i | boots, full disc check passes, every world loads; Tallon ~10 fps, Main Plaza ~5; landing cinematic streaked white |
| 2026-10-27 | hw2, hw3 | red | first-boot experience: splash, disc check with music; fixes to the bar and the note line |
| 2026-10-27 | hw4 | red | Landing Site 27-41 fps (ThinLTO + PGO); r4c froze in audio start-up |
| 2026-10-28 | hw5 | red | five-scene profile chain, back to the dashboard by itself; no freeze; landing streaks gone |
| 2026-10-28 | hw6 | red | free play: the alpha fix's visuals; out of memory at 290 s; too bright after Morph Ball; Options divider on the text |
| 2026-10-29 | hw7 | red | seven-scene chain r7a-g; r7e froze in audio start-up (fixed `f73d0d7`) |
| 2026-10-29 | hw8 | red | r8g only (Flex, Landing Site 40.2 fps median); free play: no out of memory, pause screen 14-17 fps, charge beam dead |
| 2026-10-29 | hw9 | red | chain r9a-e (r9f froze in audio start-up); free play to Parasite Queen: visuals fine, fights 10-20 fps. Hardware testing paused after it (the user) |
| 2026-10-30 | hw10 | red | chain r10a-h ran: landing glow box gone, visors' palettes right, Landing Site 34-59 (hw9 dipped to 17), Main Plaza 30-36, fights 17-18 (3.4 ticks per render); audio stuck from r10c on (the old fault); no free play |
| 2026-10-30 | hw11 | red | FPS plan M0-M2: Fixed against Auto, the mixer's share measured (fps-plan.md) |
| 2026-10-30 | hw12 | red | GPU skinning, Auto everywhere: fight 30-32, pause screen 58-60, Lava Lake 40-41; no stuck audio in 21 chained scenes (hw11-12) |
| 2026-10-30 | hw13 | red | -O2 against -O3: -O3 1-4% ahead everywhere, kept |
| 2026-10-30 | hw14 | red | Main Plaza 38-39; the pushbuffer CALL test faulted the DMA pusher |
| 2026-10-30 | hw15 | red | texture-only draws: Main Plaza 43-44, pirates 34-35, Lava Lake 44-45; JUMP and CALL tests passed |
| 2026-10-30 | free play | red, gold | 2f145b2 hung on red after a Map Station (audio lock order, fixed); gold's first run at 720p: median 35 fps, no fault |
| 2026-10-31 | hw16 | red | GPU rain, retrained PGO: pirates 37-38, Main Plaza 47-48; full LTO and the 16384-word kick no faster (dropped); audio stuck r16b-k |
| 2026-10-31 | hw17 | gold, 720p | 30 Hz ticks, ring in the video framebuffer: no swapping back; Main Plaza 13-15 (lists decoded: 6 MB pool); drive stalls >1 s ~18 a scene |
| 2026-10-31 | hw18 | red + gold | first both at once: red clean; gold 720p Main Plaza 36-40; gold froze with the ARQ worker on (backed out) and crashed once in `pb_init` |
| 2026-10-31 | hw18b, hw18c | red + gold | RC1, RC1b: the `pb_init` crash on the launch after Lava Lake on both (page 0 read-only); gold's reclaims 1.1-1.7 s |
| 2026-10-31 | hw18d | red + gold | RC1c (0ff5ed0): all 11 scenes on both, no fault; gold reclaims 0-370 ms, its screenshots 27-47 s (fixed after); release build left on both for free play |

## Performance per build

Console numbers only (xemu's are in `docs/performance.md`).

| build | room | mode | fps | notes |
|---|---|---|---|---|
| hw1 (plain) | Landing Site | 480i | ~10 | CPU-bound: sim 70 ms, display lists 22 ms |
| hw1 (plain) | Main Plaza | 480i | ~5 | display lists 120 ms, 7000 draws |
| hw4 r4a (LTO + PGO) | Landing Site | 480i | 27-41 | draw batching, LTO + PGO, SSE skinning |
| hw5 r5a (profiler) | front end | 480i | 32-45 | integer IDCT for the movie |
| hw5 r5b (profiler) | Landing Site | 480i | 37-46 | particles fixed, rain culling |
| hw5 r5c (profiler) | Main Plaza | 480i | 21-46 | low while it streams in |
| hw5 r5d (profiler) | Frigate opening | 480i | 26-43 | |
| hw5 r5e (profiler) | Phendrana | 480i | 36-43 | |
| hw10 r10c (profiler) | Landing Site | 480i | 34-59 (low 24) | Fixed; hw9's r9b dipped to 17-22 |
| hw10 r10d (profiler) | Frigate Crash Site, pirate fight | 480i | 17-18 | Fixed: 3.4 ticks per render, sim ~12 ms a tick; 33-36 after the fight |
| hw10 r10e (profiler) | Main Plaza | 480i | 30-36 | Fixed; hw9's r9c 24-32 |
| hw10 r10f (profiler) | Phendrana, visors | 480i | 48-60 | thermal and X-ray palettes |
| hw10 r10g (profiler) | Lava Lake | 480i | 27-28 | Fixed |
| hw10 r10h (profiler) | Frigate opening | 480i | 50-60 | Fixed |
| hw12 (profiler) | Frigate Crash Site, pirate fight | 480i | 30-32.5 | Auto, GPU skinning |
| hw15 (profiler) | Main Plaza | 480i | 43-44 | texture-only draws |
| hw18d r18a (profiler) | Frigate Crash Site, pirates | 480i | 39 (median) | RC1c |
| hw18d r18b (profiler) | Main Plaza | 480i | 49 | RC1c |
| hw18d r18c (profiler) | Lava Lake | 480i | 43 | RC1c |
| hw18d r18d (profiler) | Landing Site, rain | 480i | 50 | RC1c |
| hw18d r18e (profiler) | Phendrana, visors | 480i | 51 | RC1c |
| hw18d g18a (profiler) | Main Plaza | gold, 720p | 37 | RC1c, 30 Hz ticks |
| hw18d g18b (profiler) | Frigate Crash Site, pirates | gold, 720p | 30 | RC1c |
| hw18d g18c (profiler) | Landing Site, rain | gold, 720p | 28 | RC1c |
| hw18d g18d (profiler) | Lava Lake | gold, 720p | 34 | RC1c |
| hw18d g18e (profiler) | Phendrana, visors | gold, 720p | 32 | RC1c |
| hw18d g18f (profiler) | Main Plaza | gold, 480p | 46 | RC1c |

## Future (after v1, not planned)

USA 1.01/1.02 and PAL discs; more MetroidPrimePort extras if they fit
(frame interpolation is out: the console has no frames to spare);
4:3-only HUD tweaks; LAN anything.

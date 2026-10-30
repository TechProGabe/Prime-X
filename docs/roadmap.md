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
      - ThinLTO + PGO + profile-ordered link in place; red at 480: 37-46
        fps in the Landing Site's rain, 21-46 in Main Plaza (hw5)
- [ ] Phase 7: full playthrough and burn test on hardware; v1 on `main`

## Known issues

Open:

- Fog volumes aren't drawn (`PostRenderFogs`, docs/decisions.md):
  Magmoor's lava haze and the other volume fogs (acid, water); what was
  listed as the "heat haze without its warp" is this pass. Dolphin's Lava
  Lake is dimmer for it. Needs the depth buffer read as a texture, a new
  render path (handoff, Next 3; the user's call).
- Fights are slow on red: hw10's pirate fight (r10d) 17-18 fps at 3.4
  ticks per render (hw9 free play 10-20). The simulation is ~12 ms a
  tick; Game Timing: Fixed catches up 3-4 ticks a frame, Flex would run
  one (~35 fps).
- Hitches of 250-300 ms at pickups and some doors (hw9 free play): not
  measured since. hw10's room-entry stalls (0.3-1.3 s) were ~65% waiting
  (disc reads); the profiler build logs [PROFT] for any such frame.
- Audio stuck ("Sound hardware is stuck", silent until a power-off):
  hw9 r9e, hw10's first r10a and every hw10 scene from r10c on (it began
  at a relaunch). The codec stops taking frames; resets don't help. The
  start-up freeze (hw4, hw7, hw9 r9f, on codec register reads) didn't
  happen in hw10's eight launches (no codec reads since 2026-10-29).
- Inside the gunship: 12-14 fps on red in hw5; the dynamic-list budget is
  doubled since, not measured.
- Out of memory on red (hw5 at 215 s, hw6 at 290 s): gone in hw8's free
  play; keep an eye on longer sessions.

Fixed on red in hw10: the landing cinematic's glow box and the
lightning-lit clouds' hard edge; the thermal and X-ray palettes.

Fixed, not yet seen on red (hw10 had no free play):

- The pause screen at 14-17 fps (hw8; ~40-46 fps in xemu), the charge
  beam (hw8), Port Options' value cells off their rows (hw6, hw8).
- Everything too bright after Morph Ball, the white arm cannon (Chozo,
  Magmoor).

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

## Future (after v1, not planned)

USA 1.01/1.02 and PAL discs; more MetroidPrimePort extras if they fit
(frame interpolation is out: the console has no frames to spare);
4:3-only HUD tweaks; LAN anything.

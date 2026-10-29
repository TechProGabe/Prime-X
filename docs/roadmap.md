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
        unjudged: Dolphin is on the build PC (2026-10-28) but needs a save
        in the rooms to compare
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

- **Out of memory on red** in free play: hw5 at 215 s, hw6 at 290 s.
  Since then: memory-pressure reclaim and the ARAM swap file (xemu Main
  Plaza with 7 MB held back runs at ~1 MB free without failing), 0.9 MB of
  `.bss` less, the texture pool 7 MB. To be played on the console again.
- Fixed in xemu, not yet seen on red: everything too bright after Morph
  Ball, the white arm cannon (Chozo, Magmoor), the Options divider on the
  lower Video rows (not reproduced in xemu: the fix is by construction).
- Inside the gunship: 12-14 fps on red in hw5; the dynamic-list budget is
  doubled since, not measured.
- Magmoor's heat haze draws without its warp (indirect maps past
  `ind_tex`'s size limit).
- The audio start-up freeze after a chained launch (hw4 r4c) came back in
  hw7 (r7e, the codec read after the cold reset, the CPU held: no watchdog
  reboot); `f73d0d7` no longer reads the codec after the reset on the
  console. r7e then started cleanly once; to be watched.
- Landing cinematic (Tallon Overworld), the gunship coming down through
  the atmosphere: a box around its glow sprite (the user saw it black on
  red in hw7; hw7's r7b shot at frame 600 has a lighter square around the
  sprite). The sprite's alpha or blend is wrong: its quad's edges should
  fade out. Not looked at yet.
- Rendering not yet compared with Dolphin (installed, needs a save in the
  rooms): the arm cannon in bright rooms, Phendrana's elevator steam, the
  visors.

## Console history

| date | round | console | result |
|---|---|---|---|
| 2026-10-27 | hw1 | red, 480i | boots, full disc check passes, every world loads; Tallon ~10 fps, Main Plaza ~5; landing cinematic streaked white |
| 2026-10-27 | hw2, hw3 | red | first-boot experience: splash, disc check with music; fixes to the bar and the note line |
| 2026-10-27 | hw4 | red | Landing Site 27-41 fps (ThinLTO + PGO); r4c froze in audio start-up |
| 2026-10-28 | hw5 | red | five-scene profile chain, back to the dashboard by itself; no freeze; landing streaks gone |
| 2026-10-28 | hw6 | red | free play: the alpha fix's visuals; out of memory at 290 s; too bright after Morph Ball; Options divider on the text |

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

## Future (after v1, not planned)

USA 1.01/1.02 and PAL discs; more MetroidPrimePort extras if they fit
(frame interpolation is out: the console has no frames to spare);
4:3-only HUD tweaks; LAN anything.

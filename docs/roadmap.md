# Roadmap

Tick each box when its gate in `docs/plan.md` passes, with the date and
the commit.

- [x] Plan: repo, docs, logo and icon (2026-10-27)
- [ ] Phase 0: toolchain, the C++ spike, `TARGET_PC` inventory, import
      strategy, SDK surface, memory model
- [ ] Phase 1: import; every game unit compiles; `default.xbe` links;
      `CHECK_SIZEOF` passes
- [ ] Phase 2: boot to the title screen in xemu and on red; disc
      validation, 128 MB hold, upgraded-CPU handling
- [ ] Phase 3: renderer parity with Dolphin in the benchmark rooms, all
      visors, 4:3 and 16:9
- [ ] Phase 4: audio, input, saves (Frigate to the landing site, save,
      reload; Dolphin `.gci` both ways)
- [ ] Phase 5: settings rows in the game's Options, the splash, the extras,
      the dashboard icon
- [ ] Phase 6: 60 fps at 480, 30+ at 720p on both consoles; ThinLTO + PGO
- [ ] Phase 7: full playthrough and burn test on hardware; v1 on `main`

## Known issues

None yet.

## Console history

| date | build | console | result |
|---|---|---|---|

## Performance per build

| build | room | mode | fps median / p5 | notes |
|---|---|---|---|---|

## Future (after v1, not planned)

USA 1.01/1.02 and PAL discs; more MetroidPrimePort extras if they fit
(frame interpolation is out: the console has no frames to spare);
4:3-only HUD tweaks; LAN anything.

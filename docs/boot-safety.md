# Boot safety: disc validation, 128 MB consoles, upgraded CPUs

The user's requirements (2026-10-27): really good disc validation at boot,
128 MB consoles locked to 64 MB as on Melee-X, and overclocked or upgraded
CPUs handled gracefully. All three run before the game starts
(plan 2.5a), and each has a gate check in phase 2.

## Disc validation

Melee-X only checks the header's game ID and revision (`dvd.c`). Prime-X
goes further: a wrong, modified, trimmed, truncated or damaged image is
named and stopped before it can crash the game an hour in.

### What's known about the good disc

MetroidPrimePort's `platform/port_disc_verify_table.inc` (generated from the
Redump images) gives, for USA 1.00: game ID `GM8E01`, version byte 0, size
1,459,978,240 bytes, SHA-1 `ac20c744db18fdf0339f37945e880708fd317231`, and
the CRC32 of each of its 1393 1 MiB chunks. Hashes aren't game data, so
they can be committed. Its table also has 1.01, 1.02 and PAL, which lets
the error screen say what the image *is*.

Whole-image chunk CRCs only work for a byte-exact `.iso`/`.gcm`. A `.ciso`
drops unused blocks, and the unused areas of a GameCube disc are filled
with pseudo-random junk, so a chunk that's part file and part junk won't
match after compression. So Prime-X checks **files**, not chunks:

- `tools/disc_file_table.py <usa100.iso>` (new; run once by whoever has a
  Redump dump, checked against the SHA-1 above before it writes anything)
  walks the FST and writes `xbox/src/sdk/disc_table.inc`: the boot block and
  `bi2.bin`, the apploader, `main.dol`, the FST itself, then every file's
  path, offset, size and CRC32. It also carries the whole-image SHA-1 and,
  for the error screen, the other revisions' IDs, version bytes and sizes
  from MetroidPrimePort's table.

### Checks

| when | what | cost |
|---|---|---|
| every boot | magic `0xC2339F3D`; ID `GM8E01`; version byte 0; the image is `.iso`/`.gcm` of exactly 1,459,978,240 bytes or a `.ciso` whose map covers every file; the FST equals the table (entry count, every name, offset and size); CRC32 of `main.dol` (the code the font addresses and every symbol are tied to) and of the FST | well under a second |
| first boot of an image | CRC32 of every file against the table, at boot behind the splash: "VERIFYING DISC IMAGE" with the file and its CRC as they pass, and why it runs ("so a bad copy is caught now, not as a crash later"); B skips it for that boot | about a minute on red (round hw1); never again for that image (`disc.ini`) |
| Options > Verify disc image | the full check again | same |

"New image" means a size, modification time and FST-CRC triple not in
`E:\UDATA\50580001\disc.ini`, which records the last result. An image that passed is not checked again (`disc.ini` holds its size and
write time); a failed or unfinished check runs again next boot. Test builds
take `env PX_DISC_CHECK=quick` (no full check) or `=full` (at boot, behind
the splash's bar, with the A/B choice, even for an image that passed). Any
build: L + R + Y held while Prime-X starts forces the full check too, even
from a DVD (for watching it on a console; nothing on screen mentions it,
`[DISC] L+R+Y held at boot` in `boot.log`).

### What the player sees

Every failure is an error screen in the splash's style (`docs/ui.md`) with a
plain reason and what to do, and a `[DISC]` line in `boot.log`:

| found | screen says |
|---|---|
| no image next to `default.xbe` | the Melee-X "No disc image found" screen, with Prime's details |
| not Metroid Prime (ID) | "This is not Metroid Prime NTSC-U (GM8E01): the image says XXXXXX." |
| another revision or region | "This is Metroid Prime USA 1.02 (or PAL, ...). Prime-X needs USA 1.00." (named from the table) |
| right ID, wrong size, or an FST that differs | "This image has been changed or trimmed (NKit, a mod, a scrubbed copy). Prime-X needs a clean 1:1 dump." |
| a `.ciso` missing blocks that files use | "This image is missing data (N files cut short)." |
| a file's CRC differs | "N files don't match a clean dump (first: Metroid4.pak). The game may crash in those areas. A: play anyway, B: quit." |
| read errors | "The image couldn't be read at offset X (hard drive or disc fault)." |

Only the last two let the player go on. `boot.log` gets every mismatching
file with its expected and found CRC, and the SHA-1 line (`[DISC] redump
match` or `[DISC] sha1 <hash>`), so a bug report shows its dump at a glance.

## 128 MB consoles

Melee-X's rule, unchanged (its `docs/decisions.md`, "128 MB consoles always
run in 64 MB"): `xhw_mem_hold_upper` takes every page above 64 MB first
thing in `xsdk_early`, at every boot, with no setting to turn it off.
`boot.log`'s `[MEM]` line shows the RAM found and held. One memory layout
everywhere means a report from a 128 MB console is the same as one from a
stock console. Tested in xemu with a 128 MB configuration.

## Upgraded and overclocked CPUs

Consoles with a 1.0-1.4 GHz CPU upgrade, or the stock CPU on a raised FSB,
break games in three ways. Prime-X handles each:

1. **The BIOS patches the game's code.** Two forms seen. OpenCrossing's
   issues: `0f 31` (`rdtsc`) rewritten as `cd 2e` (a trap) by signature,
   which can hit byte pairs that aren't `rdtsc` at all. The beta tester's
   Cerbios console (a 1.4 GHz CPU, 2026-11-01): 5-byte `jmp` detours into
   the kernel (`e9 rel32` over the first bytes of an instruction), one at
   pbkit's PCRTC interrupt-enable write (`mov [0xFD600140], 0`) even with
   AdvCPUSupport off, and 43 bytes with it on, the first at an `rdtsc` in
   the profiler, none of them `cd 2e`. Melee-X's `xhw_code_repair.c`
   (from OpenCrossing-Xbox) compares `.text` with `D:\default.xbe` at
   boot, before anything reads the TSC, and puts back every byte that
   differs, whatever the patcher wrote, when the file is the running
   build (its header's time stamps and size) and at most 1024 bytes
   differ. Until 2026-11-01 it also required the differences to "look
   like a patcher's" (any number of `cd 2e`, at most 16 other bytes),
   which left Cerbios's detours in place: that was the slow motion. The
   log: `[BOOT] code check: 43 bytes of .text were changed in memory
   after load (9 places, 8 at an rdtsc; a BIOS patcher), put back`, then
   each run of changed bytes, the memory's and the file's
   (`001f77ff: e9 18 7b e2 7f, file c7 05 40 01 60`). Test builds:
   `-DXHW_CODE_REPAIR_TEST` patches two sites (a `cd 2e`, a 5-byte jmp)
   so the repair shows in xemu.
2. **Code that counts on 733 MHz.** Game time, `OSGetTime`/`OSGetTick`, the
   frame pacer and the watchdog come from the ACPI timer
   (`KeQueryPerformanceCounter`, 3.375 MHz) and VBlank, never the TSC
   alone. `xhw_time_ns` reads the timer every 100 ms and interpolates in
   between with the TSC at the rate measured against the timer at boot
   (reading the timer's I/O port on every call cost ~3% of the CPU).
   Where the TSC is still used for cheap intervals (the interrupt lock's
   delivery throttle, `os.c` `DELIVERY_TSC`; the profiler), its rate is
   measured at boot against the ACPI timer and the constants are scaled by
   it.
   **The ACPI timer against the display** (2026-11-01, after the first
   overclock reports; `xhw_clock_display_frame`, called once a frame from
   the frame boundary): the timer is the chipset's reference, which an
   FSB overclock (Cerbios's overclock settings, XBOverclock) can move with
   the CPU, while VBlank comes from the GPU's own 16.667 MHz crystal. Over
   the first 3 s of frames the vblanks (pbkit's count) are counted against
   the clock and compared with the mode's refresh (nxdk's 50 or 60). Within
   1.5%: `[CLOCK] display 59.940 Hz by the clock, mode 60 Hz: agree`. Off
   by more (and by less than 2.5x): the clock is scaled to the display from
   that moment on (`xhw_time_ns` and everything on it: game time, the
   pacer, the TSC's ns per tick), once, continuous at the switch, and a
   second window checks it. A window with the GPU interrupt masked (the
   count stalls) starts over. Test builds: `env PX_CLOCK_SKEW=120` runs the
   clock 20% fast so xemu exercises the correction.
   The windows are timed by the ACPI timer itself (`acpi_ns`, the
   corrected timer with no TSC interpolation in it), since the timer is
   what's being checked; and the window after a correction has to agree,
   else the correction is undone and the check is over (2026-11-01: on
   the Cerbios console a clock jumping on a patcher's rdtsc read the
   display at 40.089 Hz, the clock was scaled to 0.668, the next window
   read 89.797 Hz, and the game ran at two thirds speed by a clock that
   said 60 fps).
   **rdtsc that traps**: after the code check, and again when the display
   check ends and every 30 s of frames after, 2000 `rdtsc` are timed
   against the timer; over 250 ns each (a trap is a microsecond; the
   console's rdtsc is ~40 ns, 23 on a 1.4 GHz CPU) means a patcher
   rewrote them after all (or again), and from then on `xhw_time_ns`
   reads the ACPI timer on every call (~3% of the CPU, as before round
   hw5) and the interrupt lock's throttle uses the kernel's 1 ms tick
   (`xhw_tsc_usable`), instead of trapping thousands of times a frame.
   **rdtsc that lies**: the TSC's rate is measured against the timer over
   50 ms at the first call; outside 100 MHz-4 GHz it isn't a counter
   (2026-11-01: 164950 MHz, the patcher's detours returning one thing at
   some rdtsc sites and the raw counter at others) and the same fallback
   applies, with a `[CLOCK] tsc calibrates at N MHz ... no sane counter`
   line.
   **libc++'s `steady_clock`** (main.cpp's tick budget, the area-load and
   door timers, `MP_LOG`) runs on `QueryPerformanceCounter`, which in
   nxdk is `rdtsc` at the rate `tsc_freq_init` works out from the PLL and
   the CPU's multiplier (733333333 for a CPU its table doesn't know:
   1.9x fast on a 1.4 GHz upgrade; the scene's "frequency patch" of the
   XBE's constant is for that). Prime-X defines
   `QueryPerformanceCounter/Frequency` itself (`xhw_sys.c`, taking
   precedence over libwinapi's at link time): the clock above in 10 us
   units (libc++ multiplies the count by 1e9 before dividing, so the count
   has to stay under 9.2e9: 25 hours). No patch needed.
3. **Instructions the upgrade CPU has but the stock one doesn't (and the
   reverse).** The build targets `-march=pentium3` (SSE1, no SSE2) and
   never picks a code path by CPUID, so every console runs the same code.

`boot.log` gets `[CLOCK]` lines (release builds too, when Log File is
on): at boot, after the code check, the kernel's counter rate, the TSC's
MHz measured against it, what one `rdtsc` costs, and the kernel's 1 ms
tick over 250 ms of timer time; then the display check's verdict. The
last 8 are kept and logged again ("again: logged before the log was on")
when Log File is turned on mid-session, since a release build drops the
boot's lines once settings.ini says the log is off. `-DXHW_LOG_ALWAYS=1`
builds a release whose log is on from boot whatever the setting (a
tester's diagnostic build, 2026-11-01). Test builds add `[CPU]` (CPUID,
control registers) and `[CAL]`. A faster CPU only means more frames: no
frame cap besides the display's refresh.

What the beta's "slow motion" was (2026-11-01, from a tester's logs;
`docs/handoff.md` has the lines): a 1.4 GHz CPU upgrade on Cerbios, with
the FSB and the ACPI timer stock (`tsc 1399.97 MHz`, the display check
agreeing at 60.181 Hz). cerbios.ini's `AdvCPUSupport` ("Enables Runtime
XBE Patching For CPU-Upgraded Or Overclocked Consoles") is the detour
patcher above. With it off, and the tester's "frequency patch" (the
scene's patch of nxdk's 733333333, in `.data`, which only `steady_clock`
ran on), the game ran right. With it on, the code check of the time
left its 43 bytes in place, the TSC calibrated at 164950 MHz, the clock
jumped ahead and froze on the monotonic clamp, the pacer spun through
the freezes (`vsync 1220 ms` a frame, `MP stall: frame 542 took 2658
ms`, the watchdog's `frames stopped` every 7 s), and the display check
read 40 Hz in that and "corrected" the clock to two thirds. The four
changes above (any patch put back; the TSC's rate sanity-checked; the
display windows on the timer itself with a correction undone when the
next window disagrees; `steady_clock` on the clock) each close one of
those. The pacer waits by the clock for 60 Hz, so any clock that runs
slow throttles the menu (the one place frames are cheap enough to wait)
and fixed-step ticks go slow motion: that's why the menu looked worst.
Where an overclock would come from: nxdk's `tsc_freq_init` reads the FSB
from the NV2A's CPU PLL (PCI 0:0x60 register 0x6C: 16.667 MHz crystal /
divider x multiplier, times the CPU's multiplier from MSR 0x2A), and
Cerbios's overclock settings are `CPUMPLLCoeff`/`NVPLLCoeff`, i.e. that
PLL; the ACPI timer (in the MCPX) isn't on it, so the display check is
expected to "agree" there too, and the boot line logs both PLLs
(`[CLOCK] fsb 133.3 MHz (cpu pll 01230801), gpu 233 MHz (nvpll
00011c01)` stock).

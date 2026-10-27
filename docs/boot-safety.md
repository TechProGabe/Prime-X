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
| first boot of an image | CRC32 of every file against the table, on a worker while the splash's scan bar shows progress; for a plain `.iso`/`.gcm` also the SHA-1 of the whole image, for the Redump line | about a minute from the hard drive (1.4 GB); slicing-by-8 CRC on the P3 is not the limit |
| Options > Verify disc image | the full check again | same |

"New image" means a size, modification time and FST-CRC triple not in
`E:\UDATA\50580001\disc.ini`, which records the last result. The full check
can be skipped with B (it says what that risks) and then runs again on the
next boot; a skipped or failed check is in every `boot.log` until a full
check passes.

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

1. **The BIOS patches the game's code.** Cerbios (AdvCPUSupport, on by
   default) and others rewrite `0f 31` (`rdtsc`) as `cd 2e` by signature,
   and can hit byte pairs that aren't `rdtsc` at all. Melee-X's
   `xhw_code_repair.c` (from OpenCrossing-Xbox) compares `.text` with
   `D:\default.xbe` at boot and puts back every byte a patcher changed,
   before anything reads the TSC. Prime-X takes it as is; its log line is
   `[BOOT] code check`. C++ code has more `0f 31` byte pairs than C, so the
   count is logged and the `MAX_OTHER` limit checked against Prime's image.
2. **Code that counts on 733 MHz.** Game time, `OSGetTime`/`OSGetTick`, the
   frame pacer and the watchdog come from the ACPI timer
   (`KeQueryPerformanceCounter`, 3.375 MHz) and VBlank, never the TSC.
   Where the TSC is still used for cheap intervals (the interrupt lock's
   delivery throttle, `os.c` `DELIVERY_TSC`; the profiler), its rate is
   measured at boot against the ACPI timer and the constants are scaled by
   it. The ACPI timer is cross-checked against VBlank over the first
   seconds, in case an FSB overclock moved it; if they disagree by more than
   1%, VBlank wins and `boot.log` says so.
3. **Instructions the upgrade CPU has but the stock one doesn't (and the
   reverse).** The build targets `-march=pentium3` (SSE1, no SSE2) and
   never picks a code path by CPUID, so every console runs the same code.

`boot.log` gets a `[CPU]` line: CPUID name and family, the measured TSC
rate in MHz, the ACPI/VBlank agreement, and how many bytes the code check
put back. A faster CPU only means more frames: no frame cap besides the
display's refresh.

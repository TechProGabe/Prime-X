Prime-X: Metroid Prime (NTSC-U 1.00, GM8E01 rev 0) running natively on the original Xbox.

**Beta 2.** Mostly fixes from your bug reports, plus a proper settings menu.

- **New Xbox Port Options menu.** It's on the main menu and in Pause > Options. Every setting explains what it does, and you can remap buttons on a picture of the controller.
- **Fixed the crash right after the Frigate escape** ("Assertion failed ... exp2f").
- **Remapping actually swaps buttons now** (#1). A always confirms in menus, so you can't lock yourself out.
- **Upgraded and overclocked CPUs** run at the right speed, Cerbios AdvCPUSupport included. You don't need a frequency patch.
- **Hand-edited settings.ini** mistakes get caught. The game tells you what it fixed and keeps your file as settings.bad.ini.
- Error screens wait for a button instead of rebooting.

Updating: copy the new `default.xbe` and `default.tbn` over the old ones. Saves and settings stay.

New install: put your own disc image (`.iso`, `.gcm` or `.ciso`) in the `Prime-X` folder next to `default.xbe`. The first start checks it once, which takes about a minute.

Found a bug? Turn on Xbox Port Options > System > Log File, make it happen again, and attach `E:\UDATA\50580001\boot.log`.

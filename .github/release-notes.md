Prime-X: Metroid Prime (NTSC-U 1.00, GM8E01 rev 0) running natively on the original Xbox.

**Beta 4.** A small one: two bugs you ran into, and simpler controls.

**Fixes**

- **No more black chunks in Chozo Ruins after saving or loading.** Parts of a room could turn solid black until you left it.
- **Samus shows up at save stations again.** With the Phazon Suit on, she and the room vanished behind the save prompt, and the pause menu had a black background.

**Controls**

- **Right trigger fires in Dual Stick**, same as A.
- **Button remapping is gone.** Pick Dual Stick or GameCube in Xbox Port Options > Controller > Controls, and the buttons follow that layout.

Updating: copy the new `default.xbe` and `default.tbn` over the old ones. Your saves and settings stay. If you had remapped buttons, they go back to the layout you pick.

New install: put your own disc image (`.iso`, `.gcm` or `.ciso`) in the `Prime-X` folder next to `default.xbe`. The first start checks the image once, which takes about a minute.

Found a bug? Turn on Xbox Port Options > System > Log File, make it happen again, and attach `E:\UDATA\50580001\boot.log`.

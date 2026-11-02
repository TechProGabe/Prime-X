Prime-X: Metroid Prime (NTSC-U 1.00, GM8E01 rev 0) running natively on the original Xbox.

**Beta 3.** This one is built around your feedback on Beta 2. The big changes are new controls, a crash fix, and a settings menu that looks like part of the game.

**Controls**

- **Dual Stick aiming is on by default** (#2). The left stick moves and strafes, the right stick aims. To switch beams, hold Black and tilt the right stick. Aim Speed (Low, Medium, High) is in Xbox Port Options > Controller. If you'd rather play like the GameCube did, set Aiming to GameCube in the same tab.
- **Free aim no longer feels backwards.** Pushing up looks up. Reverse Y-Axis in the game's Options flips it if you like it the other way.

**Fixes**

- **Fixed a crash when you quit to the title screen and load your game again.** The same bug could also leave black holes in a room's walls or freeze the screen with a "GPU fault".
- **The map zooms again.** It was stuck all the way out, and the triggers did nothing.
- **The Fusion Suit stays on.** It used to come off the next time you started the game.
- **No more false "crash" screen when restarting after a Video Mode change.** On slower stock hard drives, saving your settings took long enough that the game thought it had frozen.
- **If the sound chip stops responding, the game goes quiet and tells you.** Before, it kept trying to restart the sound and stuttered.

**Xbox Port Options**

- **It looks like Metroid Prime's own pause menu now:** amber, with the game's font and the game showing through behind it.
- **It's quicker.** The game pauses while the menu is open, so moving around the menu is instant.
- **Changing Video Mode asks whether to restart now or later.** Pick with the D-pad or the left stick.
- **Brightness, Screen Offset and Stretch are gone from the game's Display options.** They never did anything on the Xbox.

Updating: copy the new `default.xbe` and `default.tbn` over the old ones. Your saves and settings stay. Dual Stick turns on for everyone, and Black becomes Beam Select unless you'd already given it something else to do. You can change both in Xbox Port Options.

New install: put your own disc image (`.iso`, `.gcm` or `.ciso`) in the `Prime-X` folder next to `default.xbe`. The first start checks the image once, which takes about a minute.

Found a bug? Turn on Xbox Port Options > System > Log File, make it happen again, and attach `E:\UDATA\50580001\boot.log`.

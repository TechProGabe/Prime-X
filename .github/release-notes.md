Prime-X: Metroid Prime (NTSC-U 1.00, GM8E01 rev 0) running natively on the original Xbox.

**This is a beta.** The whole game should be playable from the Frigate to the credits, but it hasn't had wide testing yet, so you will run into bugs. If you hit one, turn on Port Options > System > Log File, play until it happens again, and attach `E:\UDATA\50580001\boot.log` to your report.

Bring your own disc image. Put it in the `Prime-X` folder next to `default.xbe` (`.iso`, `.gcm` or `.ciso`, any file name), or pack both into one image with `tools/make-xiso`. The first start checks every file of the image once, which takes about a minute.

What to expect on a stock console: about 40 to 50 fps at 480 in most areas, low 30s in big fights, and around 30 at 720p. The README has the full table and the list of known problems (fog volumes aren't drawn yet, sound can get stuck after one app launches another directly, slower loads on nearly full drives).

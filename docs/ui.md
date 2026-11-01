# UI: boot splash and settings

Two pieces of UI are Prime-X's own. Both are specified by the user
(2026-10-27): the splash remixes Melee-X's "TechProGabe Presents..." card
in Prime's look. The settings were first rows inside the game's own Options
screens; since 2026-11-01 they are the Xbox Port Options popup (below).

No game art, fonts or logos in either: shapes, colours and the unscii font.

## Boot splash

### What Melee-X does (`xbox/src/hw/xhw_splash.c`, from OpenCrossing-Xbox)

Drawn by the CPU straight into the linear framebuffer `XVideoSetMode()`
returns, before the renderer takes over: no GPU state, no assets. A navy
gradient, "TechProGabe Presents..." in unscii-16 fading in, held 2 s
(`XHW_SPLASH_MS`), any button skips, a load bar under the title
(`xhw_splash_progress(f)`), and the "hold BACK for safe video" hint. API:
`xhw_splash_show()`, `xhw_splash_progress(f)`, `xhw_splash_release()`;
`-DXHW_NO_SPLASH` turns it off.

### Prime-X: a haunted CRT terminal (built, 2026-10-27)

The user picked option E of five (`tools/xbox/splash_mockups.py` draws them;
"A" was the scan-visor readout this section first described) and set the
order: "the headline should be Prime-X and then what it's doing".
`xbox/src/hw/xhw_splash.c`:

| element | what |
|---|---|
| credit | "TechProGabe Presents..." small and dim above the headline |
| headline | **PRIME-X** in unscii-16 at 5x, typed on (120 ms a letter), red and cyan fringes like a misaligned CRT gun, a 1 px glow, a block cursor that blinks once typed |
| game line | `METROID PRIME - GM8E01 - USA 1.00` |
| signal trace | a twitching line across the card, a burst every ~3 s |
| task | what the boot is doing, at 2x: SCANNING, VERIFYING DISC IMAGE, LOADING, with the percentage; a 40-segment bar under it. The bar eases towards the boot's progress (never slower than a second for the width) and the splash ends only once it has filled and held 300 ms (round hw2: it sat at 10% and jumped to 100%) |
| detail | during the disc check: the file just checked with its CRC and OK/BAD, and a hex line of its bytes |
| notes | the check says why and how to skip it: "First boot only: checking every file so a bad copy is / caught now, not as a crash later. ~1 min. B: skip", both lines the same colour (round hw2) |
| music | the first boot's check plays `xbox/assets/intro.mp3` at 75%, appended to `default.xbe` as IMA ADPCM (`tools/xbox/make_intro.py`, `xhw_intro.c`): a copy of the game can't lose it, and it costs no memory (the kernel never loads it) and ~0.1% of the CPU |
| boot log | top left, typed on: CHOZO ARCHIVE INTERFACE, then what the boot found: the CPU and its measured MHz (UPGRADED X86 past 780), memory (128 MB held to 64), the disc, the quick check, the files (178/178, checked before, skipped, mismatch) |
| rain | glyph rain in phosphor green (hex, symbols, Latin-1 marks), white heads, stronger towards the edges, kept out of the card, the log, the tag and the hint |
| tag | a red `// SIGNAL` top right, blinking slowly |
| brackets | slide in from the corners (400 ms, eased), orange as they lock, then cyan |
| CRT | scanlines, darker corners, a band torn sideways every ~4 s, a 300 ms fade in |
| errors | the same glass with orange brackets, "PRIME-X // SIGNAL LOST", the title in orange-red, the lines in light cyan (`xhw_error_screen` before the game owns the display); it stays until a button is pressed, no timeout (2026-11-01, a tester: the card is the guide to where the image goes) |
| footer | "Hold BACK for safe video (480i)" |

Cost (the user: "should not interfere with the main goal"): the glass, the
card and a per-pixel shade (scanlines, corners) are made once; a frame
copies them, draws only what moves through the shade and is copied out,
~25 fps from a normal-priority thread: 7 ms a frame in xemu (`[SPLASH]` in
`boot.log` has the console's). Minimum hold 2 s, any button skips the
hold (never the work). Kill switch `-DXHW_NO_SPLASH`; `-DXHW_SPLASH_DUMP`
takes `[FBDUMP]`s of the settled screen and of a quarter-done stage.

## Xbox Port Options: a popup of its own (2026-11-01)

The user, after the beta: "the port options menu is just shitty. i know we
were trying to keep it to match the game, but lets just do something new.
when someone selects port settings, make a clear custom menu pop up that
explains everything. add button mapping here, we can use the button mapping
tech from recomp-x". Then: "make port options stand out" on the main menu,
and call it "Xbox Port Options". This replaces the 2026-10-27 rule that the
settings live in the game's own Options frames (that design is summarised
at the end).

### Where it opens

| from | what |
|---|---|
| the main menu | the row in place of "Metroid Fusion Connection Bonuses" (a GBA link an Xbox can't have) reads **Xbox Port Options**, in gold so it stands out (the game's own `&main-color` text tag on the front pane; the shadow pane keeps its colour) |
| the New Game popup | its third choice (Hard's slot before Normal is beaten): Start / Options / Xbox Port Options |
| the pause screen's Options | every category (Visor, Display, Sound, Controller) ends with an **Xbox Port Options** row above Restore Defaults; A opens the popup, the body pane says what it is |
| test builds | `env PX_MENU_AT=<frame>` in autopad.txt opens it at that frame (`tools/xbox/scenarios/menu`) |

The game keeps running behind it (the title's animation, the pause
screen) but reads a neutral pad until the popup closes and its closing
press is let go (`xsdk_menu_block`, pad.c), so B doesn't also leave the
game's menu.

### The card (`xbox/src/hw/xhw_menu.c`)

576 x 416 in 480-line pixels, centred, drawn at the output's scale: 1x at
480, 3/4 as wide at 480 when the dashboard says 16:9 (the TV stretches it
back), 1.5x at 720p. All shapes are antialiased from distances and the text
is unscii-16 supersampled to the scale.

| part | what |
|---|---|
| glass | a cyan border, a navy gradient, scanlines, the scan visor's corner brackets |
| header | **XBOX PORT OPTIONS** at 2x, a small orange PRIME-X, top right the mode now ("Now: 720p 16:9") |
| tabs | Video, Controller, Buttons, Gameplay, System between LT and RT badges; the current one lit |
| rows | the label left; the value right between two arrows, with pips under it (how many choices, which one); a value that isn't the default is orange with a dot; a row that applies at the next start has a `*` |
| help box | what the selected row does (white), what the shown value does (cyan), a note (orange: "* Video Mode changes the next time Prime-X starts", "The dashboard doesn't allow 720p: the next start uses 480p", "Press Y again ...") |
| footer | A Change, B Close, Y Defaults, the D-pad Select |
| Buttons tab | N64Recomp-X's Controls page: an Xbox controller (its body from a distance field, shaded once and cached when memory allows), every input with a leader line and a callout naming its action; the remappable ones (A B X Y White Black START BACK and the stick clicks) bright, the fixed ones (LT Lock On, RT Free Aim, D-pad Visors, left stick Move, right stick Beams; D-pad and right stick swap with Swap Beam Controls) dim; what port 1 holds lights up; the selected input has a ring |

Drawn by the CPU into the finished frame (after the GPU is idle, before
the flip and the screenshot dumps), as the notices are: no GPU state, the
same in every mode. Nothing reads the framebuffer (write-combined). The card
is drawn in 16-row bands in RAM, where the shapes blend, into a cache of the
whole card in the framebuffer's format (~0.9 MB at 480, ~1.1 MB at 720p)
that exists only while the popup is up; each frame copies each row's opaque
span. It is drawn again only when what it shows changes. If the cache can't
be allocated (after asking the reclaimers), the bands go straight into
every frame: more CPU, same picture; `[MENU] card ...` in boot.log says
which.

### The rows (`xbox/src/port/port_menu.cpp`)

| tab | row | values | what the box says the values do |
|---|---|---|---|
| Video | Video Mode `*` | 480i / 480p / 720p | every TV / sharper, component + dashboard / HD 16:9, component + dashboard, ~30 fps |
| Video | Widescreen | Auto / 4:3 / 16:9 | follows the dashboard / the GameCube's shape / more to the sides (720p is 16:9) |
| Video | Widescreen HUD | Inner / Edges | where the visor's readouts sit at 16:9 |
| Video | Field of View | 55 / 65 / 75 | the GameCube's / wider / widest |
| Video | Frame Rate | Hide / Show | the counter top left |
| Controller | Rumble Strength | Low / Medium / Full | 40 / 70 / 100% (On/Off is the game's own row) |
| Controller | Stick Dead Zone | Low / Medium / High | 10 / 20 / 30% |
| Controller | C-Stick Dead Zone | Low / Medium / High | 15 / 25 / 35% |
| Controller | Trigger Click | Light / Medium / Firm | how far LT/RT go in |
| Buttons | each remappable input | None, Fire, Jump, Morph Ball, Missile, Map, Lock On, Free Aim, Pause, D-Up/Down/Left/Right | what the action does in Prime |
| Gameplay | Skip Cutscenes | Off / On | START skips any cutscene, from the next room |
| Gameplay | Fusion Suit | Off / On | the GBA-link bonus, kept in the game's save |
| System | Screen Capture | Off / On | BACK saves shotNN.bmp |
| System | Log File | Off / On | boot.log for bug reports |
| System | Restore All Defaults | A, then A again | every Port Options setting, buttons included |

The rows call the same `PortXboxOptions::Get/Set/Step` the old in-game rows
did, so each applies at once (Video Mode at the next start) and settings.ini
is written once, when the popup closes. Buttons: taking an action another
button has swaps the two, and Fire always stays on some button (GitHub #1).

### Controls in the popup

The Xbox controllers as they are, not the remapped GameCube pad, so no
layout can lock a player out: up/down (D-pad or left stick; held, it repeats
after 300 ms every 85 ms) pick, left/right change, A changes too (or acts on
Restore All Defaults), LT/RT switch tabs, Y twice within 3 s restores the
tab's defaults, B or START close. The game's own pause-screen sounds: move,
change, tab, back.

### settings.ini by hand

settings.c checks a hand-edited file line by line (docs/decisions.md): a
value it can't read keeps the default, a number out of range takes the
nearest, buttons take GameCube names or the popup's (Fire, Jump...), and a
port with no Fire or no Pause is reset. What was wrong shows as a notice at
the top of the screen for 10 s, is written at the top of the corrected
settings.ini and in boot.log, and the file as it was is kept as
settings.bad.ini. The written file explains every key, with each button's
action beside it.

### Before (2026-10-27 to 2026-10-31)

Prime-X's settings were rows inside the game's own Options screens, drawn by
its GUI as OpenCrossing-Xbox's page is drawn like Animal Crossing's: extra
rows after each pause-screen category's retail rows (the right table
scrolled), the remap as a carousel in the three-cell value widget, a
description in the body pane, and on the title screen the Options frame
with Prime-X's four categories in place of the retail ones (five rows each,
no scrolling, value cells re-laid on a fixed grid after rounds hw6 and hw8
found them on the labels). It looked built in but was hard to follow, which
is why it was replaced.

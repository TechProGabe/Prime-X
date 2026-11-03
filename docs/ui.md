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
| the pause screen's Options | every category (Visor, Display, Sound, Controller) ends with an **Xbox Port Options** row above Restore Defaults; A opens the popup, the body pane says what it is. Display has only that row and Restore Defaults: Screen Brightness, Offset and Stretch did nothing on the Xbox (the GameCube's copy filter and VI) and were taken out (2026-11-02) |
| the title screen's Options | the same Display category (A on its row opens the popup); the other categories are retail |
| test builds | `env PX_MENU_AT=<frame>` in autopad.txt opens it at that frame (`tools/xbox/scenarios/menu`) |

The game is held while it is up (2026-11-02, below), and reads a neutral
pad until the popup's closing press is let go (`xsdk_menu_block`, pad.c),
so B doesn't also leave the game's menu.

### The card (`xbox/src/hw/xhw_menu.c`)

**Since 2026-11-02 it looks like Prime's own pause Options** (the user's
pick of four mockups, "match the game vibe better ... as light as
possible"): an amber hologram over the frame it opened on (read once at
open; ~22% of it through an amber tint, Prime's scanlines every third
row), chamfered amber frames on the card, the tabs, each row and the help
box, the title "[ Xbox Port Options ]" in the game's Deface Large and
everything else in Deface 14B with its outline (lifted from the disc
through the game's font loader; unscii-16 without it). Rows: the selected
one filled brighter; a value not the default in pale yellow with a mark.
The table below is the 2026-11-01 layout; the parts are the same.

576 x 416 in 480-line pixels, centred, drawn at the output's scale: 1x at
480, 3/4 as wide at 480 when the dashboard says 16:9 (the TV stretches it
back), 1.5x at 720p. All shapes are antialiased from distances and the text
is unscii-16 supersampled to the scale.

| part | what |
|---|---|
| glass | a cyan border, a navy gradient, scanlines, the scan visor's corner brackets |
| header | "[ Xbox Port Options ]" in Deface Large, centred; the mode now at the right under it ("720p 16:9") |
| tabs | Video, Controller, Gameplay, System between LT and RT badges; the current one lit (a Buttons tab, remapping on a drawn controller, was there 2026-11-01 to 2026-11-03) |
| rows | the label left; the value right between two arrows, with pips under it (how many choices, which one); a value that isn't the default is orange with a dot; a row that applies at the next start has a `*` |
| help box | what the selected row does (white), what the shown value does (cyan), a note (orange: "* Video Mode changes the next time Prime-X starts", "The dashboard doesn't allow 720p: the next start uses 480p", "Press Y again ...") |
| footer | A Change, B Close, Y Defaults, the D-pad Select |

**The game is held while the popup is up** (2026-11-02; beta2's testers:
"quite laggy", the user: "heavy and laggy, streamline the whole thing for
performance"). `OpenMenu` runs a modal loop at 60 Hz (port_menu.cpp
`RunModal`): the pads, the view, and what changed drawn by the CPU straight
onto the frame on screen (nothing is presented, so it stays); the alarms and
the card's, disc's and ARAM's completions go on, so the music's stream
keeps reading. The game's next present after the popup closes replaces the
card. Before, the game ran on behind the card, every frame waited for the
GPU and copied a ~1 MB cache of the card in, and a change was drawn 3 ms a
frame over several of the game's frames (at 720p's 30 Hz a move showed
~100-200 ms late). The cache is gone. The watchdog counts the loop's
frames as presents.

Nothing reads the framebuffer (write-combined). The card is drawn in 16-row
bands in RAM, where the shapes blend, then each band's opaque span goes to
the screen. Only what changed is drawn again (a move: the two rows and the
help box; a tab: the whole card), within 60 ms a frame (a guard; the first
console build stalled 336-581 ms a move, the music skipping). The drawing
itself got ~20x cheaper: the device pixels' logical positions come
from tables (no divide per pixel), each glyph's supersampled coverage is
made once per scale (~185 KB at 720p while the popup is up), lines use
floats, and the glass's two rims skip the inside the gradient covers, with
the scanlines folded into the gradient. `tools/xbox/bench_menu.py` times it
on the host (a P3 is ~34x slower): a move 0.25 ms at 480 and 0.5 ms at
720p, from 10 and 22 ms. The popup makes no sounds (the game's menu sounds
were the user's first suspect).

### The rows (`xbox/src/port/port_menu.cpp`)

| tab | row | values | what the box says the values do |
|---|---|---|---|
| Video | Video Mode `*` | 480i / 480p / 720p | every TV / sharper, component + dashboard / HD 16:9, component + dashboard, ~30 fps. Closing the popup with a new mode asks in a dialog of its own: **Later / Restart now** side by side (Later lit; a tester's idea), D-pad or left stick to pick, A OK, B Later. Now shows "Restarting Prime-X in ...", saves settings.ini and relaunches Prime-X |
| Video | Widescreen | Auto / 4:3 / 16:9 | follows the dashboard / the GameCube's shape / more to the sides (720p is 16:9) |
| Video | Widescreen HUD | Inner / Edges | where the visor's readouts sit at 16:9 |
| Video | Field of View | 55 / 65 / 75 | the GameCube's / wider / widest |
| Video | Frame Rate | Hide / Show | the counter top left |
| Controller | Controls | GameCube / Dual Stick (the default) | the button layout and aiming, the only control choice (no remapping since 2026-11-03). Both: X jump, B Morph Ball, Y missile, LT lock on, White map, D-pad visors, START pause. GameCube: A fires, hold RT and tilt the left stick to aim, the right stick picks beams, Black is the map too. Dual Stick: the right stick aims, the left one moves and strafes, **A or RT fires**, hold Black and tilt the right stick for a beam (GitHub #2, 2026-11-02; the row was "Aiming") |
| Controller | Aim Speed | Low / Medium / High | Dual Stick's turn at full tilt: 120 / 180 / 250 degrees a second |
| Controller | Rumble Strength | Low / Medium / Full | 40 / 70 / 100% (On/Off is the game's own row) |
| Controller | Stick Dead Zone | Low / Medium / High | 10 / 20 / 30% |
| Controller | C-Stick Dead Zone | Low / Medium / High | 15 / 25 / 35% |
| Controller | Trigger Click | Light / Medium / Firm | how far LT/RT go in |
| Gameplay | Skip Cutscenes | Off / On | START skips any cutscene, from the next room |
| Gameplay | Fusion Suit | Off / On | the GBA-link bonus, kept in settings.ini (`[game] fusion_suit`; the save doesn't hold it) |
| System | Screen Capture | Off / On | BACK saves shotNN.bmp (otherwise BACK does nothing) |
| System | Log File | Off / On | boot.log for bug reports |
| System | Restore All Defaults | A, then A again | every Port Options setting |

The rows call the same `PortXboxOptions::Get/Set` the old in-game rows
did, so each applies at once (Video Mode at the next start) and settings.ini
is written once, when the popup closes.

### Controls in the popup

The Xbox controllers as they are: up/down (D-pad or left stick; held, it
repeats after 300 ms every 85 ms) pick, left/right change, A changes too (or
acts on Restore All Defaults), LT/RT switch tabs, Y twice within 3 s
restores the tab's defaults, B or START close. No sounds (the user, after
the first console build: the music skipped; the cause was the redraw,
above, but the sounds stay out).

### settings.ini by hand

settings.c checks a hand-edited file line by line (docs/decisions.md): a
value it can't read keeps the default, a number out of range takes the
nearest. What was wrong shows as a notice at the top of the screen for 10
s, is written at the top of the corrected settings.ini and in boot.log, and
the file as it was is kept as settings.bad.ini. The written file explains
every key, and both button layouts under `aim`. An older file's button
lines (`a = A` ..., `lt`, `rt`, `trigger_z`) are skipped without a notice
and the file is written again without them.

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

# UI: boot splash and settings

Two pieces of UI are Prime-X's own. Both are specified by the user
(2026-10-27): the splash remixes Melee-X's "TechProGabe Presents..." card
in Prime's look, and the settings live inside the game's own Options screens
and look built in, the way OpenCrossing-Xbox's Options page does.

No game art, fonts or logos in either: shapes, colours and the unscii font
for the splash, and the game's own font and frames (read from the disc at
runtime) for the settings.

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
| task | what the boot is doing, at 2x: SCANNING, VERIFYING DISC IMAGE, LOADING, with the percentage; a 40-segment bar under it |
| detail | during the disc check: the file just checked with its CRC and OK/BAD, and a hex line of its bytes |
| notes | the check says why and how to skip it: "First boot only: checking every file so a bad copy is / caught now, not as a crash later. ~1 min. B: skip" |
| boot log | top left, typed on: CHOZO ARCHIVE INTERFACE, then what the boot found: the CPU and its measured MHz (UPGRADED X86 past 780), memory (128 MB held to 64), the disc, the quick check, the files (178/178, checked before, skipped, mismatch) |
| rain | glyph rain in phosphor green (hex, symbols, Latin-1 marks), white heads, stronger towards the edges, kept out of the card, the log, the tag and the hint |
| tag | a red `// SIGNAL` top right, blinking slowly |
| brackets | slide in from the corners (400 ms, eased), orange as they lock, then cyan |
| CRT | scanlines, darker corners, a band torn sideways every ~4 s, a 300 ms fade in |
| errors | the same glass with orange brackets, "PRIME-X // SIGNAL LOST", the title in orange-red, the lines in light cyan (`xhw_error_screen` before the game owns the display) |
| footer | "Hold BACK for safe video (480i)" |

Cost (the user: "should not interfere with the main goal"): the glass, the
card and a per-pixel shade (scanlines, corners) are made once; a frame
copies them, draws only what moves through the shade and is copied out,
~25 fps from a normal-priority thread: 7 ms a frame in xemu (`[SPLASH]` in
`boot.log` has the console's). Minimum hold 2 s, any button skips the
hold (never the work). Kill switch `-DXHW_NO_SPLASH`; `-DXHW_SPLASH_DUMP`
takes `[FBDUMP]`s of the settled screen and of a quarter-done stage.

## Settings, inside the game's Options

### The model: OpenCrossing-Xbox

OpenCrossing-Xbox's Options page (`xbox/src/xbox_settings_menu.c` there) is
drawn like Animal Crossing's own windows (the game's font, its colours, its
frame shapes), reached from the title screen and the pause menu, with tabs,
a help line for the selected row, unapplied values marked, and changes
waiting for Apply. Players can't tell it's not part of the game. Prime-X
gets the same result a better way: Prime's Options screens are data-driven,
so the Xbox rows go **into the game's own option tables** and are drawn by
the game's own GUI.

### Prime's Options screens

- **Pause > Options** (`src/MetroidPrime/CPauseScreenFrame.cpp`): categories
  Visor, Display, Sound, Controller (the fifth slot is Quit Game). Each
  category is a table of `SGameOption {option, stringId, min, max, step,
  type}`: sliders (`kOT_Float`) and On/Off-style choices
  (`kOT_DoubleEnum`, `kOT_TripleEnum`), labels from the game's STRG
  strings, values in `CGameOptions` (saved in the memory card file).
- **Title screen > Options** (`CFrontEndUI`, `SOptionsFrontEndFrame`):
  the same tables in the front end's frame.
- MetroidPrimePort already adds its own rows after each category's retail
  rows on the pause screen (`EPortOption`, from `kGO_RestoreDefaults + 1`),
  with labels in code because STRG has no entries for them, and the right
  table scrolls to fit. Its title-screen Options keeps the retail tables
  because that frame doesn't scroll.

### Prime-X rows

Added after the retail rows of each category, in both the pause and the
title-screen Options (`*` = applies after a restart):

| category | row | values | default |
|---|---|---|---|
| Visor | Skip cutscenes | Off (retail: only some can be skipped) / On (any cutscene skips, MetroidPrimePort's `PortSkipCutscenes`) | Off |
| Display | Video output `*` | Auto (shown as "Auto, 720p") / 480i / 480p / 720p; only what the dashboard allows | Auto |
| Display | Widescreen | Auto (follows the dashboard, shown as "Auto, 16:9") / 4:3 / 16:9; 720p is always 16:9 | Auto |
| Display | Widescreen HUD | Centred / Edge to edge (16:9 only) | Centred |
| Display | Field of view | slider, retail to retail + 20 degrees | retail |
| Display | FPS counter | Off / On | Off |
| Controller | Rumble strength | slider, 0-100% | 100% |
| Controller | Stick dead zone | slider, with a live stick readout | as Melee-X |
| Controller | C-stick dead zone | slider | as Melee-X |
| Controller | Trigger click | slider: how far L/R go in before they click (lock-on, free aim) | as Melee-X (230/255) |
| Controller | Buttons | opens a remap page in the same frame style, per port | GameCube layout |
| Controller | Screenshots | Off / On (Back saves `shotNN.bmp`) | Off (On in test builds) |
| Controller | Front LED | Off / On | Off |
| (every) | Restore defaults | the retail row also restores the Xbox rows of that category | |
| Display | Verify disc image | runs the full check (`docs/boot-safety.md`) | |

Rules for the rows:

- Drawn by the game's own frame (`CGuiTableGroup`/`CGuiTextPane`), in its
  font and colours, with the same highlight, sounds and slider widget as
  the retail rows. Labels and value strings live in code (`xbox/src/port`),
  in the game's case and tone ("Widescreen HUD", not "WIDESCREEN_HUD").
  Check every string's glyphs exist in the game's font.
- Choice rows with more than two values need a list type the retail tables
  don't have (`kOT_List` with code-supplied value strings); MetroidPrimePort's
  aspect row shows how it hooks in.
- The title-screen frame must scroll too (the retail rows plus ours don't
  fit). Do it the way the pause screen's right table does; if the frame
  can't, page the category instead. The title screen is where a player with
  a black screen fixes video, so its rows are not optional.
- A restart row asks first, in the game's own yes/no dialog (the one
  Restore defaults uses), then restarts the XBE (Melee-X's "Save and
  restart").
- Values are saved in `E:\UDATA\50580001\settings.ini` (Melee-X's
  `settings.c`: temp file, read back, swapped in), not in the game's save,
  so they work before a save file exists. The retail rows stay in the
  game's save as on the GameCube.
- Hold Back at boot: safe video, as Melee-X (480i, 720p and 480p off in
  `settings.ini`).
- `[MENU]` log line when the Options screen opens, with the rows shown.

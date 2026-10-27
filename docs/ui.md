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

### Prime-X: the same card, as a scan-visor readout

Keep the API, the timing, the skip and the safe-video hint. Change the look:

| element | Melee-X | Prime-X |
|---|---|---|
| background | navy to near-black gradient | near-black at the top to deep teal at the bottom (`#020608` to `#072a33`), like visor glass; faint scanlines (every 4th row ~6% brighter); a soft dark vignette at the edges |
| grid | none | a faint dot grid (every 32 px) that fades out toward the edges, drawn once |
| title | "TechProGabe Presents..." fading in | the same words in unscii-16 at 2x, cyan (`#7fe9ff`) with a 1 px dim-cyan glow, **typed on** a character at a time (~40 ms each) the way Prime's scan text writes itself, a block cursor blinking after the last character |
| framing | none | four scan brackets that slide in from the screen corners and lock around the title (~400 ms, eased); they flash orange (`#ff8a2a`) on locking, then settle to cyan |
| load bar | a bar under the title | a thin scan bar with tick marks: "SCANNING" at its left, the percentage at its right, filling as boot progresses; "SCAN COMPLETE" flashes once when it fills |
| first boot of a new disc image | n/a | the bar is the full disc check's progress: "VERIFYING DISC IMAGE 37%" (`docs/boot-safety.md`), with "B: skip" |
| fade | fade in | 300 ms fade in from black, fade to black when the game takes the display |
| footer | the safe-video hint | the same hint, dim cyan, bottom centre: "Hold BACK for safe video (480i)" |

The text, bracket and bar areas are redrawn each frame; the background and
grid are drawn once. At 16:9 the card stays centred in a 4:3 area with the
background filling the sides.

**Error screens** (no disc, wrong disc, failed check, fatal errors; Melee-X's
`xhw_error_screen`) take the same look: the same background, orange
brackets, the title in orange-red, the lines in light cyan, and a "B: quit,
A: continue" line only where going on is allowed.

Kill switch `-DXHW_NO_SPLASH`; duration `-DXHW_SPLASH_MS=<n>`. Screenshot
it with `[FBDUMP]` for the docs and the README once it exists.

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

# History: sessions and console rounds, 2026-10-27 to 2026-10-31

Moved here from `handoff.md` as they age (newest first): the
investigation notes of the first days, console rounds hw1-hw10 and the
round notes of hw16-hw18d. `handoff.md` keeps the current state;
`roadmap.md` has the console history in one table; `fps-plan.md` section
0 has rounds hw11-hw15's numbers; this PC's `C:\xemu\hw\prime\ROUNDS.md`
says where each round's logs and images are.

## Sessions 2026-10-31: 720p, stock drives, release candidate 1 (rounds hw16-hw18d)

Moved from `handoff.md` on 2026-10-31: its "Next session" items as they
stood when RC1c ran (the outcome is in the handoff and `roadmap.md`).

0. **720p for the release** (2026-10-31, the user: "im potentially
   releasing soon and 720p will be a big concern"). Gold's 720p free play
   (`C:\xemu\hw\prime\logs-gold-fix-pre`, an older build) hitched for
   two reasons: Auto switched between 30 and 60 Hz ticks (22-50 fps), and
   free memory sat ~3.8 MB below 480's, so ARAM swapped to the disk all
   along (432 chunks out, 346 read back, each read a stall). Done, xemu
   only: **720p runs 30 Hz ticks throughout** (480 keeps Auto: the user),
   and **the vertex ring lives in the video library's idle framebuffer at
   720p** (+1.5 MB free; `XGX_RING_IN_VFB`, decisions.md). Next: a console
   round at 720p to see the swap counts (`[AR]` lines) and `[MEM] low`
   against gold's log; then, if it still swaps, the ARAM reclaimer's
   choice of chunks. **Round hw17 ran on gold** (`logs-hw17`, 2f782e0):
   swapping 0-32 chunks out, none back (was 432/346); [MEM] low 0-5;
   but Main Plaza at 720p 13-15 fps (display lists decoded every call:
   the 6 MB texture pool had no room for spilled lists) -> 720p's
   texture pool 7 MB (xemu: as few decoded as 480). Every gold scene,
   480 too, froze ~6 s once at its room load and stalled >1 s ~18
   times, the game thread in ReadFile on the disc image (ARAM pages
   read through `ar.c` `read_pages`, synchronously): gold's E: is 456 MB
   free of 4881 (the ISO and aram.swp there, likely fragmented); red
   (F:, 3.6 GB free on E:) has ~1 such stall a scene. Other 720p scenes:
   pirates 27-30, Landing Site 31-49, Lava Lake 35-41, Phendrana 30-34
   (GPU 12-14 ms a frame: fill).
0a. **Round hw18 (RC1 candidate), 2026-10-31** (`logs-hw18red`,
   `logs-hw18gold`, `-a`/`-b` the failed gold starts). Red at 480 clean:
   pirates 38, Main Plaza 48, Lava Lake 44-45, Phendrana 59-60, no stall
   past a second, rain's GPU wait 1-2 ms. Gold at 720p: Main Plaza 13-15
   -> 36-40 fps (no lists decoded), stalls past a second ~18 -> 2. But
   gold also froze: twice with ARQ copies on the worker (frames stopped,
   the watchdog silent), and once churned its drive for minutes with the
   reclaimer's swap writes on the worker (80 chunks out, 23 straight
   back). Both backed out for RC1 (decisions.md "The hard drive stays off
   the game thread"); boot.log is written straight to disk in the first
   600 frames again (the frozen starts had lost their last lines in the
   queue). One more gold start crashed in `pb_init` (the write to
   0x80000000 that starts pbkit's pushbuffer, an access violation): the
   first in every round so far; the watchdog sent it to the dashboard.
   Round hw18b (c1e4fff): it hit red too (r18d) and gold again (g18e):
   3 in ~13 chained launches since hw18, none before. `xhw_page0_guard`
   (boot and before pb_init) now logs the PTE of 0x80000000 and claims
   physical page 0 if it is free (a contiguous allocation, highest
   0xFFF), so the store lands in memory of ours; a `[MEM] page 0 ... not
   mapped` line says which case it was. Round hw18c (724adc5): r18d and
   g18e crashed again, each the launch after Lava Lake, page 0 mapped but
   read-only and uncached (PTE 655/659, 663 on every other boot). The
   guard now also gives a read-only page 0 its write bit (claim first,
   then the PTE; checked in xemu with `env PX_PAGE0_RO=1`).
   Round hw18d (0ff5ed0, `logs-hw18dred`, `logs-hw18dgold`): all 11
   scenes on both ran through, no crash, watchdog or fault. Page 0 was
   writable on every boot this time (PTE 663, gold's g18e 64f, g18f
   66f), so the repair never had to act: not yet seen on hardware.
   Red's stalls are room loads and its shots (~0.7 s each); r18d's rain
   still ran ~1.65M streaks through the clip, ~3K without, ~60K culled.
   Gold: `aram.swp` grew 4 -> 8 MB once (65 ms); `[MEM] low` reclaims
   now 0-370 ms (were 1.1-1.7 s); the first world load ~4-5 s (red
   ~0.8 s); every screenshot froze it 47 s at 720p, 27 s at 480p (one
   WriteFile per row, each growing the file): the shot writer now sets
   the file's size first and writes 64 KB at a time. Gold fps (median of the
   5 s lines): Main Plaza 37, pirates 30, Landing Site 28, Lava Lake 34,
   Phendrana 32, Main Plaza at 480p 46.
0b. **Stock drives** (2026-10-31, the user: gold's drive is what most
   players have, little free space, "we should address it"). No hardware
   tests until truly needed (the user turned the consoles off). Done,
   xemu only: boot.log through a writer thread, and off in release builds
   unless Port Options > System > Log File is on (the user); the
   reclaimer's swap writes on the ARQ worker (decisions.md "The hard
   drive stays off the game thread", "No boot.log in release builds").
   xemu with every boot.log write slowed to 300 ms: the same stalls and
   frame rates as without; room sweep clean. Then the freezes (the
   user: "fix the freezes as much as you can without hw testing"): ARQ
   copies on the worker (on; the "no text" scare was the script's
   drift), the DVD queue 64 -> 1024 (the room-streaming freezes: xemu with
   40 ms image reads, 17 s -> none). Left: the first load into a world
   waits for its reads. hw17b (staged, not uploaded) predates all this:
   restage before the next gold round.
1. **Round hw16 ran on red** (2026-10-31, `logs-hw16`, all 11 scenes and
   their shots, no fault). Against hw15: pirates 34-35 -> 37-38, Main
   Plaza 43-44 -> 47-48, Lava Lake 44-45 (same), Landing Site 35-37 ->
   35-38, Phendrana visors 59-60 (vsync). Full LTO (r16h-i) and the
   16384-word kick (r16j-k) were no faster: dropped, ThinLTO and 8192
   stay. GPU rain (r16d against r16e): the CPU's render + draw 15 -> 10
   ms, the GPU's wait 1-3 -> 7 ms, so the same frame rate; since then
   each grid's streaks sorted into 4 x 4 columns, each column tested on
   the CPU (`VPRAIN_IN` without the clip, none drawn off the screen):
   xemu's Landing Site, a steady minute: 43% of the streaks clipped, 48%
   on the short program, 9% not drawn (was 100% clipped), ~35% less of
   the GPU's vertex work. Round hw17 (gold, 720p) carries it. [GPUT] in Phendrana:
   the pass before the first EFB copy is 7-10 ms of GPU a frame.
1. **M6a on JUMPs** (hw15: both pushbuffer tests passed on red; hw14's
   CALL failure fell on the boot's one context switch). hw15: Main Plaza
   38-39 -> 43-44, pirates 33 -> 34-35, Lava Lake 42 -> 44-45 with
   texture-only draws (`XGX_MAPS_FAST`). The back end (`xgx_draw`,
   `GXCallDisplayList`) is still the biggest cost in Main Plaza: record a
   world model's draws into a segment, jump to it with a patched JUMP back.
   Then: the game's own draw (render, 10-14 ms) is the biggest bucket ([PROFR] in
   hw12: the immediate-mode flush and rain in the fight, DrawSurface,
   texture binds and SetCurrent in Main Plaza); the mixer is still 13%
   of a fight; GPU rain (a vertex program with EXPP's fraction; the CPU
   line clip near the camera is the open question) and the visor passes
   (M5.6) are next in the plan.

### Fights, as sized on 2026-10-30 (Auto has run them at 30-32 fps since hw12)

2. **Fights**: hw10's r10d ran 17-18 fps at 3.4 ticks per render (sim
   ~12 ms a tick): Fixed's catch-up is most of the frame; Flex would give
   ~35. The user knows (2026-10-30, Fixed vs Flex explained: Flex runs
   one update a frame with dt = the frame's time, at most 1/30 s, so below
   30 fps the game slows instead; hw7 measured Main Plaza 31.9 -> 35.3 and
   Landing Site 34.9 -> 37.9 with Flex; never measured in a fight). A cap
   on Fixed's catch-up would be the other lever (the game would run slow
   in fights instead of dropping frames): the user's call. In: skinned lists over
   the dynamic budget go index-only; same-frame reuse of skinned vertices;
   per-model skin workspaces (`GXPortSkinKeep`, on since 2026-10-30, 1 MB,
   given back when memory runs short); `TriBoxOverlap` rejects on the
   bounds first. xemu pirate room profile (`39F2DE28:B9ABCD56`, 6 reports):
   `BuildBoneRange` 5.2 -> 3.4% of the samples, `dyn_fetch_batch` 6.7 ->
   5.4, `TriBoxOverlap` 2.2 -> 1.1. Most repeat calls left are other
   instances of the same model (other pose): nothing to reuse. Game
   Timing: Fixed runs 3.4 updates per drawn frame in xemu's pirate room
   at ~17 fps (`[PERF]`'s "ticks per render" works since 2026-10-30): in
   fights the catch-up is most of the frame; Flex runs one. The user's
   choice; worth telling them.
   Next lever, GPU skinning, sized 2026-10-30: models have 3-82 virtual
   bones (pirate-class 35-50; a virtual bone is one matrix a vertex, at
   most 3 joints blended on the CPU). The VP already takes a per-vertex
   matrix index (`a0` = PNMTXIDX, 10 position and normal matrices, used on
   the console by Melee-X), so a dynamic list can be drawn from the model's
   own arrays with each vertex's virtual bone as its matrix index and the
   palette (current PNMTX x bone, NRM x bone rotation) loaded per draw:
   no VP change. To handle: lists split into draws of at most 10 bones
   (strips become triangle lists: memory); `CSkinRules` (the bones) is
   shared by every instance, so `Calculate` must copy its pose's matrices;
   texgen from position or normal (reflections) wants skinned model-space
   values: CPU path; a vertex whose position and normal fall in different
   bones: CPU path; skinning itself can only be skipped lazily (sPointGen,
   morphs, anything that reads the workspace). Memory is the catch: the
   static skinned vertices must sit in the vertex pool (GPU-readable; full
   in hw9's fights) while the dynamic template stays for the CPU-path
   fallbacks, about twice a character's vertex memory on a console that
   already runs near 3 MB free there. Not started for that reason.

## FPS plan under way, as of hw12 (2026-10-30, afternoon)

The user: "execute the fps plan". `docs/fps-plan.md` section 0 lists what
landed and the rounds' numbers (hw11, hw12). Built and on red:

- M0: the mixer's share in [PERF] and [PROFA], the game's draw apart from
  the ticks ("render", `ms per tick`, [PROFR]), stats at scene ends.
- M1: **Game Timing: Auto is the default** (30 Hz ticks drawn at 60 with
  MetroidPrimePort's interpolation when the frame doesn't fit; particles
  blended over two-frame steps). Since 2026-10-30 the only mode (the
  user removed the Port Options row; Fixed/Flex as test hooks only).
- M2: the mixer's ramps, resampler, ADPCM and reverb, bit-exact
  (`tools/xbox/test_mixer.py`).
- M4: **GPU skinning** (gx_vtx.c `gs_*`, `GXPortSkinGpu`): characters
  drawn from their bind pose with a matrix per bone, reflections through
  per-bone texture matrices, a 1 MB vertex-pool budget, CPU fallback.
- M3/M5 pieces: draw merging, `GXSetArray` no-op, adaptive pushbuffer
  kick, rain grids tested once, the minimap's positions in one call,
  `XboxPresentBlend` once a frame, the sorted lists' coordinates in list
  order (`BuildNearList`).
- Compiler: **`-O3` by default** (hw13: 1-4% ahead of -O2 in every scene).

Red, Auto: fight 17-18 (hw10, Fixed) -> 30-32 fps (hw12), pause screen
46.5 -> 58-60, Lava Lake 28 -> 40, Main Plaza 35 -> 36-37 (the draw work
there is the limit). No audio fault in 21 chained scenes (hw11, hw12).


## Console round hw10 (red, 2026-10-30)

Chain r10a-h on the profiler build (`tools/xbox/scenarios/hw10/README.md`),
logs and shots in `C:\xemu\hw\prime\logs-hw10` (the save folder pulled
whole: each scene's log is `boot_Prime-X-r10x.log`, its shots
`shot_Prime-X-r10x_NN.bmp`); the first attempt's r10a in `logs-hw10a`;
the save folder before the upload in `logs-hw10pre`. Maps
`prime_x.hw10.map` (profiler), `prime_x.hw10ship.map`. No free play (the
user), so the pause screen, the gunship and the pickup hitches weren't
measured; the console still has `Prime-X` (ship) and `Prime-X-prof`
(profiler, no script) for a later session.

- **The landing glow box is gone on red** (r10b shot 14: a starburst
  fading out, as Dolphin), and the lightning-lit clouds fade (shot 13):
  the fog-colour and spot-light fixes hold on hardware.
- **Visors on red** (r10f): thermal's heat palette and X-ray's lavender as
  in xemu, 48-60 fps; the DEPENDENT_GB unit runs on hardware, no fault.
- fps against hw9: Landing Site (r10c vs r9b) mostly 34-59, lowest 24
  (hw9 dipped to 17-22); Main Plaza (r10e vs r9c) mostly 30-36 (hw9
  24-32), a dip to 18-20; Frigate opening (r10h vs r9d) 50-60, as before;
  Lava Lake (r10g) 27-28.
- **Fights** (r10d, Frigate Crash Site pirates): 17-18 fps in the fight,
  3.2-3.5 ticks per render: sim 38-41 ms a frame (~12 ms a tick) plus
  display lists and draws ~16 ms. Under Game Timing: Fixed the fight is
  the catch-up (71% of the profile's samples in the simulation); one tick
  a frame would be ~28 ms (~35 fps). Flex is the user's lever; capping
  Fixed's catch-up would slow the game down instead (a choice for the
  user). After the fight, 33-36 fps.
- Room-entry stalls of 0.3-1.3 s in every scene (`MP stall`), as before;
  the [PROFT] frames (150 ms or more) were ~65% waiting (the game thread
  not running: disc reads), not computing. Lowest free memory in the
  chain 3.7 MB; no fault, hang or out of memory in eight scenes.
- **Audio stuck** ("Sound hardware is stuck"): the first attempt's r10a
  (the console switched off and on before, previous exit recorded as an
  in-game reset) and, in the second attempt, every scene from r10c on.
  r10a (after a power-off) and r10b played; r10c's boot found the AC97 as
  idle as r10b's had (the same registers in `[AUDIO] found`), yet its
  engine never left descriptor 0, through six restarts and a cold reset.
  It began at the relaunch from r10b, which exited at frame 700 in the
  middle of the landing cinematic (lockstep; streamed audio not started).
  An old fault (Melee-X's console A on 2026-10-03, hw9 r9e): the codec
  stops taking frames and only a power-off brings it back, so it isn't
  the bus masters (reset each boot) and likely not the AC-link cold reset
  either (no effect). Next: compare what each XBE leaves at exit (the
  engine running or not, `xhw_audio_shutdown`), and try a warm reset
  (global control bit 2) before the cold one; codec register reads froze
  the CPU twice (hw7, hw9), so not those.


## Session 2026-10-30 (day): Dolphin references, rendering fixes, hw10 staged

- Docs consolidated: the sessions and rounds of 2026-10-27..29 moved to
  `docs/history.md`; `renderer.md`'s status column filled in; the
  roadmap's known issues split into open and fixed-waiting-for-hw10.
- `tools/xbox/dolphin_ref.py`: retail frames from Dolphin in any room, no
  save or input (Gecko codes: the front end goes to the game, the Frigate
  becomes the room asked for; `--visor` holds a visor). Dolphin movies
  were tried for input first: with one playing this build drew nothing.
- **Fog colour on additive blends**: retail's `CGX::update_fog` sets the
  fog colour black while the blend's destination factor is ONE, through
  `GXSetFogColor`, which was a no-op here (`prime_sdk.c`). Fog's colour
  was added over every additive effect in fog: the landing cinematic's
  glow drew in a light square (xemu lockstep frames 450-452; the user saw
  it black on red in hw7, which this doesn't explain: check on red). Now
  in `gx_state.c`.
- **Spot lights**: GX clamps cos(L, dir) at 0 before the angle polynomial
  (Dolphin's LightingShaderGen); the VP didn't, so a cos^2 term lit the
  back. The lightning-lit clouds in the landing cinematic ended on a hard
  polygon edge; now they fade as in Dolphin. `tests/xbox/vp_ref.c` got
  the same line (its header says so) and `legacy_len`'s spot count went
  13 -> 14; `test_vp_opt.py` 200000 keys, 0 failed.
- **Thermal and X-ray palettes**: both visors copy the frame as intensity
  (I4, I8) and draw the copy back as C4/C8 through a TLUT (thermal's heat
  palette, `TXTR_XRayPalette`); the port sampled the copy as plain
  intensity, so the thermal gun drew white and the door's outline black,
  and X-ray drew grey. A CI view of an EFB copy now gets a TLUT texture
  (`XgxMap.dep`, gx_tex.c `dep_palette_for`) that a DEPENDENT_GB unit looks
  the copy's intensity up in (nv2a.c `UNIT_DEP`). Intensity copies now use
  GX's formula (Y = (66 r + 129 g + 25 b) / 256 + 16, Dolphin's
  TextureConverterShaderGen), since the value is an index now. Thermal
  matches Dolphin; X-ray's palette is inverted (dark -> pale lavender,
  bright -> navy), so X-ray now looks pale lavender in dim rooms. Dolphin
  can't give an X-ray reference with `--visor` (the world pass starts on
  a real visor change in `CPlayerVisor`).
- Room sweep 14/14 after each of the two commits (no FATAL, THROW, out
  of memory); the shots
  differ from the night's by timing only. Every sweep room matches
  Dolphin's frame of it but Magmoor's fog volumes; Ice Temple's and the
  quarry's odd poses (gun raised, left hand up) are Samus's idle
  animations, in retail too.

## Session 2026-10-30 (night, unattended): fights, xemu only

- The kept-workspace mismatch (why `GXPortSkinKeep` was off) is xemu's:
  the verify build checksummed each kept workspace after its skin and at
  every draw (never changed), hashed the fetched arrays around each call
  and logged MXCSR: the same array bytes and MXCSR gave two references a
  bit apart in a packed normal, back to back ("arrays changed during the
  call ... array bytes the same"). On since; a reclaimer gives the
  workspaces back when memory runs short (`GXPortSkinKeepEpoch`; test
  hook `env PX_SKIN_REVOKE_AT=<frame>`: pirate room renders the same
  before and after, verify clean).
- `TriBoxOverlap`: the triangle's bounds tested before the edge axes.
- A skinned list's position and normal fetches in one pass
  (`dyn_fetch_pos_nrm`).
- `CHierarchyPoseBuilder::BuildTransform` resumes from the deepest chain
  link already walked under the same tree version (its own samples 0.8
  -> 0.1% in the pirate room). `prof_report.py`'s "one frame up" table
  scans the stack for return addresses and catches stale ones (it showed
  3.7% here, 8-10% "under" `RecursivelyBuild` in Main Plaza): judge by the
  functions' own samples.
- `[PERF]`'s "ticks per render" counts the game updates (was always 0).
- Particles: the last lifetime's reciprocal kept (emulated FRECPE + two
  fma calls per `PortSetSystemTime`); `SqrtF`/`InvSqrtF`'s max without
  nxdk's x87 `fmaxf`. `RecursivelyBuild` makes its children's matrix once.
  `CCubeModel`'s next-surface prefetch takes the header's port line too.
- PGO profile retrained (all five scenarios gave profiles); LTO + PGO
  autopad build: room sweep 14/14. Plain build sweep 14/14 too.
- Soaks in xemu: pirate room 25 min with 5 MB held back (free steady
  ~3.5 MB, 32.8k frames), Main Plaza with 7 MB held back (reclaims and
  ARAM swap working until Samus died, no invulnerability), Phendrana's
  Ice Ruins and Magmoor's Lava Lake ~18 min each on the LTO + PGO image:
  no FATAL, THROW or out of memory.
- The pause screen (inventory doll, hw8's 14-17 fps): 30 dynamic lists, 0
  volatile, 0 index-only, ~40-46 fps in xemu.
- Profiler: `[PROFT]`, hitch frames only (150 ms+), with their waiting
  time. In xemu a room's first frame under `MP_BOOT_WORLD` is mostly
  waiting (`CGameArea::Validate` loads the area synchronously, a boot
  artifact). The console's free-play hitches (hw9: 250-300 ms "draw"
  stalls at pickups and some doors) aren't explained yet: a `[PROFT]`
  profile from the console would say whether they wait or compute.
- Tried and reverted (no gain xemu can show): the sorted lists' sort keys
  mirrored beside their ids (the nodes fit the Pentium III's L2, and xemu
  doesn't model cache misses); an SSE `put_nrm` (bit-identical by a host
  test over 20M inputs, but no faster in xemu, which runs packed SSE a
  lane at a time). Both might help on the console; unmeasurable here.
- Looked at, left: the landing glow box (fixed the next day: fog colour
  on additive blends), the HUD minimap
  (reordering fills and outlines changes overlap), pbkit's kick size
  (tuned on the console), `acos`/`sin` in `CQuaternion::Slerp` (nxdk's
  x87 versions; ~1%, results would move).

## Session 2026-10-29 (evening): hw9 read, FPS from the logs, xemu only

Round hw9's logs: `C:\xemu\hw\prime\logs-hw9` (chain r9a-r9e,
`boot_freeplay.log` = the user's ship-build play from a new game through
the Frigate to Parasite Queen, map `prime_x.hw9ship.map`).

What hw9 said:

- The user: Morph Ball and textures fine ("visually the game works
  great"); fps "really bad" fighting pirates on the Frigate up to Parasite
  Queen. The free play's slow windows (10-20 fps) have up to 16 skinned
  lists over the dynamic budget ("no dynamic form": parsed and decoded from
  the list every call, display lists 15-28 ms), the display-list cache full
  (2048 entries, the vertex pool too) at times, and the catch-up ticks
  (Fixed) multiply the sim on those frames.
- **r9f froze** at `[AUDIO] found:` on the codec register reads before the
  idle step (hw7's freeze was the first read after the reset). Fixed: no
  codec reads on the console at all (`xhw_audio.c`). r9e's audio was stuck
  ("silent until the Xbox is switched off"): roadmap known issue, not
  fixable without a console.
- Chain fps against hw7: front end about the same, Landing Site and
  Phendrana a little up (hw8's prefetches), Main Plaza flat. Profiles:
  memcpy's small-copy `rep movs` 8% (Main Plaza), the dynamic lists' fetch
  ~6%, the world-model cull loop ~3%, the object-list walks ~4%, snow
  ~10% (Phendrana), skinning 3-4%.

Done (`docs/performance.md`, "2026-10-29 (evening)", has the details;
commits `70a4294` .. the session's last):

- `OSGetTime`'s sawtooth (a 12.3 ms jump every second: an extra
  simulation tick, a hitch a second) and its two 64-bit divisions; lockstep
  frames now exactly 1/60 s of OS ticks.
- memcpy/memset small paths, vertex copies as words.
- Dynamic lists: fast fetch loops; **index-only dynamic lists** when a
  template doesn't fit the budget (own 256 KB allowance, big batches through
  a reserved buffer up to 128 KB); the vertex plan cached per format; a
  failed build backs off 30 frames. Verify: 0 differ (Landing Site, Main
  Plaza, Phendrana, Frigate, pirate room; also with the budget forced down
  so lists go index-only: `-DDLC_DYN_BUDGET=196608u -DDLC_DYN_EXTRA=0u`).
- Snow quads in one call (`GXPortSnowQuads`; verify: 4M quads, 0 differ),
  `GXTexCoord2u8` & co. fast path.
- Prefetches: world models (cull loop), entities (state manager walks),
  skinning streams, model surfaces, particles. `derive_vk`'s light scan
  cached until a light changes; the HUD's interference check cached.
- Skinning: a pose already in a model's own workspace isn't skinned again
  (`PortSkinHash`); the skinning reports the buffers it writes
  (`GXPortSkinWritten`, and `GXPortSkinWrittenThisFrame` for the shared
  ring and the swarms' buffers) and a dynamic list drawn again in the same
  frame from unwritten buffers reuses the vertices already in the ring.
  xemu Main Plaza: 9600 of 11400 repeat calls reused, verify 0 differ;
  pirate room 0 differ.
- Per-model skin workspaces (`GXPortSkinKeep`): written, off (on since
  2026-10-30).
- The audio codec is no longer read on the console (r9f's freeze).

The room sweep (plain and LTO + PGO) passed 14/14 on `7bd3b85`; later
commits were checked with verify runs, not a sweep. Game Timing: Flex
measured +10% over Fixed on red (hw7/hw8) and is the user's own choice in
Port Options.

## Session 2026-10-28 (night, unattended): fixes in xemu, not on red yet

The user's list: fix the hardware crash (the hw6 Out of memory screen),
the white gun ("Chozo Ruins starts on the white gun every time", flickers
in some rooms), everything else known, and fps. Priorities they gave:
stability > visuals > fps; memory may be traded, the retail look may not.
No hardware rounds until they say.

- **Out of memory** (`451e1bf`): memory-pressure reclaimers
  (`xhw_mem_reclaim`); below 3 MB free the game thread gives back up to
  1 MB a frame, and a failed commit asks again (try-lock only) before the
  Out of memory screen. ARAM is the reclaimer: chunks wholly on the disc
  go at once, the rest after a write to `aram.swp` in the save folder
  (`ar.c`). Test builds: `env PX_MEM_BALLOON_KB=<n>` holds RAM back; Main
  Plaza with 7000 ran down to 0.9 MB free with no failure. ~0.9 MB of
  `.bss` less (GX FIFO, random static, pause blur buffers).
- **Morph Ball washing the scene out** and part of the white gun
  (`451e1bf`): `CGX::SGXState::x4c_flags` was a PowerPC-ordered bitfield;
  `CGX::SetNumChans` never reached GX. Shots: `build-xbox/shots/morph4`
  (before) vs `morphfix_009` (after).
- **The white gun**, three causes: (1) `b858563`: five textures on the
  gun with its world shadow, four units; the stage without one read
  white. Uniform textures are now combiner constants; an overflow reads
  the texture's average. (2) `7dbd64b`: a texture a texmap still held was
  released as "idle" after 600 frames, because `CTexture::Load` doesn't
  load again what it thinks is still bound (true on a GameCube); the
  gun's reflection map in a quiet room went, and the gun read white "a
  while into the room" (Chozo's save corridor at frame 1500 in xemu).
  (3) the channel count (`451e1bf`, below). Shots:
  `build-xbox/shots/plazagun_00*`, `savegun_000`.
- The arm cannon vanishing in a room after a while is retail: after 105 s
  without firing Samus lowers it out of view (`CFidget`, `kS_HolsterBeam`,
  `x30_timeUntilHolster`); firing brings it back. xemu runs that stand
  still hit it (Lava Lake from ~frame 2600). Probably hw1's "arm cannon
  missing in Main Plaza" too.
- xemu slot 1's virtual memory card has the Fusion Suit bonus on (an
  earlier test): its runs show Samus's left hand green, slot 2's orange.
- **Display-list thrash** (`b858563`): only lists unused for 3 frames are
  evicted (hw6's 34 ms of display lists with the vertex pool full).
- `xhw_time_ns` reads the ACPI timer every 100 ms and interpolates with the
  TSC (was an I/O-port read and three 64-bit divisions per call, ~3% of
  the console's CPU in Main Plaza).
- Display-list CPU (`ad202e8`): `DlEntry` hot fields in two cache lines,
  normals packed as CMP (13% more lists fit the vertex pool), rarer
  content checks for long-stable lists, perf buckets inline, no PBR-record
  parse per material, batch tables prefetched. The texture overflow pool
  is a memory reclaimer (Prime never let it go).
- **Port Options > Game Timing: Fixed / Flex** (`f2e78fd`): Flex runs one
  game update per drawn frame (MetroidPrimePort's adaptive mode, dt at
  most 1/30 s); Fixed (default) is 60 a second with catch-up, ~1.5 updates
  per frame at ~40 fps on red. Likely the biggest fps lever left, but
  xemu is too slow to show it: **ask the user to try Flex on red** (and
  watch for physics/timing oddities: MetroidPrimePort calls the mode
  experimental).
- Gate 2 shown in xemu: doctored images (wrong ID, other revision,
  trimmed, a changed file) each stop at their screen; the code repair
  self-test. Fixed on the way: the error card ran off the screen, the
  mismatch card said "1 files" and offered the dashboard under its A/B.
- Options: "FPS Counter" is "Frame Rate" (ran past its button); the
  title's value tables sit at the selected row's own offset.
- A review of the new memory and clock code found races, fixed in
  `ebd001b`: a refill could write a chunk a reclaim had just freed; one
  failed swap write stopped swapping for good; the clock could go
  backwards across threads. Left knowingly: the mixer can read a few ms
  of zeros from a chunk being refilled by another thread (only under
  memory pressure; recently used chunks are reclaimed last).
- PGO retrained on today's code (`xbox/prime.profdata`, 2026-10-29):
  Tallon, Frigate, Phendrana, Magmoor and Chozo all gave profiles for the
  first time (the instrumented build no longer runs out of memory). The
  ship build (`XBOX_LTO=1 XBOX_PGO=xbox/prime.profdata`,
  `build-xbox-lto-pgo/xbe/default.xbe`) boots to the title in xemu. A new
  profile now restarts the -pgo build folder by itself (`xbox/build.sh`).
- Fog volumes are left out (`PostRenderFogs`): they need real depth from
  their Z16 copies, ours are masks, and they drew as solid boxes (the
  peach rectangle in Twin Fires). Proper ones need depth copies that
  carry depth: a GPU path to try on a console.
- Host test `test_aram.py` for the ARAM swap (400k random operations).
- nxdk's printf has no `%f` (prints nothing, shifts the arguments): the
  Threat Assessment distance digits were empty and the `port: frame N at
  X s` milestones garbage. Both use the SDK's `xsdk_snprintf` now
  (`ee76d4f`). The room sweep refuses a non-autopad image (a ship image
  sits on the title: the first ship sweep took 40 minutes for 0 shots).
- The LTO + PGO ship code (built with `-DXHW_AUTOPAD=1`) passes the room
  sweep: all 14 rooms render, the Chozo arm cannon is dark metal. Again
  after the display-list changes and the retrained profile (`2623192`):
  14 rooms, no FATAL, THROW, texture drop or out of memory.
- A review (subagent) of `713d3fd` found nothing; the dynamic path's
  verify was also run in the Frigate (0 differ).
- FPS, from round hw5's `[PROFL]` (memcpy's callers) and buckets:
  dynamic (skinned) lists stage each batch through an L1 scratch buffer
  instead of fetching into the ~800 KB template and copying all of it;
  one-draw lists keep their draw in the entry (no batch table miss);
  `emit_textures`' 40- and 36-byte compares inline (were memcmp calls).
  `-DXGX_DYN_VERIFY=1` checks the dynamic path against the old one: no
  draw differed (Landing Site, Main Plaza). Main Plaza: 1633 of 1933
  cached lists are one draw, 4 layouts in all. Not measured on red.
- **Save round trip in xemu** (`eb03882`): a save station offers nothing
  to an `MP_BOOT_WORLD` game (no card serial: retail); `env
  PX_CARD_FILE=0` makes it card file A. Save Station 1 (Chozo, Samus
  starts on the pad: step off, step back) saved; a fresh boot's main menu
  listed "[Samus A] Chozo Ruins" and Continue loaded it there.
  Scenarios: `tools/xbox/scenarios/save`, then `load` on the same slot.

## Session 2026-10-28 (night and morning)

- **Particles' texture coordinates**: CElementGen writes each quad corner's
  TEX0 (two S8s, one fraction bit) with `GXTexCoord1s16(0x0202)`; it was
  taken as one 16-bit coordinate, so every particle's t was 0 and its
  texture's top row stretched across it in stripes. A 16-bit write now
  fills the vertex's next two bytes whatever the call is named (`gx_vtx.c`
  `put16`; it also covers the rain's RGBA4 colour, `color16_next` before).
  This was the landing cinematic's "washed out white with vertical streaks"
  (round hw1): the ship's steam clouds, close to the camera.
- The landing's remaining white frames are single frames (shots 2, 7, 23
  of a 20-frame-step run, `/c/xemu/px/white.log`) whose last draw is an
  untextured full-screen additive quad after the letterbox: a camera-filter
  flash, the storm's lightning (drawn at full strength: the GXInit
  channel bug, fixed in the afternoon).
- Rain and snow grids wholly off screen are skipped
  (`CEnvFxManagerGrid::Render`, `GXPortBoxCulled`); the line clipper sorts
  by outcode first; `put16` decodes the RGBA4 colours and S8 texcoord pairs
  inline. Landing Site in xemu ~27 -> ~30 fps (LTO + PGO). The PGO profile
  predates these: retrain before a release (`docs/pgo.md`).
- **Lines clipped on the CPU** to the near plane and a 2x guard band
  (`gx_vtx.c` `clip_lines`): the rain lines with an end behind or just in
  front of the camera came out flipped or ~300,000 px long.
- Trace builds (`-DXGX_DEBUG_TRACE=1`) print each traced draw's first four
  vertices (position, tc0, colour) and A8R8G8B8 textures' average and alpha
  range.
- Every particle draws its whole texture now, so rooms look busier than
  before (steam, mist, sparks): compare with Dolphin before calling any of
  it a bug.

- The picture's aspect follows the game's EFB (`VIConfigure` ->
  `xgx_set_efb_width`): no more 9% squeeze and side bars at 480, full 16:9
  at 720p, letterboxed 16:9 on a 4:3 screen. 720p tested in xemu (slot 3,
  `docs/testing.md` "720p in xemu").
- Display-list cache: entries keyed by the arrays' layout too (the Frigate's
  skinned lists stopped going volatile); lists spill into the texture
  pool's free space (Main Plaza 7 -> ~29 fps in xemu).
- Movie decode twice as fast (integer IDCT, whole-tile copies): the front
  end 12 -> 26 fps in xemu.
- Skip Cutscenes (MetroidPrimePort's patches), Fusion Suit, descriptions
  under the pause screen's Prime-X rows, B is back in menus, Port Options on
  the main menu (the Fusion bonuses row), three-choice rows only.
- Streamed music (the AI path) mixed into MusyX's output.
- Test hooks: `env PX_ALL_ITEMS=1`, `PX_SKIPPABLE=1`, `PX_WIDESCREEN=0|1|2`.

## Console round hw8 (red, 2026-10-29)

Logs `C:\xemu\hw\prime\logs-hw8` (the round index: `C:\xemu\hw\prime\ROUNDS.md`);
the user's free play is `boot_freeplay.log` (ship image, map
`prime_x.hw8ship.map`). Only r8g of the chain ran.

- r8g (Landing Site, Flex) against r7g: median 37.9 -> 40.2 fps, lowest
  tenth 20.5 -> 22.5, sim 19.3 -> 18.2 ms, display lists 7.2 -> 6.7 ms;
  memset gone from the top ten (2.1%), memcpy 9.0 -> 8.0%.
- Free play, 285 s: **no out of memory** where hw6 ran out (one `[MEM] low`
  reclaim, 16 chunks to the swap file). The user: the visor steam looks
  good.
- **The pause screen at 14-17 fps** (display lists 40-48 ms a frame): six
  of Samus's lists went volatile ("no dynamic form": the dynamic budget,
  1 MB, was full and its extra 1 MB needs 6 MB free; there were 4.6) and
  were decoded on every call. The extra now needs 4 MB free (still a full
  extra above the reclaimers' 3 MB).
- **Charge beam dead** (the user): `CPlayerState::HasPowerUp(kIT_ChargeBeam)`
  asks `PortAp::ChargeBeamRule` for an owned Charge Beam, and the generated
  stub said false. Hand-written now (`port_overrides.cpp`: true). The 120
  generated bool stubs were checked for others whose "off" isn't false:
  none breaks retail (SpringBallRule's 0 is "no Spring Ball", retail).
- **Port Options still misaligned on the console** (the user: "remove the
  bar, centred and well spaced"): the frame's value cells differ in box,
  justification and height, so a value's place depended on its width.
  Every cell now gets worker 0's box, centred both ways, on fixed points
  around one centre between the columns, dividers hidden; the shared
  frame's retail rows get their cells back (`CPauseScreenFrame.cpp`).

## Console round hw7 (red, 2026-10-29)

Chain `tools/xbox/scenarios/hw7` (profiler builds of `4473399`), logs and
shots in `C:\xemu\hw\prime\logs-hw7`, map `prime_x.hw7.map`; what was on
the console before (the save, settings, hw6's xbe) in `logs-hw7pre`.

- fps at 480i against hw5 (median, lowest tenth): front end 42.6 -> 48.2
  (33 -> 43), Landing Site 28.2 -> 34.9 (15.5 -> 16.8), Main Plaza 27.8 ->
  31.9 (22 -> 25; sim 22.2 -> 18.4 ms, display lists 7.8 -> 5.7, draw 7.1 ->
  5.8). `memcmp` 2.5% -> 0.8% in Main Plaza, `memcpy` 10.2% -> 8.8% in the
  Landing Site. The Frigate isn't comparable: the script is by frame, and at
  the higher fps frame 5000 came a minute earlier in game time, still on the
  landing deck (hw5 was inside, ~2000 draws).
- No `[MEM] low`, no swap, no texture drop, no warning in r7a-r7d. The arm
  cannon in Main Plaza is dark metal. One of twelve landing-cinematic shots
  is washed grey (hw5: four of nine).
- **r7e froze** (black screen, no reboot, FTP down 40 minutes until it was
  rebooted) at `[AUDIO] step: codec power-up`: the first codec
  register read after the AC-link cold reset, the CPU held on the bus.
  hw4's r4c froze around the same reads. Fixed in `f73d0d7`: on the
  console, no codec access after the reset (it never had anything to power
  up there). Melee-X has the same code.
- r7e-r7g rerun on the fix (`stage-hw7b`, map `prime_x.hw7b.map`): each
  logged `codec not read after the reset`, played audio (no `AC97 stuck`)
  and chained on; back to the dashboard. Phendrana 41.8 -> 45.3 (p10 36 ->
  38). Game Timing: Flex against Fixed on the same build: Main Plaza 31.9
  -> 35.3 (p10 25 -> 27, sim 18.4 -> 16.7 ms), Landing Site 34.9 -> 37.9
  (p10 17 -> 20.5, sim 23.6 -> 19.3). No memory warnings; shots right.
- **The user's note:** in the Landing cinematic, as the gunship comes into
  the atmosphere, a black box around its glow texture. r7b's frame 600 shot
  (`shot_Prime-X-r7b_01.bmp`) shows the quad's square around the sprite.
  Lead (xemu trace build, lockstep, `/c/xemu/px/ab-landtrace3`, frame
  460, ship still a few pixels): the ship's engine particles are additive
  (`blend 1 4 1`, alpha = texture x vertex) with the area's fog on (type 2,
  colour `6d7e85`) and a 64x64 DXT1 texture. Fog replaces the colour, not
  the alpha, so any texel whose alpha is 1 adds the fog colour: a square
  where the texture's edge blocks are opaque. Check those blocks' alpha
  (CMPR's punch-through after the DXT1 conversion) and the fog factor at
  that distance against Dolphin. In xemu the moment lies between lockstep
  frames 460 and 465 (`PX_LOCKSTEP=1`; frame 465 is already in the cloud).
  Also seen: in the cloud whiteout (lockstep 590) the letterbox goes grey:
  the bars are a camera filter pass and the whiteout's additive full-screen
  pass is drawn after them, so probably retail; compare with Dolphin.

## Console round hw5 (red, 2026-10-28)

Chain `tools/xbox/scenarios/hw5` (five profiler builds, ThinLTO + PGO with
the retrained profile), logs and shots in `C:\xemu\hw\prime\logs-hw5`,
map `C:\xemu\hw\prime\prime_x.hw5.map`. The old test folders on the
console (`Melee-X-r14*`, `-r15*`, `Prime-X-r1*`, `-r4*`) were removed at the
user's request; `Melee-X` and `Prime-X` (free play, the disc images) stay.

- Ran start to end and returned to the dashboard by itself; no hang, and
  the audio start-up freeze of hw4's r4c didn't recur.
- fps at 480i: front end 32-45, Landing Site 37-46 (hw4 27-41, hw1 ~10),
  Main Plaza 21-46 (hw1 ~5; low while it streams in), Frigate 26-43,
  Phendrana 36-43.
- Landing cinematic: the vertical streaks are gone (particle texcoords).
  4 of 9 shots are washed out white, letterbox bars too: the full-screen
  additive camera filter (lightning?). **Next:** read its vertex colour and
  alpha. Trace builds now print vertex colours for untextured draws; a
  dense lockstep run of frames 640-1400 every 10 (`/c/xemu/px/scen-one`)
  was started and stopped unfinished at the end of the session. If the
  game's alpha is small and the frame still goes white, the blend is at
  fault; if the colour itself is near-white at full alpha, it's the game's
  flash (compare with Dolphin).
- Profile, Landing Site: `GXCallDisplayList` 13%, `memcpy` 10%, the rain's
  per-vertex path ~11% (`GXTexCoord1s16`, `GXPosition3s16`,
  `gx_vtx_flush`), `CEnvFxManager::Update` 3.5%, `BuildBoneRange` 3.3%.
  Main Plaza: display lists 13%, `xgx_draw` 8%.
- Watchdog: a busy stall (the boot screen) over 5 minutes now counts as a
  hang, so a freeze there reboots to the dashboard like any other.

## The first build session (2026-10-27)

Then: the game booted in xemu to the title screen and the file select.
Done that session:

- Phase 0.1: `tools/xbox/msys/build.sh`, `xbox/build.sh`, Docker files and
  Melee-X's tools copied and renamed (`PX_` knobs, title `50580001`). MSYS2
  build works on this PC. **Docker is not installed here**: the Docker half
  of gate 0 is unchecked (CI will cover it, phase 7).
- Phase 0.2: the C++ spike decided **one triple (nxdk's `i386-pc-win32`,
  MSVC ABI, nxdk libc++)** (`docs/decisions.md`). `spikes/cxx` holds it.
- Phase 0.3/0.4: `tools/port_blocks.py` -> `docs/port-blocks.md`; all
  `TARGET_PC` blocks kept, Xbox `Port*` hooks (`docs/decisions.md`).
- Phase 1: game, MusyX, aurora headers imported at `673eacaf`; Melee-X's
  `xbox/` imported (Melee-only files set aside: `card_endian`, `menu.c`,
  `features.c`, `simhash.c`, `stubs.c`; `thp.c` in `xbox/src/sdk/later/`);
  every unit compiles; `default.xbe` links (5.2 MB).
- Phase 2 (part): boot (`xbox/src/sdk/boot.c`), frame boundary
  (`prime_sdk.c`, `vi.c`), DOL read for the default font, THP decode
  (aurora's decoder in `xbox/src/port/thp`), pads.

## Console round hw1 (red, 2026-10-27)

The one hardware round so far (`tools/xbox/scenarios/hw1`, logs and shots in
`C:\xemu\hw\prime\logs-hw1`, map `C:\xemu\hw\prime\prime_x.hw1.map`):
five chained stages (first boot to the Frigate, Tallon, Phendrana, Magmoor,
Chozo Main Plaza) then free play. Red: 480i 4:3 composite (AV pack 1).

- Works: boot, full disc check (matches the Redump hash; took about a
  minute), title, file select, New Game, the Frigate, MP_BOOT_WORLD jumps,
  walking, firing, pause inventory and map screens, MusyX sound effects and
  music (the Frigate's and the Overworld's), rumble, the exit combo.
- **CPU-bound, GPU idle**: Tallon ~10 fps in play (sim 70 ms, display lists
  22 ms, 2800 draws, 66k vertices a frame); Chozo Main Plaza ~5 fps (display
  lists 120 ms, 7000 draws, 148k vertices). Phase 6 starts here.
- Memory: lowest free 3.7 MB (Frigate opening, Main Plaza).
- Console-only bugs: the landing cinematic (Tallon, frame 900) washed out
  white with vertical streaks (fixed 2026-10-28: particle texcoords, see
  the top); the arm cannon missing in Main Plaza (the
  user also saw it "white sometimes").
- The user: the disc check needed explaining (done: the boot screen, E).

## Console rounds hw2, hw3 (red, 2026-10-27)

The first-boot experience: boot screen E, the full disc check with the
intro music, then a second boot (logs `C:\xemu\hw\prime\logs-hw2`, `-hw3`).
The user: "overall it's good", intro approved. Fixed after hw2: the check's
second note line was dimmer; the bar sat at 10% and jumped to 100% on the
second boot (it now eases and the splash waits for it to fill); the music
moved from `intro.pcm` into `default.xbe` (IMA ADPCM, `make_intro.py`);
L+R+Y at boot forces the full check; the watchdog wrote a false "no first
frame" `hang.log` during the check. hw3 confirmed: the music plays from the
XBE, the check passes in ~65 s, the splash costs 19 ms a frame at 25 fps
(7.5 in xemu). **No more console rounds until the user says.**

## Session 2026-10-27 (evening): FPS, Options, GX fixes

- **Draw batching**: short strips, fans and line strips (every text glyph,
  billboards, map surfaces) become lists and join (`gx_vtx.c` `seg_list`).
  Landing Site in xemu: 2900-4300 draws a frame -> ~500, dlist 19-27 ms ->
  5-6, xemu fps 11-14 -> 18-19. LTO + PGO on top: 20-24.
- **ThinLTO + PGO + link order** (`docs/pgo.md`): `XBOX_LTO=1`,
  `XBOX_PGO=xbox/prime.profdata` (first profile: Tallon, Frigate, Phendrana;
  Chozo and Magmoor run out of memory instrumented), `xbox/order.txt`.
- **Builtins**: game code gets `-fhosted -fbuiltin -fno-math-errno` (nxdk's
  wrapper made `fabsf`/`sqrtf`/small `memcpy` calls).
- **Menu text fixed**: CI textures keep their texels once and draw with
  the TLUT loaded last (per-unit palette, `gx_tex.c` `palette_for`); the
  game switches palettes per run of text without reloading the font.
- **Rain**: `RenderRainParticles` writes its RGBA4 colour with
  `GXTexCoord1s16`; taken as a texcoord, every rain batch ended short and no
  rain drew (`gx_vtx.c` `color16_next`, now `put16`).
- **GUI**: `CGuiObject` recomputes only the moved subtree (was n^2).
- **Options rows** on the pause screen (`docs/ui.md`); still to do: the
  title-screen Options, Skip Cutscenes, **button remap page** (the user:
  "button mapping will be expected"), restart prompt for the video row.
- The PC crashed once under 3 xemus + a 16-job build (WHEA machine check,
  CPU core 0): at most 2 xemus, builds at `-j8`, not both at once.

- **Streamed music** (the main menu's, some rooms', movie audio): the AI DMA
  path as MetroidPrimePort drives it, the main loop running the callback
  into a 200 ms queue that MusyX's output mixes in (`prime_sdk.c`,
  `px_sdl3.cpp`). `[AI] streamed audio starts` at the title in xemu; not
  heard on a console yet.
- Port Options: Fusion Suit (System), B is back in menus, three-choice rows.
- PGO retrained on the current code (Tallon, Frigate, Phendrana).

## Console round hw4 (red, 2026-10-27, night)

Chain `tools/xbox/scenarios/hw4`, logs `C:\xemu\hw\prime\logs-hw4chain`.
Landing Site in rain: hw1 ~10 fps -> r4b plain ~24-31 -> r4a ThinLTO + PGO
~27-41 (mostly mid-30s). Shots on the console render right (gun, HUD, rain,
inventory Samus): the earlier "gun white" and sky glitches were likely the
stale-palette bug. **r4c froze** during audio start-up after a chained
launch (after `[AUDIO] found`, before `idle`; no hang.log): flushed
`[AUDIO] step:` markers now say where next time. The user's free play: the
menus work; the front end runs ~17 fps because of the background movie's
decode (THPDec's double-precision FMA on an SSE1 CPU, plain float now).
**No console rounds until the user says.**

# Handoff: state and working notes

Update this at the end of every session: what was done, what's next,
what's broken, anything the next agent would otherwise rediscover.

## Where it stands (2026-10-30, morning)

The game plays through every world in xemu (room sweep: 14 rooms, no
failure, plain and LTO + PGO) and on the red console (~35-45 fps in most
rooms at 480i at hw9, fights 10-20). Everything is committed and pushed on
`dev`. **No hardware round is planned** (the user, after hw9; hw10 was
staged and called off), so nothing since hw9 has been measured on a
console. Verify builds (`-DXGX_DYN_VERIFY=1`) check every new GX path
against the old one draw by draw in xemu; xemu itself is a bit
non-deterministic in float (session 2026-10-30), so a verify run shows a
few one-bit normals: compare against that, not zero.

Ship builds are `XBOX_LTO=1 XBOX_PGO=xbox/prime.profdata` (`docs/pgo.md`);
deploy with `tools/xbox/console.py` (console FTP 192.168.158.113,
`PX_HW=/c/xemu/hw/prime`; back up `E:\UDATA\50580001` first). The
profile was retrained on 2026-10-30's code (`xbox/prime.profdata.txt`). Console
rounds and their files: `C:\xemu\hw\prime\ROUNDS.md`.

## Next session, in order

1. **Fights** (the user's complaint after hw9: "fps got really bad
   fighting the pirates" on the Frigate). In: skinned lists over the
   dynamic budget go index-only; same-frame reuse of skinned vertices;
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
2. The HUD minimap is 150-300 draws a frame (fill and outline alternate per
   surface, `GXSetTevKColor` between): only a different draw method would
   join them; only if it keeps the retail look exactly.
3. Release: done in xemu on 2026-10-30 (profile retrained on that code,
   LTO + PGO autopad build, room sweep 14/14); a console round is the
   user's call.
4. If memory runs out on red again: another reclaimer is easy to add
   (`xhw_mem_add_reclaim`); left: the display lists' dynamic templates.
5. Not done, by choice: `GXSetDstAlpha` and the EFB pixel format (Morph
   Ball's shadow IDs, fog volumes, thermal's DSTALPHA blends) and
   Magmoor's heat-haze warp on the GPU (new render-to-texture passes: the
   console has faulted on those before).
6. Visors and parity: Dolphin reference frames in any room with
   `tools/xbox/dolphin_ref.py` (docs/testing.md "Dolphin reference
   frames"; no save needed); the comparisons (`docs/renderer.md`) not
   made yet. First: the landing glow box (retail fades the sprite's edges).
7. Gate leftovers: the 128 MB hold needs a 128 MB-aware BIOS in xemu; the
   save round trip on red and Dolphin `.gci` (phase 4); Docker/CI (the
   workflow exists, `gh` isn't logged in here).

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
- Looked at, left: the landing glow box (the game sets that fog itself;
  Dolphin now shows retail: a starburst fading out, no box), the HUD minimap
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

## Working notes

- Build: `tools/xbox/msys/build.sh` (test build: `XBOX_CFLAGS=-DXHW_AUTOPAD=1`;
  draw trace of a SHOT frame: add `-DXGX_DEBUG_TRACE=1`). Output
  `build-xbox/xbe/default.xbe`, map `build-xbox/prime_x.map`, log of the
  last build `build-xbox/build.log`.
- xemu: `. /c/xemu/px/env.sh` (this PC's `PX_*` paths, outside the repo),
  then `PX_STAGE_EXTRA=tools/xbox/scenarios/<name> tools/xbox/xemu_run.sh
  <secs> '<stop regex>'`; shots: `python tools/xbox/fbdump_to_png.py
  /c/xemu/px/run/serial.log build-xbox/shots/<name>`.
- xemu is slow on this game: the title runs ~12 fps (THP's JPEG decode in
  emulated SSE), a load can take 15 s. Give scenarios 20 minutes.
- The user's image: `C:\xemu\prime\Metroid Prime (USA) (v1.00).iso`
  (extracted from the NAS, `P:\Game Console ISO\ngc\Metroid Prime (USA)
  (v1.00).7z`; GM8E01 rev 0, SHA-1 `ac20c744db18fdf0339f37945e880708fd317231`).
- `[STUB] name` lines: a dropped hook the game reached (once each).
- `ref/Melee-X`'s working tree next door (`Documents/GitHub/Melee-X`) has an
  unfinished merge: never touch it; read `ref/Melee-X` (pinned) instead.
- Heredocs in Git Bash mangle backslashes in some cases: write files that
  contain `\\` with the editor tools, not `cat <<EOF`.
- Host tests on this PC: `PATH=/c/msys64/mingw64/bin:$PATH CC=gcc python
  tools/xbox/test_<name>.py` (no compiler on the Windows PATH, no Docker;
  `test_vp_encoder.py` also wants the `nv2a_vsh` module).
- `PX_LOCKSTEP=1` runs aren't frame-exact from one run to the next (loads
  and streaming vary), so a SHOT frame number lands on different moments.
  To bisect a glitch, shoot a dense run of frames in one run, pick the bad
  ones by their traced draw count, and skip draw ranges (`XGX_SKIP`) over
  that window. The trace's first-vertices lines show bad attribute decodes.

## The user's requirements, in one place

From the planning session (all in `docs/decisions.md`):

- USA 1.00 only; MetroidPrimePort trimmed; retail plus light extras
  (cutscene skip, widescreen HUD, FOV).
- 480 and 720 widescreen; 60 fps at 480, 30+ at 720p, as high as possible.
- Settings inside the game's own Options screens, looking built in (as
  OpenCrossing-Xbox's do).
- Splash: Melee-X's "TechProGabe Presents..." remixed in Prime's look.
- Thorough disc validation at boot; 128 MB consoles locked to 64 MB;
  upgraded/overclocked CPUs supported gracefully.
- Repo `TechProGabe/Prime-X`, private.

Added 2026-10-27 (this session):

- Test in xemu until it is stable, then on hardware.
- Level skip and the useful port debug tools must work in xemu and on the
  consoles (`MP_BOOT_WORLD` & co., `docs/testing.md`).
- Saves must not cause hitches (Melee had them before v63).
- No subagents.

## This PC

- Windows 11, Git Bash and PowerShell. `python` (not `python3`) has Pillow.
- MSYS2 `C:\msys64` (clang 21.1.8), nxdk `C:\xdev\nxdk` at `58427c07`,
  xemu `C:\xemu\xemu\xemu.exe` with `C:\xemu\xemu.toml` (shared with
  Melee-X's runs).
- GitHub: `gh` isn't logged in; the stored git credential is TechProGabe.

## Open questions for the user (ask when they come up, not before)

- Whether to fall back to a 720p render scale if 720p can't hold 30
  (`docs/performance.md`).
- "Steady 30" at 720p vs uncapped, once judder can be measured.
- Front LED effects: worth doing for Prime, or just off.

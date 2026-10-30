# FPS plan: as many frames as the Pentium III will give

Written 2026-10-30 from round hw10's logs (`C:\xemu\hw\prime\logs-hw10`,
map `prime_x.hw10.map`), the GX layer and the game's tick loop. This is
the plan another agent builds from. It supersedes the "levers" table in
`docs/performance.md` as the order of work; that file keeps the history.
Every item here has a kill switch, a measurement and a line for
`docs/decisions.md`, as the rules in `CLAUDE.md` ask.

The user's policy for this plan (asked 2026-10-30):

| question | answer |
|---|---|
| simulation rate | **Auto**: 60 Hz ticks while the frame fits, 30 Hz ticks with interpolated 60 fps presentation when it doesn't. Fixed (retail) and Flex stay as choices |
| visual trades | anything **imperceptible in motion ships on by default**; a perceptible trade becomes a Port Options row that defaults to retail |
| audio | measure the mixer first, then an SSE mixer; the hardware-voice (MCPX APU) driver only if the mixer still costs more than 10% |
| console rounds | **one round per milestone**: the builder stages it (upload + chain script) and stops; the user launches it |
| exactness | bit-exact agreement with retail or Dolphin is not required; approximations that change nothing a player can see are in |

## 0. Progress (updated as items land)

| item | state |
|---|---|
| M0.1 mixer cost in [PERF] | built: `px_mixer_frame_begin`/`end` (hw_pc.c -> px_sdl3.cpp), hw-lock waits taken out |
| M0.2 mixer thread in the profiler | built: `[PROFA]`, `xhw_prof_set_mixer_thread`; prof_report.py folds it |
| M0.3 ticks apart from traversal | built: the game's draw in `XHW_PERF_RENDER`; [PERF] `ms per tick` |
| M0.4 statistics per scene | built: `xgx_stats_now` at NEXT; prof_report.py: `pb_busy` fold (COLD), GPU-wait share |
| M0.5 test hooks | built: `PX_SIM_AUTO`, `PX_SIM_RATE`, `PX_INTERP` (and `PX_SIM_ADAPTIVE`) |
| round hw11 | ran on red 2026-10-30: numbers below |
| M1.1 Game Timing: Auto | built; the only mode since 2026-10-30 (the Port Options row removed, the user's call; Fixed/Flex as test hooks); `[TIMING]` lines, [PERF]'s auto share. xemu: holds 30 Hz in the pirate room (xemu is slow), no fault |
| M1.2 interpolated presentation | on at 30 Hz ticks (`XboxPresentBlend`); `FrameLimitEnabled` stays true for the clock's cadence leeway |
| M1.3 60 Hz assumptions | render clock steps scaled (`XboxRenderTimingSteps`), its present lag a tick's period; pad presses latched between 30 Hz reads (`pad.c`); HUD memo counter: counts unpauses, nothing to do |
| M2.1 RMS and trace | the RMS is gone from the Xbox build |
| M2.2 the mix in 32 bits | built, bit-exact (`px_mixer.h`, `test_mixer.py`): the ramps and the resampler's taps. Not float/SSE: the bounds-checked integer path is exact and needs no new buffers |
| M2.3 bus skipping | already there (zero ramps return at once) |
| M3.4 lazy `GXSetArray` | built (same array: no close, no re-sign) |
| M3.5 pushbuffer kick | built, adaptive (32768 words while the GPU keeps up) |
| M5 rain clip | built: the batch's box tested once (`XGX_LINE_BOX`) |

### Round hw11 (red, 2026-10-30): the numbers

Profiler build of b628024, 480i. fps: the bulk of each scene's [PERF]
lines (5 s each), loads and cinematics left out. Milliseconds a frame in
the steady part of the scene; "render" is now the game's own draw
(traversal, GX state calls, immediate-mode vertices), "sim" the ticks.

| scene | Fixed fps | Auto fps | ms per tick | render | dlist | draw | audio |
|---|---|---|---|---|---|---|---|
| pirate fight (r11c / r11i) | 18.5-21 | 25-26 | 7.2-7.4 | 12.6-13.2 (Auto 13.5-14.3) | 8.2-10.4 | 6.0-6.6 | 13-15% |
| Main Plaza (r11d / r11j) | 35-36 | 37-39 | 4.0 | 9.5-10 (Auto 10-10.7) | 3.5-5.8 | 7.6-7.9 | 4% |
| Landing Site (r11b / r11k) | 26-59 | 28-59 | 4-10 | 4-10 | 1.7-14 | 1-3.8 | 7-14% |
| Lava Lake (r11f / r11l) | 28-29 | 34 | 7.3 | 10 (Auto 11.6) | 5.1 | 4.0 | 9% |
| Phendrana visors (r11e) | 44-59 | | 3 | 4.5-7 | 1-2.5 | 2.2-3 | 4-23% |
| Frigate opening (r11g) | 50-60 | | 5 | 3-6 | 0.2-3.8 | 0.7-3.2 | 2-4% |
| pause screen (r11h) | 46.5 | | 1 | 5.7 | 5.2 | 2.1 | 4-15% |

What it says:

- Auto works (100% of the heavy rooms' frames at 30 Hz, no flapping) and
  gives +30% in the fight and +20% in Lava Lake, less than the model:
  the frame is now the draw work, R = render + dlist + draw = 28 ms in
  the fight, 21 in Main Plaza and Lava Lake. Interpolation costs ~1 ms of
  render. Under Auto the tick is a minor cost (sim 9-10 ms a frame in the
  fight, ~4 in Main Plaza).
- The game's own draw ("render", 10-14 ms) is the largest single bucket
  now, ahead of the display lists. Rain (GXPortRainLines + the immediate
  flush) is ~5% of the fight room (Frigate Crash Site is rainy).
- The mixer is 13-15% of the fight's CPU, up to 23% in Phendrana: a
  real cost after the 32-bit path. [PROFA]: the mix loop (salCtrlDsp)
  70%, the reverb (ReverbSTDCallback, CheapReverb) 18%.
- The GPU waits nowhere but the visors (Phendrana: gpu 3-7 ms a frame).
- The audio fault of hw10 (stuck from the third scene on) did not happen
  in twelve chained scenes.
- In the fight, the dynamic (skinned) lists are GXCallDisplayList 9.7% +
  memcpy/memmove 7.7% + BuildBoneRange 1%: what GPU skinning (M4, built
  after this round) removes.

### Round hw12 (red, 2026-10-30): GPU skinning, the mixer

Profiler build of f28c4d8, Game Timing: Auto in every scene.

| scene | hw11 | hw12 | notes |
|---|---|---|---|
| pirate fight | 25-26 (Auto) | 30-32.5 | GPU skinning off (r12c, same build): 24-26 |
| pause screen (inventory) | 46.5 | 58-60 | Samus skinned on the GPU: dlist 5.2 -> 0.9 ms |
| Lava Lake | 34 | 40-41 | |
| Main Plaza | 37-39 | 36-37 | the vertex pool ran full (GPU-skin lists took it); fixed after: a 1 MB budget, evicted first |
| Phendrana visors | 44-59 | 39-60 | |
| Frigate opening | 50-60 | 50-60 | |

No fault, no stuck audio. GPU skinning's CPU fallbacks: ~5-10% of skins
in the fight, one a frame in Main Plaza (a model with two texgens from
position and normal through different matrices: one per-vertex texture
matrix slot can't serve both).

## 1. Where the time goes (the model everything below rests on)

Measured on red at 480i, profiler builds (LTO + PGO), `[PERF]` lines of
hw10. "sim" is the `[PERF]` bucket: on Prime it holds the game ticks
**and** the game's own render traversal (`CStateManager::DrawWorld`,
culling, `CCubeMaterial::SetCurrent`, every GX state call), because
nothing enters `XHW_PERF_RENDER` (vi.c's `xsdk_perf_render_begin` is
Melee's hook and Prime never calls it). "dlist" is `GXCallDisplayList`,
"draw" is the NV2A back end (`xgx_draw`).

| scene | fps | sim ms | dlist | draw | draws/frame | ticks per render |
|---|---|---|---|---|---|---|
| Frigate Crash Site, pirate fight (r10d) | 17-18 | 38-41 | 9-11 | 6.7-7 | ~1020 | 3.4 |
| same room after the fight | 33-36 | 19-22 | 4-5 | 3.7-4 | ~620 | 1.7 |
| Chozo Main Plaza (r10e) | 30-36 | 15.7-21 | 3.8-5 | 7.6-7.9 | ~2070 | 1.7 |
| Landing Site, rain (r10c) | 34-59 | 10.4-25 | 2.1-13.9 | 2.4-3.9 | ~700 | 1.1-2.2 |
| Lava Lake (r10g) | 27-28 | 26.5 | 5.3 | 4.1 | ? | 2.2 |
| Frigate opening (r10h) | 50-60 | 7-12 | 0.2-2.7 | 0.8-2.8 | | 1.0-1.2 |
| Phendrana, visors (r10f) | 42-58 | 8.7-15.5 | 0.8-1.7 | 2.2-3 | 300-460 | 1.0-1.4 |
| gpu wait | fight, Main Plaza, Lava Lake 0.0-0.2 ms; Landing Site 0-1.9; **Phendrana's visor passes 3.5-7.4 ms a frame** (section 1c) | | | | | |

Three facts decide the plan:

**(a) The catch-up spiral.** Under Game Timing: Fixed (retail) the game
runs one 1/60 s tick per 16.7 ms of wall time, so a slow frame runs more
ticks and gets slower. With k = ms per tick and R = ms of render work a
frame (traversal + dlist + draw), the frame settles at

    T = R / (1 - k / 16.7)

The pirate fight: k ≈ 9.4, R ≈ 23 (6 traversal + 10 dlist + 7 draw) gives
T = 52 ms (17-19 fps, measured). Main Plaza: k ≈ 5, R ≈ 19 gives 27 ms
(36 fps, measured). The amplifier 1/(1 - k/16.7) is 2.3x in a fight: every
millisecond cut from the tick is worth 2.3 ms of frame, and every
millisecond of render is paid 2.3 times. Fixed can never reach 60 fps
unless k + R ≤ 16.7 in the room. Flex (one tick a frame, dt = the frame)
removes the amplifier but slows the game under 30 fps and makes dt vary.

**(b) The profile is flat.** Over 100k samples of the fight no game
function is above 2.7%; the GX layer's display-list path (`GXCallDisplayList`
9.2% + its memcpy 5.2% + `gx_vtx_flush` 2.7% + `xgx_draw` 2.5% + `emit_vc`
1.1% + `pb_cache_flush` 1.0% + `bind_new` 0.7%) is the only block, ~22%.
In Main Plaza the same block is ~27% (`GXCallDisplayList` 11.3%, `xgx_draw`
7.9%, memcpy 3.5%, `bind_new` 1.7%, `emit_vc` 1.7%). The game's own
simulation is hundreds of 0.1-1% functions (the sorted lists, collision,
particles, animation readers, `sin`/`atan` from pdclib 1.1%). Micro-work
inside the tick yields a few percent each; the structural levers below
(fewer ticks, fewer and cheaper draws, work moved to the GPU) are the only
ones that reach 60 fps.

**(c) Two costs are invisible, and one room is the other way round.**
The GPU waits are ~0 in the fight, Main Plaza and Lava Lake: the NV2A is
idle most of those frames, so anything moved onto it is free. Not in the
visor passes: Phendrana with thermal or X-ray on (r10f) waits 3.5-7.4 ms a
frame for the GPU (`[NV2A] per 600 frames: GPU still busy at 452 frame
starts, 4138 us a frame waiting there`, 600 EFB copies per 600 frames),
and 33.5% of its profiler samples sit under `wait_idle`'s spin (the
report credits them to `pb_create_gr_instance`, which is merely the map
symbol 0xA0 bytes before `pb_busy`: a bucket-sharing artefact, note it in
`prof_report.py`). At 44 fps there the CPU has ~18 ms of work and the GPU
~22 ms: the thermal and X-ray passes are **GPU-bound at 480**, and 720p
multiplies that by 2.25. The GPU levers (milestone 5, item 6) matter for
those rooms and for 720p, and nowhere else. And MusyX's
software mixer runs on its own thread (`extern/musyx/src/musyx/runtime/hw_pc.c`,
`salAudioThreadFunc`, a 160-sample frame every 5 ms): the sampling profiler
reads only the game thread's stack, so when the mixer preempts the game
the sample lands on whatever game code was interrupted, and `[PERF]`'s
"audio 0%" is a hook (`xhw_perf_audio`) nothing calls. From the code: per
voice per frame an ADPCM decode, a 4-tap polyphase resampler in 64-bit
arithmetic, an RMS (64-bit multiply-accumulate and a double `sqrt`, used
only by a trace), and up to nine `mixRampChannel` passes (main L/R/S,
aux A L/R/S, aux B L/R/S) each doing two 64-bit multiplies and two
64-bit clamps per sample (`pc_audio_math.h`), plus the software reverb
(`StdReverb`/`CheapReverb`) on the aux bus. Estimated 0.5-1% of the CPU per
active voice: 8-20% in play. Nobody has measured it. On the GameCube this
ran on the DSP.

Two more numbers from the in-room (`--last`) profiles, for the weather
items: the Landing Site's rain is `gx_vtx_flush` 6.6% + `GXPortRainLines`
3.1% + `CEnvFxManager::Update` 1.1% + its share of memcpy, **~12% of a
rainy frame** on red; Phendrana's snow is `GXPortSnowQuads` 3.8% +
`gx_vtx_flush` 5.8% + `CEnvFxManager::Update` 2.8%, ~10%, on top of the
visors' GPU wait. Lava Lake's tick is particles: `CElementGen::InternalUpdate`
2.2%, the element evaluators (`CCEKeyframeEmitter::GetValue`, `CVEAngleCone`
with `sinf`/`cosf` per particle), `CParticleDatabase::Update`,
`RenderParticles` and its per-vertex GX calls (`GXTexCoord1s16`,
`GXNormal3s8`).

Everything else that was tried this week (prefetches, inline compares,
cheaper clocks, fast paths per attribute) is done and in; the profile
shows what is left.

## 2. Rules for every item

- A kill switch per change: `-DXGX_<NAME>=0` (GX layer), `-DXSDK_<NAME>=0`
  (SDK), `-DPX_<NAME>=0` (port/game hooks); test builds also take
  `env PX_<NAME>=0|1` where a scenario needs an A/B in one image.
- Game-code edits stay one-line hooks into `xbox/src/port` under
  `TARGET_XBOX` with a `PORT_XBOX:` comment and a line in `docs/decisions.md`
  ("Edits to imported code"), as the existing ones.
- A renderer change ships with a verify build (`-DXGX_DYN_VERIFY=1` style:
  the new path and the old one run on every draw and compare) and a room
  sweep (`tools/xbox/room_sweep.sh`, 14 rooms, plain and LTO + PGO).
- A simulation change (milestone 1) is checked with `simh` lockstep hashes
  where it claims to be a no-op (Auto at 60 Hz must hash like Fixed) and
  with lockstep shots against the previous build where it changes timing
  by design.
- Numbers come from red only. Each milestone ends with a staged console
  round (`tools/xbox/scenarios/hw11` and on): the hw10 chain's scenes
  (Landing Site r10c, pirate fight r10d, Main Plaza r10e, Phendrana visors
  r10f, Lava Lake r10g, Frigate opening r10h) plus a pause-screen scene and
  the profiler build with no script for free play. Compare `[PERF]` median
  and 5th-percentile fps, ms per tick (new, milestone 0), audio %, draws per
  frame, and the shots.
- PGO retrains after each milestone that changes hot code (`docs/pgo.md`);
  the current profile is from 452e820.
- xemu's timing is not the console's: a change that xemu can't show (SSE
  mixing, cache layouts, pushbuffer kicks) is still built, with its switch,
  and judged on red.

## 3. Milestones

Order matters: each one changes what the next should measure. Estimates
are for a stock 733 MHz console at 480i, from the model in section 1;
they are to be checked, not promised.

### Milestone 0: see everything (1-2 days)

Nothing below can be judged without these.

1. **Mixer cost in `[PERF]`.** In `salAudioThreadFunc` (hw_pc.c, under
   `TARGET_XBOX`) take `xhw_perf_now()` around the frame's render
   (`userCallback()` → `salCtrlDsp`) and call `xhw_perf_audio(delta)`. The
   "audio N%" field then means something, and `[PERFX]` already lists how
   much of it landed in each bucket.
2. **Mixer thread in the profiler.** `xhw_prof.c` samples `s_game` only.
   Register the mixer thread too (`xhw_prof_set_mixer_thread()` from
   `px_SDL_CreateThread`) and, when the game thread is preempted and the
   mixer is the running thread, credit the sample to a `[PROFA]` histogram
   (its EIP from the same `xhw_thread_eip` walk, or the kernel's current
   thread). `prof_report.py` folds `[PROFA]` like `[PROFS]`.
3. **Ticks apart from traversal.** Wrap `archSupport->GetIOWinManager().Draw()`
   in `main.cpp` (TARGET_XBOX) with `xhw_perf_enter(XHW_PERF_RENDER)` /
   `xhw_perf_leave`, so "sim" is the ticks and per-frame bookkeeping, and
   "render" the game's traversal. `[PERF]` adds `ms per tick` = sim / ticks.
   `[PROFS]` becomes a tick-only profile for free.
4. **Draw-merge and state statistics per scene** already exist
   (`[NV2A] per N draws: ... changed nothing ... maps ...`, `GPU still busy
   at N frame starts`); make sure the chain logs them at each scene's end,
   not only every 600 frames. `prof_report.py`: fold `pb_busy`'s spin
   correctly (section 1c) and print the GPU-wait share per report.
5. **Test hooks for milestone 1 in one image**: `env PX_SIM_RATE=30|60`,
   `PX_SIM_AUTO=0|1`, `PX_INTERP=0|1`, so xemu lockstep runs can A/B the
   tick policy and the interpolation without rebuilding.

Round hw11: the hw10 chain on this build. Its purpose is the mixer's
number and the tick/traversal split per room; fps should match hw10.

### Milestone 1: the tick policy (3-5 days)

The biggest single lever, and most of it is already in the tree.
MetroidPrimePort's frame interpolation (`ref/MetroidPrimePort/docs/FRAME_INTERPOLATION.md`)
is imported: actor transforms (`CActor::PortSnapshotRenderTransform`,
`PortPresentedView`), skinned poses (`CAnimData::PortPresentedPose`),
particles, swooshes, electric effects, beam weapons and the camera all
blend between the last two ticks at `GetTickInterpolation()`. On the Xbox
every flag returns false (`port_debug.cpp`: `ActorInterpolation`,
`PoseInterpolation`, `ParticleInterpolation`) and `FrameLimitEnabled()`
returns true, which sets the blend factor to -1 in `RsMain`. The clock
(`PortTiming::FixedStepClock`) already takes any period (`SetPeriod`) and
exposes `Interpolation()` = leftover / period. `PortDebug::SimRate()` is a
constant 60.

Build:

1. **Game Timing: Auto** (default), Fixed, Flex, in the existing Port
   Options row (`port_xbox_options.cpp` `kXO_GameTiming`, `xsdk_settings`
   `sim_adaptive` becomes a three-way `sim_mode`). Auto picks the period
   per frame from {1/60, 1/30}:
   - to 30 Hz when the last frame's CPU time (sim + render + dlist + draw
     + tex + efb, from `xhw_perf_acc`, excluding gpu and vsync waits) was
     over 16.0 ms for 3 of the last 4 frames;
   - back to 60 Hz when `ms per tick + render` (both measured, section
     M0.3) has been under 14 ms for 30 frames in a row.
   The controller lives in `port_debug.cpp` (`SimRate()` reads it) and is
   fed by a one-line call from `xsdk_frame_boundary` (vi.c). At 60 Hz Auto
   is Fixed: `simh` hashes must match. Test hook `PX_SIM_AUTO`.
2. **Interpolated presentation on** whenever the tick period is 1/30
   (and, as a setting, at 1/60 too, where it costs a little CPU for
   nothing visible on a 60 Hz output): `ActorInterpolation`,
   `PoseInterpolation`, `ParticleInterpolation` true; `FrameLimitEnabled()`
   false on the Xbox (the vsync pacing in vi.c is not the PC's frame
   limiter); `RsMain` then passes `GetTickInterpolation()` through. With
   a 1/30 period and 60 Hz frames the factor alternates 0 and ~0.5, which
   is what the blend was written for (the clock's `cappedCadence` leeway
   keeps the alternation steady).
3. **Things that assume a 60 Hz tick** (`ref/MetroidPrimePort/docs/HIGH_FPS_AUDIT.md`
   lists them for the raised rate; at the lowered one they run half as
   often):
   - `CGraphics::TickRenderTimings(sTicksAdvanced)` steps the draw-time
     clock (UV scrolls, texture animations) 1/60 s per tick: pass
     `ticks * TickPeriod() * 60` or the world's textures scroll at half
     speed at 30 Hz.
   - `CStateManager::xf80_hudMessageFrameCount` (HUD memos count ticks):
     scale by `TickPeriod() * 60`.
   - Pad edges: `CInputGenerator::Update` samples the pad once per tick,
     so a 20 ms press can fall between 30 Hz ticks. `pad.c` keeps "pressed
     since the last read" bits so no edge is lost (the pad-poll alarm runs
     at 60 Hz either way).
   - `CSpacePirate`/`CFlyingPirate` `% 7` think cadences, `x63c_frenzyFrames`,
     `CParasite`/`CSeedling` think counters, `CPlayer::x2b0_outOfWaterTicks`:
     left alone (a few frames of timing, the audit's own verdict).
   - `CElementGen::kTickTime` (1/60 substeps): see milestone 5; at 30 Hz the
     systems run two substeps a tick, same CPU as before, correct output.
4. **Flex stays** as the no-lag alternative: one tick a frame, dt = the
   frame's time. Document the difference in the row's description: Auto
   is smooth (60 fps presentation) but shows the world up to one tick
   (33 ms) behind the simulation and samples the pad at 30 Hz in heavy
   rooms; Flex is immediate but slows the game under 30 fps and varies dt.
5. **The catch-up cap** in `UpdateTicks` (`tickBudget`, 33 ms of tick work)
   is left for Fixed; Auto never spirals because it changes the period
   instead.

Expected (model, section 1): the pirate fight 17-18 → 33-36 fps (two
frames per 9.4 + 2 × 23 ms); Main Plaza 30-36 → ~45; Lava Lake 27 → ~45;
the Landing Site's lows 24 → 40+. The game runs at full speed everywhere
it did not before. Verify: lockstep shots of the sweep rooms unchanged at
60 Hz; at 30 Hz the pirate room's motion smooth across 20-frame shot
runs; `simh` equal to Fixed when Auto sits at 60 Hz. Round hw12.

### Milestone 2: the mixer (2-4 days, after hw11's number)

If hw11 says the mixer is under 3% of the CPU, skip to milestone 3. The
code says it will not.

1. Delete the per-voice RMS (`voiceRms`, trace only) and the `fprintf`
   trace paths from the Xbox build.
2. `mixRampChannel`, `addThreeChannelBuffer`, `downmixStudioToStereo`,
   the resampler and the ADPCM decode in 32-bit integer or SSE1 float:
   decode a voice's 160 samples to float once, then each channel is a
   4-wide multiply-add with a ramped gain (40 iterations), saturate at
   the end of the frame, not per sample. The 64-bit `musyxPcScaleQ15` /
   `musyxPcClamp32` pairs are the cost: s16 × Q15 fits 32 bits, and the
   studio bus can hold float. Keep the integer path under
   `-DPX_MIXER_SSE=0`; a host test (`tools/xbox/test_mixer.py`, like
   `test_vp_opt.py`) compares both over random voices to within 1 LSB.
3. Skip whole buses: a voice with zero send to aux B (most of them) and
   the surround channel in stereo output mode (`sndOutputMode`) must cost
   nothing; check `volS`/`volLb` are actually zero there, and that the
   `startVol == 0 && deltaVol == 0` early-out catches them.
4. The reverb (`StdReverb`, `CheapReverb`): measure its share with the
   `[PROFA]` histogram; port its inner loops to float SSE if it is over 2%.
5. Voice count: `sndInit`'s voice number is Prime's (`CAudioSys`); do not
   cap it by default. If a fight runs more than ~24 voices, a Port Options
   row "Sound voices: Retail / 24" is a perceptible trade and defaults to
   retail.

Expected: the mixer from (estimated) 8-20% of the CPU to 2-4%, worth
6-15% of every frame, more in fights. Round hw13 (can share hw12's round
if both are ready).

### Milestone 3: cheaper draws (1-1.5 weeks)

Main Plaza spends ~27% of its frame in the GX layer for 2070 draws (8-9 µs
a draw) and the fight ~22% for 1020. The back end's own statistics say
what those draws change: in Main Plaza 69% of draws change only the
texture maps, 20% change nothing, 11% only a TEV register or konst
colour, 5% only the position matrix. Each item below is a switch.

1. **Merge draws that change nothing** (`xgx_draw`, `-DXGX_MERGE=0`). The
   NV2A takes any number of `DRAW_ARRAYS` ranges inside one
   `BEGIN`/`END`; `xgx_draw` already emits several per batch. When
   `d == 0`, the primitive, layout and 32768-vertex window are the
   previous draw's, append the range and skip the state machinery and
   the END/BEGIN pair; any state emission, EFB operation, kick or frame
   end closes the open primitive first. 20% of Main Plaza's draws, 4-5%
   of its frame.
2. **Konst colours into vertex colours** (`gx_vtx.c`/`nv2a.c`,
   `-DXGX_KONST_FOLD=0`). The HUD minimap (`CMapWorld::Draw`) is 150-300
   immediate-mode draws every frame of play, each surface's fill and
   outline with `GXSetTevKColor` between: the 11% "tevreg only" draws.
   When consecutive batches differ only in a konst colour, the stage reads
   it as a plain colour input, and a vertex colour channel is unused, give
   each batch's vertices that colour as COLOR0 and point the stage at the
   rasterized colour: the same 8-bit value per pixel, the retail look
   exactly, and item 1 then merges them. The map screens get the same.
   1-2 ms a frame in every room (3-7% of a 30 ms frame).
3. **Static promises for disc data** (`port_xbox_gx.h`):
   - `GXPortTexStatic(obj)` from `CTexture`'s disc constructor and
     `GXPortTexForget(data)` from its destructor: the texture cache
     (`gx_tex.c` `tex_due`, `quick_hash`) stops sampling texels of
     textures the game never writes; EFB copies and movie planes keep
     their checks. Also keep the cache entry's index in the texture
     object (a spare word in `TexObj`) so `bind_new`'s `find()` becomes
     one compare. `bind_new` is 1.7% of Main Plaza and 3.3% of its
     simulation bucket.
   - `GXPortListStatic(dl, bytes)` from `CCubeModel`'s construction and
     `GXPortListForget` from its destructor: `dlc_content_changed`'s
     sampled words (a few thousand cache misses a frame in Main Plaza) go
     away for world models; skinned and CPU-built lists keep them.
4. **`GXSetArray` lazily** (`-DXGX_LAZY_ARRAYS=0`): each of a model's 5-12
   calls runs `gx_vtx_close()` and `sig_arr()`; mark instead and recompute
   the signature at the next plan use. 1.0% of Main Plaza.
5. **Pushbuffer kick** `XGX_PB_KICK` 8192 → 32768 words: each kick is
   pbkit's `pb_cache_flush` (sfence, WC flush trigger, spin), 1.0-1.6% of
   every frame; the GPU is idle, so starting it later costs nothing while
   its work stays shorter than the next frame's CPU time (`XGX_OVERLAP`
   already lets it run into the next frame). Measure on red only.
6. **`xhw_time_ns` inline** (`xhw.h`): the ACPI/TSC interpolation as a
   static inline read of a seqlocked (base_ns, base_tsc, scale) triple;
   0.3-0.6% of every frame in calls and 64-bit divisions.
7. **pbkit at `-O1`** with the busy-wait loops' flags made `volatile`
   (`patch_pbkit.py`): it is built without optimisation because `-O2`
   hangs `while (pb_BackBufferbReady[...])`; `pb_busy`, `pb_create_gr_instance`
   and friends are 0.3-0.5%.
8. **Transcendentals.** Two halves. (i) `libm_fill.c` overrides pdclib's
   `sinf`, `cosf`, `tanf`, `atan2f`, `atanf`, `acosf`, `asinf`, `expf`,
   `powf` with SSE1 polynomial versions (range reduction + degree 7-9,
   max error ~1e-6): that catches every float caller at once, the particle
   elements first (`CVEAngleCone::GetValue` and co., `cosf`/`sinf` per
   particle). (ii) `src/Kyoto/Math` under `TARGET_XBOX`:
   `CQuaternion::Slerp`/`AxisAngle`, `CTransform4f::RotateX/Y/Z`,
   `CMatrix3f::RotateX/Y/Z`, `CFrustumPlanes` and `CCubeMaterial`'s UV
   throb call double `sin`, `cos`, `acos`, `atan` from pdclib through x87
   (`-mfpmath=sse` leaves doubles on the FPU): 1.1% of the fight in
   `sin`+`atan` alone, more in the conversions around them. Use the float
   versions, or Prime's own `CMath::FastSinR`/`FastCosR`/`FastArcCosR`
   (129 call sites in retail already). The user accepts the
   approximations; a host test (`test_libm.py`) bounds the error.
9. **`CCharLayoutInfo::GetSegIdFromString`** (0.2-0.3%: a string map lookup
   per locator query per frame): cache by (layout, string pointer).

Expected: Main Plaza's R 19 → 12-13 ms, the fight's 23 → 17; with
milestone 1 that is Main Plaza ~60 and the fight ~45. Verify build:
`-DXGX_MERGE_VERIFY=1` draws merged and unmerged alternately and compares
`[FBDUMP]` shots; the room sweep; Dolphin references for the minimap and
the map screen (`tools/xbox/dolphin_ref.py`). Round hw14.

### Milestone 4: skinning on the GPU (1.5-2 weeks)

The fight's remaining GX cost is the CPU-skinned lists: `GXCallDisplayList`'s
dynamic path and its memcpy (fetching skinned positions and normals from
the workspace into the ring on every call, ~14% of the fight's frame) and
`BuildBoneRange` (1.1-1.6%, up to 3% with several pirates). The design is
sized in `docs/handoff.md` (Next 2): the vertex program already takes a
per-vertex matrix index (`a0` = PNMTXIDX, 10 position and normal
matrices, used by Melee-X on the console), so a skinned list can be drawn
from the model's own disc arrays with each vertex's virtual bone as its
matrix index and the palette loaded per draw. Build:

1. **Memory first**, since the handoff stopped here: the static skinned
   vertices must live in the vertex pool (GPU-readable). Fund it by
   - **double buffering**: `pb_init` allocates three framebuffers
     (`pb_FBAddr[3]`); the GPU is idle, so two are enough. `patch_pbkit.py`
     drops the third: 1.2 MB at 480, 1.8 MB at 720p (`-DXGX_FB_COUNT=3` to
     restore);
   - the dynamic templates' budget (`DLC_DYN_BUDGET` 1 MB + 1 MB extra in
     malloc) shrinking as lists move to the pool: give the vertex pool the
     difference.
   Lowest free memory in hw10 was 3.7 MB; the reclaimers stay as they are.
2. **The GPU path per list**, decided at build time, with the CPU path as
   the fallback for a list that: uses texgen from position or normal
   (reflections want skinned model-space values), has a vertex whose
   position and normal fall in different bones, or has more than 10
   distinct bones in a batch after splitting (strips become lists when
   split; count the memory). `CSkinRules` is shared by every instance, so
   `CSkinnedModel::Calculate` copies its pose's matrices into the palette
   instead of skinning; the skin itself is skipped lazily (`sPointGen`,
   morphs, anything reading the workspace keeps the CPU path for that
   call). Verify build `-DXGX_GPU_SKIN_VERIFY=1`: the CPU-skinned draw and
   the GPU one alternate per frame and the shots compare within the
   normal packing's one bit.
3. Kill switch `-DXGX_GPU_SKIN=0`; the per-model workspaces
   (`GXPortSkinKeep`) and same-frame reuse stay for the CPU-path lists.

Expected: the fight's R 17 → 11-12 ms, `BuildBoneRange` to ~0 for GPU-path
models; with milestones 1-3, the fight at ~55-60 fps at 60 Hz ticks or
60 at 30 Hz. The pause screen's Samus (30 dynamic lists) comes with it.
Round hw15.

### Milestone 5: weather and particles (1 week)

Visual-only work the GameCube's CPU did because its GPU could not.

1. **Rain in the vertex program** (`-DXGX_GPU_RAIN=0`). `CEnvFxManager`
   integrates every rain particle per tick (`UpdateRainParticles`:
   position += velocity × dt, wrapped in its grid cell) and writes the
   lines each frame (`GXPortRainLines`); together with the line batch's
   flush (the clip test and copy in `gx_vtx_flush`) that is ~12% of a
   rainy frame on red (section 1). The motion is linear, so a static vertex
   buffer per grid pattern (each line's start, its cell, its phase) and a
   time constant reproduce it exactly: z = cell height × frc((z0 - speed
   × t) / height), the slant as a constant, the density ramp as a vertex
   count. The CPU keeps culling grids and placing them around the camera
   (cheap) and the visor drops stay as they are (`CHUDBillboardEffect`).
   Imperceptible by construction: on by default.
2. **Snow and underwater flakes** the same way where their sway (a
   per-flake sinusoid of time) fits a short polynomial in the vertex
   program (the NV2A has no `sin`; a parabola per half period is
   indistinguishable on a drifting flake). Phendrana's snow is still
   ~10% of its frame after `GXPortSnowQuads` (section 1: the quad writes,
   their flush and the grids' update).
3. **Particle quads in one call** (`GXPortParticleQuads`, like
   `GXPortSnowQuads`): `CElementGen::RenderParticles` makes 12 GX calls a
   particle (four corners of position, colour, texcoord) through
   `gx_vtx.c`'s slot machinery; write the four vertices at once. The
   fight's `gx_vtx_flush` (2.7-3.9%) is mostly this.
4. **Particles step at the tick period** (`-DPX_PARTICLE_TICK=0`): with
   a 1/30 tick `CElementGen` runs two 1/60 substeps (`kTickTime`), so at
   30 Hz the particle CPU does not halve with the rest of the tick. One
   1/30 step integrates the same linear motion; drag and gravity differ
   by second-order terms no eye finds on a spark. Interpolated
   presentation already blends `x10_prevPos` → position. Lockstep shots
   of the Frigate's fire and the Landing Site's steam decide whether it
   is imperceptible; if not, it is a row defaulting to retail.
5. **Sim micro-work for the tick-only profile** (`[PROFS]` after M0.3),
   each one line and a measurement: the collision leaf cache
   (`CAreaCollisionCache`) kept per physics actor across ticks while its
   swept box stays inside the cached one (margin 1 unit;
   `BuildOctreeLeafCache` and its memcpy are ~0.5% of the fight);
   `UpdateSortedLists`' second pass per tick skipping actors whose bounds
   did not move (0.9-1.9% + `BuildNearList` 0.7-1.6%); `CFluidPlaneCPU::Render`
   builds the lava's mesh on the CPU every frame: build it every other
   frame and redraw the last one (the surface's motion is its UV
   animation, not its mesh).
6. **The visor passes on the GPU** (the one GPU-bound case, section 1c;
   and the whole 720p question). First measure per pass: A/B builds that
   skip one pass each (`XGX_SKIP` ranges on the thermal cold/hot passes,
   the I4 EFB copy `DoThermalBlendCold`/`Hot`, the DEPENDENT_GB palette
   quad, X-ray's blur) and read `GPU still busy` per build. Then, in the
   order the numbers say: copy only the viewport rectangle at the size the
   lookup reads; the I4 copy and the palette lookup at half resolution
   (thermal's heat image is already soft: compare shots with Dolphin; if
   it shows, a "Visor detail: Retail / Fast" row defaulting to retail);
   X-ray's blur passes at half resolution the same way; no `wait_idle`
   around a copy the GPU can order itself (`XGX_EFB_GPU_COPY` already
   draws the copy as a quad). At 720p every full-screen pass costs 2.25x:
   the "720p: render scale" fallback in `docs/performance.md` applies to
   the visor passes only, not the world.

Expected: rainy and snowy rooms +10-15%; fights +3-5%; Lava Lake's tick
~9 → ~6 ms; Phendrana's visors to a steady 60 at 480 and 30+ at 720p.
Round hw16.

### Milestone 6: moonshots (each gated on the numbers before it)

**(a) The static world as recorded pushbuffer segments.** After
milestone 3, measure what the world's ~1700 draws a frame in Main Plaza
still cost; if it is over 5 ms, record them. The NV2A's DMA pusher takes
subroutine calls (`address | 2`) and returns (`0x00020000`), as every NV4+
pusher does; xemu's `hw/xbox/nv2a/pfifo.c` implements both, pbkit uses
only jumps (`0x20000000 | address`), so the first step is a ten-line test
on red (a recorded clear in a called segment). Then: a record mode in
`nv2a.c` where the words `xgx_draw` emits for one world model (its
materials' textures, combiners, texgens and its lists' `DRAW_ARRAYS`
ranges) go to a side buffer that ends in a return; frame-level constants
(view, projection, fog, the area's lights) are set before the call and
excluded from the record; per-material view-dependent constants
(reflection texgen matrices, UV animations) are patched in place through
a patch table each frame; the record pins what it references (texture
pool entries, vertex-pool buffers, vertex-program slots) and is dropped
when any of them is evicted or the visor mode changes. Playback is one
`CALL` per visible model in `DrawUnsortedGeometry`/`DrawSortedGeometry`
(the game still culls). Memory: about the frame's pushbuffer bytes for
the world (Main Plaza peaks at 164 KB a frame), kept per visor mode.
Effort 1-2 weeks; payoff: world draw CPU in every room to ~0.1 ms, the
last piece of R. Switch `-DXGX_RECORD=0`.

**(b) Hardware voices on the MCPX APU.** Only if hw13 says the SSE mixer
still costs over 10%. The Xbox's audio processor has 256 hardware voices
with pitch, volume envelopes and 32 mix bins (what DirectSound used) and
a DSP for effects; nxdk has no driver for any of it, and the AC97 path
(`xhw_audio.c`, already fragile: hw9/hw10's "audio stuck") is downstream
of it. xemu's `hw/xbox/mcpx/apu.c` and `vp.c` are the only register
reference. MusyX's voices would map to hardware voices with the
GameCube's DSP-ADPCM transcoded to the Xbox's IMA-ADPCM at sample-group
load (4-bit to 4-bit; a PCM16 decode would quadruple ARAM's ~4 MB), the
aux bus read back for the CPU reverb. Effort 3-4 weeks, console-only
validation, hang risk on the audio link. Listed so nobody rediscovers
it; not started unless the number says so.

**(c) A second sweep of the world traversal** if (a) is not built:
`CCubeModel::DrawSurfaces` → `CCubeMaterial::SetCurrent` per surface
(1.5% + 0.8% in Main Plaza) could run once per material per model with
the surfaces sorted by material at load (opaque geometry is
order-independent under the Z test; the sorted (alpha) lists keep their
order). Changes draw order within a model only.

### Anytime: compiler trials (half a day each, measured on red)

Each is a build flag in `xbox/CMakeLists.txt` behind an option, kept only
if a round shows a gain:

- `-O3` for `px_game`, `px_game_mp`, `px_sdk` (PGO is on; -O2 vs -O3 differs
  mostly in vectorization and unrolling the Pentium III rarely profits
  from, but it is cheap to try).
- Full LTO (`-flto`) instead of ThinLTO, with `-fwhole-program-vtables`:
  Prime's hot virtual calls (`CRealElement::GetValue` per particle,
  `CActor::Think`, `CEntity::AcceptScriptMsg`) get whole-program
  devirtualization on top of PGO's indirect-call promotion. Needs
  `-fno-rtti` (we have it) and the MSVC vtable layout LLVM supports.
- `-mllvm -inline-threshold` and `-import-instr-limit` (now 30) one step
  each way under PGO: the image is 5 MB against a 128 KB L2 (the Xbox's
  Coppermine), so smaller may be faster.
- `-march=pentium3` is already passed by `nxdk-cc` (checked): SSE1, cmov,
  prefetch, no SSE2. `-ffp-contract=off` is moot (no FMA) and stays.

Things considered and not planned, so they are not redone: AI think
rates or entity sleeping (gameplay), fewer particles by default (visible),
a lower render resolution or 16-bit colour at 480 (the GPU is idle
outside the visor passes, and both are visible), SSE2 (boot-safety), a
second thread for the simulation (one CPU), and micro-optimizations xemu
alone found faster (its softfloat and cache-less timing mislead:
`docs/history.md`, 2026-10-30 night).

## 4. The budget after each milestone (480i, model estimates)

| room | now | M1 | M1+M2 | +M3 | +M4 | +M5 |
|---|---|---|---|---|---|---|
| pirate fight | 17-18 | 33-36 | 38-42 | 45-50 | 55-60 | 60 |
| Main Plaza | 30-36 | 45 | 50 | 60 | 60 | 60 |
| Landing Site (rain) | 24-59 | 40-60 | 45-60 | 55-60 | 60 | 60 |
| Lava Lake | 27-28 | 45 | 50 | 55 | 55 | 60 |
| Frigate opening | 50-60 | 60 | 60 | 60 | 60 | 60 |
| Phendrana, thermal/X-ray on | 42-58 | 45-58 (GPU-bound) | same | same | same | 60 (M5.6) |

The M2 column assumes the mixer measures ~10%; if hw11 finds it at 3%
or at 20%, shift the columns. 720p (R5G6B5 + Z24S8, 16:9) follows at
roughly the same CPU cost per frame where the CPU is the bound (the
world, the fights): the 30+ target there comes with M1-M3. The visor
passes are the exception (GPU-bound already at 480): M5.6.

## 5. Risks and what catches them

| risk | where | catch |
|---|---|---|
| 30 Hz ticks feel laggy (world one tick behind, pad at 30 Hz) | M1 | the Auto controller stays at 60 Hz wherever the frame fits; Flex is one row away; measure the share of time Auto spends at 30 Hz per room (`[PERF]` logs it) |
| interpolation artefacts at 1/30 (snaps over the 4-unit / 45° thresholds, attachments trailing a tick) | M1 | lockstep shot runs of the pirate room and the Landing Site at `PX_SIM_RATE=30`; the FRAME_INTERPOLATION limits list |
| SSE mixer changes the sound | M2 | host test within 1 LSB; A/B by ear on red with `PX_MIXER_SSE=0\|1` |
| draw merging or the konst fold changes a pixel | M3 | the verify build compares merged against unmerged shots; the room sweep; Dolphin references |
| static promises serve stale data (a freed texture's memory reused) | M3 | `GXPortTexForget`/`GXPortListForget` from the destructors; a test build that poisons freed memory |
| GPU skinning: per-draw bone splits cost more vertex memory than the templates | M4 | count before building; the budget decides per list, the CPU path stays |
| double buffering tears or stalls at 60 fps | M4 | `frame_open`'s flip guard; `-DXGX_FB_COUNT=3` back |
| the pushbuffer CALL is not supported on real hardware | M6a | the ten-line test on red before anything else |
| any change holds in xemu and not on red | all | one round per milestone, nothing merged to `main` without it |

## 6. What to do first, tomorrow morning

1. Milestone 0 items 1-3 and 5 (a day), build the profiler chain
   `tools/xbox/scenarios/hw11` from hw10's (the same scenes plus the pause
   screen), stage it, stop: the user launches it.
2. While hw11 waits: milestone 1 in xemu with the test hooks, lockstep
   shots at 30 Hz, the Port Options row.
3. Read hw11: the mixer's share and ms per tick per room go into this
   file's section 1 table, and decide M2's depth.

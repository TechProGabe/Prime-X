# Handoff: state and working notes

Update this at the end of every session: what was done, what's next,
what's broken, anything the next agent would otherwise rediscover.

## Where it stands (2026-10-27)

Planning only. No code has been imported. The repo has the plan and its
docs, the README (written as the finished port's, with a status banner),
`LICENSE.md`, the logo (`docs/logo.svg`) and the dashboard icon
(`xbox/assets/logo.png`, from `tools/xbox/make_logo.py`).

The user approves the plan before work starts. When they have:

## Start here

1. Read `CLAUDE.md`, `docs/plan.md`, `docs/decisions.md`,
   `docs/sources.md`, then skim Melee-X's `CLAUDE.md` and `docs/` (house
   style, and how its layer works).
2. `tools/fetch_refs.sh`. Check out MetroidPrimePort at the pinned commit
   (`docs/sources.md`). The repo's history matters for phase 0.3 (finding
   which `TARGET_PC` blocks came with which feature), so make it a full
   clone: `git -C ref/MetroidPrimePort fetch --unshallow` if needed.
3. Branch `dev` from `main`.
4. Phase 0.1: copy Melee-X's toolchain scripts, build an empty XBE on this
   PC (MSYS2) and in Docker.
5. Phase 0.2: the C++ spike. It decides the build model; nothing is imported
   before it's recorded in `docs/decisions.md`.

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

## This PC

- Windows 11, Git Bash and PowerShell. `python` (not `python3`) has Pillow.
- GitHub: `gh` isn't logged in; the stored git credential is TechProGabe
  (`GH_TOKEN=$(printf 'protocol=https\nhost=github.com\n\n' | git credential
  fill | sed -n 's/^password=//p')`).
- Melee-X, OpenCrossing-Xbox and other projects sit next to this repo in
  `Documents/GitHub`: read them through `ref/` clones, never work inside
  them.
- The user's Metroid Prime USA 1.00 image: ask where it is; never copy it
  into the repo.

## Open questions for the user (ask when they come up, not before)

- Whether to fall back to a 720p render scale if 720p can't hold 30
  (`docs/performance.md`).
- "Steady 30" at 720p vs uncapped, once judder can be measured.
- Front LED effects: worth doing for Prime, or just off.

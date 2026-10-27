#!/usr/bin/env python3
"""List every `TARGET_PC` preprocessor block in the game code with the Port*
names it uses and a class (docs/plan.md 0.3):

  keep        portability: endianness, sizes, bug fixes; no Port* names, or
              only the ones in KEEP
  replace     PC platform (files, logs, input, timing): an Xbox version in
              xbox/src/port
  extra-keep  the light extras the user wants (cutscene skip, HUD, FOV)
  drop        everything else (Remastered, randomizer, mods, savestates...):
              compiled, but its Port* calls answer "off"

  python tools/port_blocks.py [tree]          # default: src/ + include/ of this repo,
                                              # or ref/MetroidPrimePort before the import
  python tools/port_blocks.py --md docs/port-blocks.md

A block's class is the strongest of its names' classes (drop < keep <
replace < extra-keep is not an order: a block is "drop" only when every name
in it is a dropped feature's). Re-run after every MetroidPrimePort sync.
"""
import argparse
import collections
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]

# Port* names by namespace (or function-name prefix) -> class. Anything not
# listed is a game-local helper (a static PortFoo in a game file): keep.
CLASSES = {
    'keep': '''PortParticleMath PortFloat28 PortTickGeneration PortGuid PortSkin PortCurlNoise
        PortKssm PortKssmChild PortCollisionWarp PortDoorFlags PortInventoryScope PortBossState
        PortGuiHit PortWorldFromLocal PortUnlockStackTokens PortLockStackTokens PortStackTokensLocked
        PortIrndUnit PortIrndBits PortRetryFailedRead PortForgetDiscReads PortReportBadResource
        PortFindDolResource PortFindDolFont PortHeapCheckEnabled PortHeapCheckAll PortHeapPeekBlock
        PortEntitySchema PortSwitchCard PortCardChannel PortCardResolvedDirectory PortOnDefaultCard
        PortSaveFormat PortStrings''',
    'replace': '''PortDebug PortDisc PortGci PortPaths PortLog PortCrash PortWatchdog PortMemoryLog
        PortTiming PortInputDevices PortControls PortEnv PortTextures PortReadDisc PortDiscIndex
        PortDiscResource PortSimTick''',
    'extra-keep': '''PortSkipCutscenes PortZoomedFov PortShotAim PortShotFromDrawnMuzzle PortHudString
        PortHudStringAt PortGetMargins''',
    'drop': '''PortRoomEnv PortAp PortApWorld PortApProgress PortSaveState PortRoomGeo PortRoomEffects
        PortPointerMenu PortActorShadow PortElectric2 PortMapPickups PortRadial PortPointer PortMods
        PortDreadSamus PortBeamDoors PortTriReticle PortRemastered PortFx PortVisorMesh PortRandomizer
        PortPauseLighting PortMapMarks PortInputMap PortVisorOverrides PortThirdPerson PortShell
        PortPhoto PortMapSide PortHudBars PortTriOrbit PortRemasteredGround PortEnvFx PortVisorGoo
        PortThirdPersonBody PortRemasteredGroundRider PortMouse PortMapIcons PortHints PortFreeCam
        PortDiscord PortCustomRes PortRoomLiquid PortRemasteredBallLight PortPalLanguages
        PortLiveSplit PortSwooshRing PortHoldToggle PortHdFont PortViewModel
        PortRoomGeometryHandedness PortRetailPlacement PortQueenBarrier PortPrompts PortPbrRecord
        PortBallLooks PortCollisionView PortWorldHolders PortTickPair PortConsole PortSmoke
        PortSpeedrunTimer PortTracker PortModelVariant PortVfx PortTri PortSmokeWorldTeleport''',
}
NAME_CLASS = {n: c for c, names in CLASSES.items() for n in names.split()}
# prefixes for the many one-off helper names of a dropped feature
DROP_PREFIXES = ('PortSmoke', 'PortVfx', 'PortTri', 'PortRemastered', 'PortDread', 'PortConsole',
                 'PortThermalOverride', 'PortVisorGoo', 'PortPresent', 'PortSnapshot', 'PortKssm')
ORDER = ['extra-keep', 'replace', 'keep', 'drop']

COND = re.compile(r'^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)')
NAME = re.compile(r'\bPort[A-Z][A-Za-z0-9_]*')


def classify(name):
    if name in NAME_CLASS:
        return NAME_CLASS[name]
    if name.startswith(DROP_PREFIXES):
        return 'drop'
    return 'keep'


def blocks(path):
    """Yield (start, end, branch_text) for each TARGET_PC branch in a file:
    the lines from a condition naming TARGET_PC to its #else/#elif/#endif."""
    lines = path.read_text(errors='replace').splitlines()
    stack = []  # [is_pc_branch, start]
    for i, line in enumerate(lines, 1):
        m = COND.match(line)
        if not m:
            continue
        kind, rest = m.groups()
        if kind in ('if', 'ifdef', 'ifndef'):
            stack.append([('TARGET_PC' in rest and kind != 'ifndef' and '!' not in rest.split('TARGET_PC')[0][-2:]), i])
        elif kind in ('elif', 'else', 'endif') and stack:
            top = stack[-1]
            if top[0]:
                yield top[1], i, '\n'.join(lines[top[1]:i - 1])
            if kind == 'endif':
                stack.pop()
            else:
                top[0], top[1] = (kind == 'elif' and 'TARGET_PC' in rest), i


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('tree', nargs='?')
    ap.add_argument('--md')
    args = ap.parse_args()
    tree = pathlib.Path(args.tree) if args.tree else (ROOT if (ROOT / 'src/Kyoto').exists()
                                                       else ROOT / 'ref/MetroidPrimePort')
    rows = []
    for sub in ('src', 'include'):
        for p in sorted((tree / sub).rglob('*')):
            if p.suffix not in ('.cpp', '.c', '.hpp', '.h'):
                continue
            for start, end, text in blocks(p):
                names = sorted(set(NAME.findall(text)))
                cls = min((classify(n) for n in names), key=ORDER.index) if names else 'keep'
                rows.append((p.relative_to(tree).as_posix(), start, end, cls, names))
    count = collections.Counter(r[3] for r in rows)
    files = len({r[0] for r in rows})
    summary = f'{len(rows)} TARGET_PC blocks in {files} files: ' + ', '.join(f'{c} {count[c]}' for c in ORDER)
    print(summary)
    if args.md:
        out = ['# TARGET_PC blocks', '',
               'Generated by `tools/port_blocks.py --md docs/port-blocks.md`; do not edit.', '',
               summary + '.', '',
               '| file | lines | class | Port* names |', '|---|---|---|---|']
        out += [f'| `{f}` | {s}-{e} | {c} | {" ".join(n) if n else ""} |' for f, s, e, c, n in rows]
        pathlib.Path(args.md).write_text('\n'.join(out) + '\n', newline='\n')


if __name__ == '__main__':
    main()

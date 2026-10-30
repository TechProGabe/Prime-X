#!/usr/bin/env python3
"""Reference frames from Dolphin: boot your own GM8E01 image in Dolphin straight
into a new game in a chosen room (as MP_BOOT_WORLD does in the port), dump the
frames and keep one PNG every few seconds (docs/testing.md "Dolphin
reference frames").

  python tools/xbox/dolphin_ref.py plaza                     # a room_sweep.sh room
  python tools/xbox/dolphin_ref.py 39F2DE28:B9ABCD56         # <MLVL>[:<MREA>] (prime_rooms.py)
  python tools/xbox/dolphin_ref.py plaza --secs 120 --every 2
  python tools/xbox/dolphin_ref.py plaza --gui               # Dolphin's window, to play it yourself
  python tools/xbox/dolphin_ref.py iceruins --visor thermal   # a visor held (scan, xray, thermal)

How it gets there, no save and no input needed: two Gecko codes on the
retail executable (GM8E01 rev 0, addresses from PrimeDecomp's symbols.txt):
- CMainFlow::SetGameState (0x80023598): asked for the front end, it goes to
  the game instead, after giving the game state the new game's world (the
  Frigate, 158EFE17) and its backup, as MP_BOOT_WORLD does;
- CGameState::SetCurrentWorldId (0x801D3964): given the Frigate, it gives
  the world asked for, and sets that world's desired area to the room asked
  for (retail's own CWorldState field, as MP_BOOT_WORLD does).
The front end is skipped every time (a game over comes back to the room).

Dolphin runs from its own user folder (--user, default C:\\xemu\\dolphin\\px-user,
made on first use), so your own Dolphin settings are untouched. Frames go to
build-xbox/shots/dolphin/<room>/SSSS.png, named by the second of the dump
they come from. Needs ffmpeg on the PATH.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DOLPHIN = r'C:\xemu\dolphin\Dolphin-x64\Dolphin.exe'
ISO = r'C:\xemu\prime\Metroid Prime (USA) (v1.00).iso'
USER = r'C:\xemu\dolphin\px-user'

# room_sweep.sh's rooms
ROOMS = {
    'landing': '39F2DE28', 'frigcrash': '39F2DE28:B9ABCD56', 'temple': '39F2DE28:2398E906',
    'plaza': '83F6FF6F:D5CDB809', 'shrine': '83F6FF6F:DF746AE0', 'sunlobby': '83F6FF6F:3D238FCD',
    'shorelines': 'A8BE6291:F7285979', 'iceruins': 'A8BE6291:B33A0620', 'icetemple': 'A8BE6291:6655F51E',
    'lavalake': '3EF8237C:A4719C6A', 'twinfires': '3EF8237C:E4A4462E', 'quarry': 'B1AC4D65:643D038F',
    'phazontunnel': 'B1AC4D65:BBFA4AB3', 'crater': 'C13B09D1:93668996', 'frigate': '158EFE17',
}

FRIGATE = 0x158EFE17
GP_GAME_STATE = 0x805A8C40       # .sbss gpGameState
SET_GAME_STATE_HOOK = 0x80023598  # stw r4, 0x14(r3): x14_gameState = state (r3 this, r31 the queue)
SET_WORLD_HOOK = 0x801D3964       # stw r31, 0x84(r30): after StateForWorld, r3 = the world's state
SET_CURRENT_WORLD_ID = 0x801D3944
STATE_FOR_WORLD = 0x801D39D8
WRITE_BACKUP_BUF = 0x801D3BB4
kCFS_FrontEnd, kCFS_Game = 8, 14


def lis(rd, imm): return (15 << 26) | (rd << 21) | ((imm >> 16) & 0xFFFF)
def ori(ra, rs, imm): return (24 << 26) | (rs << 21) | (ra << 16) | (imm & 0xFFFF)
def li(rd, imm): return (14 << 26) | (rd << 21) | (imm & 0xFFFF)
def cmpwi(ra, imm): return (11 << 26) | (ra << 16) | (imm & 0xFFFF)
def cmplw(ra, rb): return (31 << 26) | (ra << 16) | (rb << 11) | (32 << 1)
def bne(off): return (16 << 26) | (4 << 21) | (2 << 16) | (off & 0xFFFC)
def mr(ra, rs): return (31 << 26) | (rs << 21) | (ra << 16) | (rs << 11) | (444 << 1)
def mtctr(rs): return 0x7C0903A6 | (rs << 21)
def stw(rs, d, ra): return (36 << 26) | (rs << 21) | (ra << 16) | (d & 0xFFFF)
def lwz(rd, d, ra): return (32 << 26) | (rd << 21) | (ra << 16) | (d & 0xFFFF)
BCTRL, NOP = 0x4E800421, 0x60000000


def ha(x): return ((x + 0x8000) >> 16) << 16          # lis for a signed low half
def lo(x): return x & 0xFFFF
def load(rd, x): return [lis(rd, x), ori(rd, rd, x)]
def call(fn): return load(12, fn) + [mtctr(12), BCTRL]


def c2(addr, body, skip_at):
    """A Gecko C2 (insert) code; body's last word is the instruction replaced,
    body[skip_at] a bne to it."""
    body = list(body)
    body[skip_at] = bne((len(body) - 1 - skip_at) * 4)
    body += [NOP, 0] if len(body) % 2 == 0 else [0]
    lines = ['C2%06X %08X' % (addr & 0x1FFFFFF, len(body) // 2)]
    return lines + ['%08X %08X' % (body[i], body[i + 1]) for i in range(0, len(body), 2)]


def gecko(world, area):
    skip_front_end = [
        cmpwi(4, kCFS_FrontEnd), None,
        stw(3, 8, 1),                                            # this, in a local not yet live
        lis(12, ha(GP_GAME_STATE)), lwz(3, lo(GP_GAME_STATE), 12),
        *load(4, FRIGATE), *call(SET_CURRENT_WORLD_ID),           # what CFrontEndUI's new game gives it
        lis(12, ha(GP_GAME_STATE)), lwz(3, lo(GP_GAME_STATE), 12),
        *call(WRITE_BACKUP_BUF),
        lwz(3, 8, 1), li(4, kCFS_Game),
        stw(4, 0x14, 3),
    ]
    to_room = [
        *load(12, FRIGATE), cmplw(31, 12), None,
        *load(31, world),
        mr(3, 30), mr(4, 31), *call(STATE_FOR_WORLD),             # StateForWorld(this, world)
        *load(12, area), stw(12, 0x10, 3),                        # x10_desiredAreaAssetId
        stw(31, 0x84, 30),
    ]
    return c2(SET_GAME_STATE_HOOK, skip_front_end, 1) + c2(SET_WORLD_HOOK, to_room, 3)


VISORS = {'combat': 0, 'xray': 1, 'scan': 2, 'thermal': 3}


def visor_code(visor):
    """Every frame: CPlayerState's current and transitioning visor (x14, x18)
    = visor, through gpGameState -> x98_playerState (an rc_ptr: its CRefData,
    then the object). Pointer codes, each step checked to be in MEM1."""
    return ['48000000 %08X' % GP_GAME_STATE, 'DE000000 80008180',
            '58010000 00000098', 'DE000000 80008180',
            '58010000 00000000', 'DE000000 80008180',
            '14000014 %08X' % visor, '14000018 %08X' % visor,
            'E0000000 80008000']


def write_ini(path, sections):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', newline='\n') as f:
        for name, body in sections:
            f.write('[%s]\n' % name)
            for line in body:
                f.write(line + '\n')


def setup_user(user, world, area, visor=None):
    write_ini(os.path.join(user, 'Config', 'Dolphin.ini'), [
        ('Analytics', ['Enabled = False', 'PermissionAsked = True']),
        ('AutoUpdate', ['UpdateTrack = ']),
        ('Core', ['EnableCheats = True', 'SIDevice0 = 6', 'SlotA = 1', 'SlotB = 255', 'CPUThread = True']),
        ('Interface', ['ConfirmStop = False', 'UsePanicHandlers = False', 'OnScreenDisplayMessages = False']),
        ('Display', ['RenderToMain = False', 'Fullscreen = False']),
        ('Movie', ['DumpFramesSilent = True']),
        ('General', ['ISOPath0 = ' + os.path.dirname(ISO), 'ISOPaths = 1']),
    ])
    write_ini(os.path.join(user, 'Config', 'GFX.ini'), [
        ('Settings', ['InternalResolution = 1', 'AspectRatio = 2', 'InternalResolutionFrameDumps = True',
                      'DumpCodec = ffv1', 'DumpFormat = avi']),
    ])
    codes = ['$Prime-X boot room'] + gecko(world, area)
    enabled = ['$Prime-X boot room']
    if visor is not None:
        codes += ['$Prime-X visor'] + visor_code(visor)
        enabled += ['$Prime-X visor']
    write_ini(os.path.join(user, 'GameSettings', 'GM8E01.ini'), [('Gecko', codes), ('Gecko_Enabled', enabled)])


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('room', help='a room_sweep.sh name (%s) or <MLVL>[:<MREA>]' % ', '.join(ROOMS))
    ap.add_argument('--secs', type=int, default=90, help='how long Dolphin runs (default 90)')
    ap.add_argument('--every', type=float, default=5, help='keep a frame every this many seconds (default 5)')
    ap.add_argument('--out', help='default build-xbox/shots/dolphin/<room>')
    ap.add_argument('--dolphin', default=DOLPHIN)
    ap.add_argument('--iso', default=ISO)
    ap.add_argument('--user', default=USER)
    ap.add_argument('--gui', action='store_true', help="Dolphin's window, no dump: play it yourself")
    ap.add_argument('--visor', choices=sorted(VISORS), help='hold this visor (written every frame)')
    args = ap.parse_args()

    spec = ROOMS.get(args.room.lower(), args.room)
    world, _, area = spec.partition(':')
    world, area = int(world, 16), int(area, 16) if area else 0xFFFFFFFF
    name = args.room.lower() if args.room.lower() in ROOMS else spec.replace(':', '_')
    if args.visor:
        name += '_' + args.visor
    user = os.path.abspath(args.user)
    setup_user(user, world, area, VISORS.get(args.visor))
    print('[dolphin] world %08X area %08X, user folder %s' % (world, area, user))

    if args.gui:
        subprocess.Popen([args.dolphin, '-u', user, '-e', args.iso])
        return

    dump = os.path.join(user, 'Dump', 'Frames')
    shutil.rmtree(dump, ignore_errors=True)
    os.makedirs(dump)
    proc = subprocess.Popen([args.dolphin, '-u', user, '-b', '-C', 'Dolphin.Movie.DumpFrames=True', '-e', args.iso])
    try:
        proc.wait(timeout=args.secs)
    except subprocess.TimeoutExpired:
        # a close request first, so the dump's index is written
        subprocess.run(['taskkill', '/PID', str(proc.pid)], capture_output=True)
        try:
            proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            subprocess.run(['taskkill', '/F', '/PID', str(proc.pid)], capture_output=True)
            proc.wait()
    time.sleep(1)
    dumps = sorted(f for f in os.listdir(dump) if f.lower().endswith('.avi'))
    if not dumps:
        sys.exit('[dolphin] no frame dump in %s' % dump)
    out = args.out or os.path.join(ROOT, 'build-xbox', 'shots', 'dolphin', name)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    subprocess.run(['ffmpeg', '-v', 'error', '-i', os.path.join(dump, dumps[0]), '-vf', 'fps=1/%g' % args.every,
                    os.path.join(out, 'f%04d.png')], check=True)
    for f in sorted(os.listdir(out)):
        n = int(f[1:5])
        os.replace(os.path.join(out, f), os.path.join(out, '%04d.png' % round((n - 1) * args.every)))
    print('[dolphin] %d frames in %s' % (len(os.listdir(out)), out))


if __name__ == '__main__':
    main()

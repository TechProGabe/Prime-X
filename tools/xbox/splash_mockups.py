#!/usr/bin/env python3
"""Mock-ups of the boot splash's looks (docs/ui.md "Boot splash"), drawn
the way xhw_splash.c draws (640x480, unscii-16 at 1x/2x/3x, no other assets)
so a look can be picked before it is built.

  python tools/xbox/splash_mockups.py <out dir>     # splash_A.png .. splash_D.png + splash_options.png

Each is the first boot of an image, the disc check 27% done.
"""
import math
import pathlib
import random
import re
import sys

from PIL import Image, ImageDraw

W, H = 640, 480
FONT_H = pathlib.Path('C:/xdev/nxdk/lib/hal/font_unscii_16.h')
GLYPHS = [int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]{2}),', FONT_H.read_text().split('#define FONT_VMIRROR 0', 1)[1])]

TITLE = 'TechProGabe Presents...'
GAME = 'METROID PRIME  -  GM8E01  -  USA 1.00'
NOTE1 = 'First boot of this disc image: checking every file, once.'
NOTE2 = 'About a minute.   B: skip (it checks again next boot)'
HINT = 'Hold BACK for safe video (480i)'
PCT = 0.27


def hexc(s):
    return tuple(int(s[i:i + 2], 16) for i in (1, 3, 5))


class Canvas:
    def __init__(self):
        self.img = Image.new('RGB', (W, H))
        self.px = self.img.load()

    def put(self, x, y, c):
        if 0 <= x < W and 0 <= y < H:
            self.px[x, y] = c

    def blend(self, x, y, c, a):
        if 0 <= x < W and 0 <= y < H:
            o = self.px[x, y]
            self.px[x, y] = tuple(int(o[i] + (c[i] - o[i]) * a) for i in range(3))

    def text(self, s, x, y, zoom, c, n=None):
        for ch in s[:n]:
            g = GLYPHS[ord(ch) * 16:(ord(ch) + 1) * 16]
            for gy in range(16):
                for gx in range(8):
                    if g[gy] & (0x80 >> gx):
                        for zy in range(zoom):
                            for zx in range(zoom):
                                self.put(x + gx * zoom + zx, y + gy * zoom + zy, c)
            x += 8 * zoom

    def width(self, s, zoom):
        return len(s) * 8 * zoom

    def centered(self, s, y, zoom, c):
        self.text(s, (W - self.width(s, zoom)) // 2, y, zoom, c)

    def glow(self, s, x, y, zoom, c, g, n=None):
        for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            self.text(s, x + dx, y + dy, zoom, g, n)
        self.text(s, x, y, zoom, c, n)

    def rect(self, x, y, w, h, c):
        for j in range(y, y + h):
            for i in range(x, x + w):
                self.put(i, j, c)


def bracket(cv, x, y, sx, sy, c, arm=22, t=3):
    cv.rect(x if sx > 0 else x - arm + 1, y if sy > 0 else y - t + 1, arm, t, c)
    cv.rect(x if sx > 0 else x - t + 1, y if sy > 0 else y - arm + 1, t, arm, c)


def scan_bar(cv, y, label, fg, track, tick, text, done_w=400):
    x0 = 120
    for i in range(11):
        h = 7 if i % 5 == 0 else 4
        cv.rect(x0 + i * 399 // 10, y - h, 1, h, tick)
    cv.rect(x0, y, done_w, 4, track)
    cv.rect(x0, y, int(done_w * PCT), 4, fg)
    cv.text(label, x0, y - 24, 1, text)
    p = f'{int(PCT * 100)}%'
    cv.text(p, x0 + done_w - cv.width(p, 1), y - 24, 1, text)


# ---- A: scan visor (built: xhw_splash.c) ----
def option_a():
    cv = Canvas()
    cyan, dimc, text, hint = hexc('#7fe9ff'), hexc('#2a6a78'), hexc('#bfeef7'), hexc('#5f9aa6')
    for y in range(H):
        t = y / (H - 1)
        base = [2 + 5 * t, 6 + 36 * t, 8 + 43 * t]
        scan = 1.0625 if y % 4 == 0 else 1.0
        for x in range(W):
            dx, dy = x - W / 2, y - H / 2
            v = max(96, 256 - (dx * dx * 0.75 + dy * dy) / 700) / 256
            c = [int(b * scan * v) for b in base]
            if x % 32 == 16 and y % 32 == 16:
                a = max(0, 120 - (dx * dx * 0.75 + dy * dy) / 900) / 256
                c = [int(c[i] + (cyan[i] - c[i]) * a) for i in range(3)]
            cv.px[x, y] = tuple(c)
    tw = cv.width(TITLE, 3)
    tx = (W - tw) // 2
    for (x, y, sx, sy) in ((tx - 24, 130, 1, 1), (tx + tw + 23, 130, -1, 1), (tx - 24, 240, 1, -1), (tx + tw + 23, 240, -1, -1)):
        bracket(cv, x, y, sx, sy, cyan)
    cv.glow(TITLE, tx, 150, 3, cyan, dimc)
    cv.centered(GAME, 212, 1, text)
    scan_bar(cv, 300, 'VERIFYING DISC IMAGE', cyan, (10, 34, 40), dimc, text)
    cv.centered(NOTE1, 322, 1, text)
    cv.centered(NOTE2, 342, 1, hint)
    cv.centered(HINT, H - 64, 1, hint)
    return cv.img


# ---- B: combat visor: the helmet's frame, an energy-tank bar ----
def option_b():
    cv = Canvas()
    teal, tealdim, white, amber = hexc('#8fe3e8'), hexc('#3b7f86'), hexc('#e6fbff'), hexc('#ffb547')
    for y in range(H):
        t = y / (H - 1)
        for x in range(W):
            # Tallon-green haze behind the glass
            dx, dy = (x - W / 2) / W, (y - H * 0.55) / H
            v = max(0.0, 1 - (dx * dx + dy * dy) * 3.2)
            cv.px[x, y] = (int(4 + 10 * v), int(14 + 36 * v + 6 * t), int(14 + 30 * v))
    # the helmet: dark arcs top and bottom, the visor glass's rim
    for x in range(W):
        u = (x - W / 2) / (W / 2)
        top = int(46 - 34 * (1 - u * u))
        bot = int(H - 46 + 34 * (1 - u * u))
        for y in range(0, top):
            cv.put(x, y, (6, 10, 12))
        for y in range(bot, H):
            cv.put(x, y, (6, 10, 12))
        cv.put(x, top, tealdim)
        cv.put(x, top + 1, teal)
        cv.put(x, bot, teal)
        cv.put(x, bot - 1, tealdim)
    # side ladders, as the HUD's
    for i in range(12):
        cv.rect(40, 120 + i * 20, 10 if i % 3 else 18, 2, tealdim)
        cv.rect(W - 50 if i % 3 else W - 58, 120 + i * 20, 10 if i % 3 else 18, 2, tealdim)
    cv.centered('[ PRIME-X ]', 92, 1, tealdim)
    cv.glow(TITLE, (W - cv.width(TITLE, 3)) // 2, 140, 3, white, tealdim)
    cv.centered(GAME, 202, 1, teal)
    # energy-tank style progress: 20 segments
    cv.text('[ VERIFYING DISC IMAGE ]', 120, 262, 1, teal)
    p = f'{int(PCT * 100):02d}'
    cv.text(p, 520 - cv.width(p, 2), 254, 2, white)
    for i in range(20):
        on = i < int(20 * PCT)
        cv.rect(120 + i * 20, 284, 17, 9, amber if on else (22, 44, 46))
    cv.centered(NOTE1, 312, 1, white)
    cv.centered(NOTE2, 332, 1, teal)
    cv.centered(HINT, H - 74, 1, tealdim)
    return cv.img


# ---- C: the pause screen's amber (inventory and log book) ----
def option_c():
    cv = Canvas()
    orange, amber, cream, brown = hexc('#ff8a2a'), hexc('#ffb547'), hexc('#ffe7c2'), hexc('#7a3f12')
    for y in range(H):
        t = y / (H - 1)
        for x in range(W):
            stripe = 1.08 if y % 3 == 0 else 1.0
            dx, dy = x - W / 2, y - H / 2
            v = max(0.35, 1 - (dx * dx * 0.75 + dy * dy) / 140000)
            cv.px[x, y] = tuple(int(c * stripe * v) for c in (58 + 20 * t, 34 + 10 * t, 14 + 4 * t))
    # the inventory's panel: a bevelled frame with notched corners
    x0, y0, x1, y1 = 56, 96, W - 57, 372
    for i in range(3):
        c = (orange, amber, brown)[i]
        cv.rect(x0 + 14 + i, y0 + i, x1 - x0 - 28 - 2 * i, 1, c)
        cv.rect(x0 + 14 + i, y1 - i, x1 - x0 - 28 - 2 * i, 1, c)
        cv.rect(x0 + i, y0 + 14 + i, 1, y1 - y0 - 28 - 2 * i, c)
        cv.rect(x1 - i, y0 + 14 + i, 1, y1 - y0 - 28 - 2 * i, c)
    for k in range(14):
        for (cx, cy, sx, sy) in ((x0, y0, 1, 1), (x1, y0, -1, 1), (x0, y1, 1, -1), (x1, y1, -1, -1)):
            cv.put(cx + sx * k, cy + sy * (14 - k), orange)
    cv.centered('[ Prime-X ]', 108, 1, amber)
    cv.glow(TITLE, (W - cv.width(TITLE, 3)) // 2, 140, 3, cream, brown)
    cv.centered(GAME, 202, 1, amber)
    scan_bar(cv, 290, 'VERIFYING DISC IMAGE', orange, (40, 20, 8), brown, cream)
    cv.centered(NOTE1, 312, 1, cream)
    cv.centered(NOTE2, 332, 1, amber)
    cv.centered(HINT, H - 64, 1, amber)
    return cv.img


# ---- D: deep space over Tallon IV (closest to N64Recomp-X's sky) ----
def option_d():
    cv = Canvas()
    rnd = random.Random(7)
    white, ice, violet, phazon = (240, 246, 255), hexc('#a9d8ff'), hexc('#6a5acd'), hexc('#3fd0ff')
    for y in range(H):
        for x in range(W):
            nx, ny = x / W, y / H
            # a violet nebula and Tallon IV's limb glowing at the bottom
            neb = math.exp(-((nx - 0.28) ** 2 + (ny - 0.30) ** 2) * 9) * 0.55 + \
                math.exp(-((nx - 0.78) ** 2 + (ny - 0.18) ** 2) * 14) * 0.35
            r = 4 + 40 * neb
            g = 6 + 18 * neb
            b = 18 + 70 * neb
            d = math.hypot(nx - 0.5, (ny - 1.55) * 0.9)
            if d < 0.78:
                k = 0.78 - d
                r, g, b = r + 30 * k, g + 70 * k, b + 60 * k
                if d < 0.72:
                    r, g, b = 10 + 14 * k * 10, 26 + 30 * k * 10, 22 + 18 * k * 10
            cv.px[x, y] = (min(255, int(r)), min(255, int(g)), min(255, int(b)))
    for _ in range(260):
        x, y = rnd.randrange(W), rnd.randrange(int(H * 0.8))
        a = rnd.random()
        cv.blend(x, y, white, 0.3 + 0.7 * a)
        if a > 0.93:
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                cv.blend(x + dx, y + dy, ice, 0.5)
    cv.glow(TITLE, (W - cv.width(TITLE, 3)) // 2, 126, 3, white, (40, 50, 110))
    cv.centered('Metroid Prime', 190, 2, ice)
    cv.centered('GM8E01  -  USA 1.00  -  Redump verified on first boot', 228, 1, (140, 160, 210))
    x0, y0 = 120, 300
    cv.text('Verifying disc image (first boot only)', x0, y0 - 20, 1, white)
    cv.rect(x0, y0, 400, 8, (20, 24, 52))
    for i in range(int(400 * PCT)):
        t = i / 400
        cv.rect(x0 + i, y0, 1, 8, tuple(int(phazon[k] * (1 - t) + violet[k] * t) for k in range(3)))
    p = f'{int(PCT * 100)}%'
    cv.text(p, x0 + 400 - cv.width(p, 1), y0 - 20, 1, ice)
    cv.centered('Every file is checked once; about a minute.', 322, 1, (190, 200, 230))
    cv.centered('B: skip (it checks again next boot)', 342, 1, (130, 145, 190))
    cv.centered(HINT, H - 64, 1, (130, 145, 190))
    cv.text('Prime-X dev', W - 32 - cv.width('Prime-X dev', 1), H - 40, 1, (90, 100, 150))
    return cv.img


# ---- E: A as a haunted terminal: an Aliens-style CRT readout over falling
# glyph rain (the user, 2026-10-27: "cooler, a bit spooky and tech like") ----
def option_e():
    cv = Canvas()
    rnd = random.Random(1979)
    cyan, phos, dimc = hexc('#7fe9ff'), hexc('#4dffc4'), hexc('#1f5a5e')
    text, hint, red = hexc('#bff7f0'), hexc('#4f8f8a'), hexc('#ff3b3b')
    # near-black glass
    for y in range(H):
        for x in range(W):
            cv.px[x, y] = (1, 6, 6)
    # glyph rain: columns of hex, symbols and Latin-1 marks falling, bright
    # heads and fading tails, dimmer towards the middle where the card sits
    pool = '0123456789ABCDEF:;<>=*+#%&$@' + ''.join(chr(c) for c in (0xA4, 0xA7, 0xB1, 0xB5, 0xB6, 0xD0, 0xDE, 0xF0, 0xFE, 0xD8, 0xC6, 0xBF))
    for col in range(0, W, 10):
        if rnd.random() < 0.15:
            continue
        head = rnd.randrange(-4, H // 16 + 8)
        length = rnd.randrange(8, 26)
        mid = abs(col - W / 2) / (W / 2)
        strength = 0.22 + 0.6 * mid
        for k in range(length):
            row = head - k
            if row < 0 or row * 16 >= H:
                continue
            fade = (1 - k / length) * strength
            c = (230, 255, 245) if k == 0 else phos
            c = tuple(int(c[i] * (0.25 + 0.75 * fade) * fade + 4) for i in range(3))
            cv.text(rnd.choice(pool), col, row * 16, 1, c)
    # the card: a darker panel the rain falls behind, framed by the brackets
    px0, py0, px1, py1 = 70, 118, W - 70, 404
    for y in range(py0, py1):
        for x in range(px0, px1):
            o = cv.px[x, y]
            cv.px[x, y] = tuple(int(o[i] * 0.12) + (1, 7, 8)[i] for i in range(3))
    for (x, y, sx, sy) in ((px0, py0, 1, 1), (px1 - 1, py0, -1, 1), (px0, py1 - 1, 1, -1), (px1 - 1, py1 - 1, -1, -1)):
        bracket(cv, x, y, sx, sy, cyan, arm=26)
    cv.rect(px0 + 30, py0, px1 - px0 - 60, 1, dimc)
    cv.rect(px0 + 30, py1 - 1, px1 - px0 - 60, 1, dimc)
    # the boot log, typed out in the top-left like MU/TH/UR's
    log = ['CHOZO ARCHIVE INTERFACE  v0.1',
           '> CPU   PENTIUM III 733 MHZ ........ OK',
           '> MEM   64 MB   MEM1 24 / ARAM 16 .. OK',
           '> DISC  GM8E01  REV 00 ............ FOUND']
    for i, l in enumerate(log):
        cv.rect(36, 30 + i * 16, cv.width(l, 1) + 4, 16, (1, 6, 6))
        cv.text(l, 38, 30 + i * 16, 1, phos if i else cyan)
    # the credit, small, then the headline: PRIME-X with chromatic fringes
    # (red left, cyan right), a glow and a block cursor
    cv.centered('TechProGabe Presents...', 134, 1, hint)
    head = 'PRIME-X'
    hz = 5
    hw = cv.width(head, hz)
    hx = (W - hw) // 2 - 10
    cv.text(head, hx - 3, 152, hz, (90, 10, 14))
    cv.text(head, hx + 3, 152, hz, (0, 70, 90))
    cv.glow(head, hx, 152, hz, cyan, dimc)
    cv.rect(hx + hw + 6, 160, 26, 64, cyan)
    cv.centered('METROID PRIME  -  GM8E01  -  USA 1.00', 240, 1, text)
    # a signal trace across the card, as if something is listening
    for x in range(px0 + 30, px1 - 30):
        t = (x - px0) / 24.0
        v = math.sin(t) * 3 + (rnd.random() - 0.5) * 2
        if 300 < x < 340:
            v += math.sin((x - 300) / 40 * math.pi) * 9 * math.sin(t * 3)
        cv.put(x, int(262 + v), dimc)
        cv.put(x, int(262 + v) + 1, (10, 40, 44))
    # what it is doing, the second biggest line, then its bar, the file
    # being checked and a hex dump
    x0, y0 = 120, 312
    task = 'VERIFYING DISC IMAGE'
    cv.glow(task, x0, 278, 2, text, (8, 40, 44))
    p = f'{int(PCT * 100)}%'
    cv.text(p, x0 + 400 - cv.width(p, 2), 278, 2, cyan)
    for i in range(40):
        on = i < int(40 * PCT)
        cv.rect(x0 + i * 10, y0, 8, 6, cyan if on else (8, 30, 34))
    cv.text('> Metroid4.pak ........... CRC 7F3A91C2  OK', x0, y0 + 14, 1, phos)
    cv.text('  04B3C000  9A 3F 00 1C E4 07 B2 55 0D', x0, y0 + 30, 1, dimc)
    cv.centered(NOTE1, 362, 1, text)
    cv.centered(NOTE2, 380, 1, hint)
    hw = cv.width(HINT, 1)
    cv.rect((W - hw) // 2 - 8, H - 54, hw + 16, 20, (1, 6, 6))
    cv.centered(HINT, H - 52, 1, hint)
    sx = W - 40 - cv.width('// SIGNAL', 1)
    cv.rect(sx - 16, 30, cv.width('// SIGNAL', 1) + 20, 16, (1, 6, 6))
    cv.text('// SIGNAL', sx, 30, 1, red)
    cv.rect(sx - 12, 35, 6, 6, red)
    # CRT: scanlines, a glitch band, noise, and dark rounded corners
    glitch_y = 258
    for y in range(H):
        row = [cv.px[x, y] for x in range(W)]
        shift = 4 if glitch_y <= y < glitch_y + 3 else 0
        for x in range(W):
            c = row[x - shift] if shift else row[x]
            k = 0.68 if y % 2 else 1.0
            if shift:
                k *= 1.25
            n = rnd.random() * 6
            dx, dy = (x - W / 2) / (W / 2), (y - H / 2) / (H / 2)
            corner = max(0.0, (dx * dx) ** 2 + (dy * dy) ** 2 - 0.8)
            v = max(0.0, 1 - corner * 2.2) * (1 - 0.18 * (dx * dx + dy * dy))
            cv.px[x, y] = tuple(max(0, min(255, int((c[i] * k + n) * v))) for i in range(3))
    return cv.img


def main():
    out = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else '.')
    out.mkdir(parents=True, exist_ok=True)
    if len(sys.argv) > 2:   # one option: splash_mockups.py <out> E
        for k in sys.argv[2:]:
            globals()['option_' + k.lower()]().save(out / f'splash_{k}.png')
        print('wrote', out)
        return
    imgs = {'A': option_a(), 'B': option_b(), 'C': option_c(), 'D': option_d()}
    for k, im in imgs.items():
        im.save(out / f'splash_{k}.png')
    sheet = Image.new('RGB', (W * 2 + 12, H * 2 + 12 + 56), (30, 30, 30))
    names = {'A': 'A  Scan visor (built)', 'B': 'B  Combat visor HUD', 'C': 'C  Pause-menu amber', 'D': 'D  Deep space over Tallon IV'}
    d = ImageDraw.Draw(sheet)
    for i, k in enumerate('ABCD'):
        x, y = (i % 2) * (W + 12), (i // 2) * (H + 40)
        sheet.paste(imgs[k], (x, y + 28))
        d.text((x + 6, y + 8), names[k], fill=(235, 235, 235))
    sheet.save(out / 'splash_options.png')
    print('wrote', out)


if __name__ == '__main__':
    main()

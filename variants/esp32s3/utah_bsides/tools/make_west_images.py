#!/usr/bin/env python3
"""Draws the Old West pictures for the name tag (160x128, white on black): the raven, the
wanted poster, the skinwalker, the marshal's star, the ghost town, don't answer, and the
campfire, howl, tracks and mine animations.

    python3 make_west_images.py ../images [../../../../userPrefs.jsonc]

With userPrefs.jsonc given it also puts the still raven on Meshtastic's boot screen.
"""
import math
import os
import random
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

W, H = 160, 128
OUT = sys.argv[1]
SERIF = "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf"
SERIFC = "/usr/share/fonts/truetype/dejavu/DejaVuSerifCondensed-Bold.ttf"
SANS = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"


def font(path, size):
    return ImageFont.truetype(path, size)


def new():
    im = Image.new("1", (W, H), 0)
    return im, ImageDraw.Draw(im)


def ctext(d, y, text, f, x0=0, x1=W):
    w = d.textlength(text, font=f)
    d.text(((x0 + x1 - w) / 2, y), text, font=f, fill=1)


def wide_text(text, path, size, stretch=1.35):
    """Text drawn then stretched sideways, for a wood-type poster look."""
    f = font(path, size)
    tmp = Image.new("1", (400, size * 2), 0)
    td = ImageDraw.Draw(tmp)
    td.text((2, 0), text, font=f, fill=1)
    box = tmp.getbbox()
    tmp = tmp.crop(box)
    return tmp.resize((int(tmp.width * stretch), tmp.height))


def coyote_head(d, cx, cy, s, fill=1, eyes=0):
    """Front-on coyote head (the skinwalker on the poster)."""
    pts = [
        (cx - 20 * s, cy - 22 * s), (cx - 10 * s, cy - 8 * s), (cx + 10 * s, cy - 8 * s),
        (cx + 20 * s, cy - 22 * s), (cx + 18 * s, cy - 2 * s), (cx + 14 * s, cy + 6 * s),
        (cx + 6 * s, cy + 14 * s), (cx + 3 * s, cy + 24 * s), (cx - 3 * s, cy + 24 * s),
        (cx - 6 * s, cy + 14 * s), (cx - 14 * s, cy + 6 * s), (cx - 18 * s, cy - 2 * s),
    ]
    d.polygon(pts, fill=fill)
    # ears' insides, eyes slanted, nose
    for side in (-1, 1):
        d.polygon([(cx + side * 17 * s, cy - 17 * s), (cx + side * 11 * s, cy - 9 * s),
                   (cx + side * 15 * s, cy - 8 * s)], fill=1 - fill)
        d.polygon([(cx + side * 4 * s, cy + 0 * s), (cx + side * 12 * s, cy - 3 * s),
                   (cx + side * 10 * s, cy + 2 * s)], fill=eyes if eyes else 1 - fill)
    d.rectangle([cx - 2 * s, cy + 20 * s, cx + 2 * s, cy + 23 * s], fill=1 - fill)


def wanted():
    im, d = new()
    d.rectangle([0, 0, W - 1, H - 1], outline=1)
    d.rectangle([3, 3, W - 4, H - 4], outline=1)
    # nail holes in the corners
    for x, y in ((7, 7), (W - 8, 7), (7, H - 8), (W - 8, H - 8)):
        d.ellipse([x - 1, y - 1, x + 1, y + 1], fill=1)
    t = wide_text("WANTED", SERIF, 22, 1.3)
    im.paste(t, ((W - t.width) // 2, 8))
    # portrait frame
    d.rectangle([52, 36, 107, 88], outline=1)
    coyote_head(d, 80, 63, 0.95)
    f = font(SERIFC, 10)
    ctext(d, 91, "DEAD OR ALIVE", f)
    f2 = font(SANS, 8)
    ctext(d, 103, "FOR JAMMING THE MESH", f2)
    ctext(d, 113, "REWARD: 500 PACKETS", f2)
    im.save(f"{OUT}/04_wanted.png")


def skinwalker():
    random.seed(7)
    stars = [(random.randrange(W), random.randrange(70)) for _ in range(40)]
    frames = []
    for f in range(16):
        im, d = new()
        for i, (x, y) in enumerate(stars):
            if (i + f) % 7:  # a few twinkle off each frame
                d.point((x, y), fill=1)
        mx, my, r = 104, 46, 34
        d.ellipse([mx - r, my - r, mx + r, my + r], fill=1)
        # mesas along the bottom: filled white, a black notch canyon
        d.polygon([(0, 104), (18, 104), (24, 96), (52, 96), (58, 106), (90, 106), (96, 92),
                   (138, 92), (146, 104), (W, 104), (W, H), (0, H)], fill=1)
        # juniper on the ridge
        for k in range(5):
            d.ellipse([18 + k, 86 - k * 4, 32 - k, 96 - k * 4], fill=1)
        gone = 10 <= f <= 12  # it vanishes for a moment...
        if not gone:
            x = 98 if f < 10 else 112  # ...and comes back somewhere else
            # tall thin figure, coyote head, against the moon (black on white)
            d.polygon([(x - 3, 92), (x - 1, 62), (x + 1, 62), (x + 3, 92)], fill=0)        # legs/body
            d.polygon([(x - 6, 66), (x - 2, 44), (x + 2, 44), (x + 6, 66)], fill=0)        # torso
            d.line([(x - 5, 48), (x - 11, 70)], fill=0, width=2)                       # long arms
            d.line([(x + 5, 48), (x + 11, 70)], fill=0, width=2)
            d.polygon([(x - 5, 42), (x - 6, 30), (x - 2, 36), (x + 2, 36), (x + 6, 30), (x + 5, 42),
                       (x + 2, 46), (x - 2, 46)], fill=0)                                # head and ears
            blink = f in (4, 5)
            if not blink:
                d.point([(x - 2, 40), (x + 2, 40)], fill=1)                          # eyes catch the light
        frames.append(im.convert("L"))
    durs = [300] * 16
    durs[9] = 900   # it stares
    durs[12] = 700  # empty moon
    frames[0].save(f"{OUT}/05_skinwalker.gif", save_all=True, append_images=frames[1:],
                   duration=durs, loop=0)


def star():
    im, d = new()
    cx, cy, ro, ri = 80, 62, 46, 25
    pts = []
    for k in range(12):
        a = -math.pi / 2 + k * math.pi / 6
        r = ro if k % 2 == 0 else ri
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    d.polygon(pts, outline=1, fill=0)
    inner = [(cx + (p[0] - cx) * 0.82, cy + (p[1] - cy) * 0.82) for p in pts]
    d.polygon(inner, outline=1)
    for k in range(0, 12, 2):  # ball tips
        x, y = pts[k]
        d.ellipse([x - 4, y - 4, x + 4, y + 4], fill=1)
    d.ellipse([cx - 21, cy - 21, cx + 21, cy + 21], outline=1)
    f = font(SERIFC, 8)
    ctext(d, cy - 12, "MESH", f)
    ctext(d, cy - 2, "MARSHAL", f)
    d.line([(cx - 12, cy + 10), (cx + 12, cy + 10)], fill=1)
    f2 = font(SANS, 8)
    ctext(d, H - 10, "UTAH BSIDES  2025", f2)
    im.save(f"{OUT}/06_mesh_marshal.png")


def tumbleweed(d, cx, cy, r, ang):
    for k in range(9):
        a = ang + k * 0.7
        d.arc([cx - r, cy - r, cx + r, cy + r], math.degrees(a), math.degrees(a) + 140, fill=1)
        rr = r * (0.45 + 0.07 * (k % 4))
        d.arc([cx - rr + math.cos(a) * 2, cy - rr, cx + rr + math.cos(a) * 2, cy + rr],
              math.degrees(a * 1.3), math.degrees(a * 1.3) + 200, fill=1)


def ghost_town():
    frames = []
    for f in range(20):
        im, d = new()
        ground = 108
        d.line([(0, ground), (W, ground)], fill=1)
        # saloon: false front, porch, swinging doors, windows
        d.rectangle([28, 40, 132, ground], outline=1)
        d.polygon([(28, 40), (28, 26), (44, 26), (44, 20), (116, 20), (116, 26), (132, 26), (132, 40)], outline=1)
        t = wide_text("SALOON", SERIF, 11, 1.4)
        im.paste(t, ((W - t.width) // 2, 24))
        d.line([(24, 64), (136, 64)], fill=1)  # porch roof
        for x in (30, 130):
            d.line([(x, 64), (x, ground)], fill=1)
        for x0 in (38, 104):  # windows, one broken
            d.rectangle([x0, 46, x0 + 18, 58], outline=1)
            d.line([(x0 + 9, 46), (x0 + 9, 58)], fill=1)
        d.line([(104, 46), (112, 54)], fill=1)
        d.line([(112, 54), (110, 58)], fill=1)
        swing = int(3 * math.sin(f * 0.6))  # the doors creak
        d.rectangle([68, 76, 79, 98], outline=1)
        d.rectangle([81 + swing, 76, 92 + swing, 98], outline=1)
        for y in (82, 88, 94):
            d.line([(70, y), (77, y)], fill=1)
            d.line([(83 + swing, y), (90 + swing, y)], fill=1)
        # wind streaks
        for k in range(3):
            x = (f * 14 + k * 61) % (W + 40) - 20
            d.line([(x, 112 + k * 5), (x + 14, 112 + k * 5)], fill=1)
        # tumbleweed rolling past, bouncing
        x = -14 + f * 9.5
        y = ground - 9 - abs(math.sin(f * 0.8)) * 10
        tumbleweed(d, x, y, 9, f * 0.9)
        frames.append(im.convert("L"))
    frames[0].save(f"{OUT}/07_ghost_town.gif", save_all=True, append_images=frames[1:], duration=120, loop=0)


def dont_answer():
    im, d = new()
    f = font(SERIFC, 13)
    lines = ["IF YOU HEAR", "YOUR NAME", "FROM THE", "JUNIPERS..."]
    for i, ln in enumerate(lines):
        ctext(d, 6 + i * 17, ln, f)
    t = wide_text("DON'T ANSWER", SERIF, 14, 1.15)
    if t.width > W - 8:
        t = t.resize((W - 8, t.height))
    im.paste(t, ((W - t.width) // 2, 84))
    d.line([(20, 108), (140, 108)], fill=1)
    # two eyes watching from the bottom corner
    for x in (128, 140):
        d.polygon([(x - 4, 118), (x + 4, 115), (x + 2, 120)], fill=1)
    im.save(f"{OUT}/08_dont_answer.png")


# ---- drawn at 4x and shrunk, for smooth edges ----------------------------------------------
SS = 4


class Big:
    """A 4x canvas; coordinates are given in screen pixels."""

    def __init__(self):
        self.im = Image.new("L", (W * SS, H * SS), 0)
        self.d = ImageDraw.Draw(self.im)

    def P(self, pts):
        return [(x * SS, y * SS) for x, y in pts]

    def poly(self, pts, fill=255):
        self.d.polygon(self.P(pts), fill=fill)

    def ell(self, cx, cy, rx, ry, fill=255):
        self.d.ellipse([(cx - rx) * SS, (cy - ry) * SS, (cx + rx) * SS, (cy + ry) * SS], fill=fill)

    def line(self, pts, width, fill=255):
        self.d.line(self.P(pts), fill=fill, width=int(width * SS), joint="curve")
        for x, y in (pts[0], pts[-1]):  # round ends
            self.ell(x, y, width / 2, width / 2, fill)

    def arc(self, cx, cy, r, a0, a1, width, fill=255):
        self.d.arc([(cx - r) * SS, (cy - r) * SS, (cx + r) * SS, (cy + r) * SS], a0, a1, fill=fill, width=int(width * SS))

    def small(self):
        return self.im.resize((W, H), Image.LANCZOS).point(lambda v: 255 if v >= 128 else 0)


def xor(a, b):
    from PIL import ImageChops
    return ImageChops.logical_xor(a.convert("1"), b.convert("1")).convert("L")


def stars_layer(seed, n, f, ymax=100, avoid=None):
    random.seed(seed)
    im = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(im)
    for i in range(n):
        x, y = random.randrange(W), random.randrange(ymax)
        if avoid and avoid(x, y):
            continue
        if (i * 3 + f) % 9:  # a few twinkle off each frame
            d.point((x, y), fill=255)
    return im


# The raven from the badge PCB artwork: tools/raven_pcb.png is its silhouette, cut out of
# hardware/utah_bsides_front.png (the dark bird; its light wing stroke and eye stay holes).
# The beak is light on the PCB, so it's drawn here, open (as on the PCB) or shut.
RAVEN_MASK = os.path.join(os.path.dirname(os.path.abspath(__file__)), "raven_pcb.png")
# measured off the PCB: the gape (corner of the mouth) at (26, 21); the upper half thick and
# hooked, the lower straight; open, they make a V toward the upper right with light inside
# deep at the head, tapering to the tip
UPPER_OPEN = [(23.5, 12.5), (29, 9), (35, 6.5), (41, 6.5), (43, 10), (39, 10.6), (33, 12.6), (28, 17), (26, 21)]
UPPER_SHUT = [(24, 14.8), (30, 14.3), (35.5, 15), (40, 16.6), (42.5, 18.3), (39.5, 18.5), (35, 19), (30, 19.6), (26, 20.2)]
LOWER_OPEN = [(26, 21), (33, 20.8), (40, 21), (45, 23), (41, 25.6), (33, 26), (25, 24.5)]
LOWER_SHUT = [(26, 20.2), (30, 19.6), (35, 19), (42, 18.5), (38.5, 19.6), (33, 20.8), (25.5, 22.3)]

def raven_shape(b, pose, ox=18, oy=2, sc=1.0):
    """The PCB raven, placed at (ox, oy) in screen pixels. pose: beak (0 shut .. 1 wide open,
    as on the PCB), eye (True open). Draws the black bird; returns the light details (inside
    of the open beak, a shut eye) for the caller."""
    T = lambda pts: [(ox + x * sc, oy + y * sc) for x, y in pts]
    beak, eye = pose["beak"], pose["eye"]
    mask = Image.open(RAVEN_MASK).convert("L")
    big = mask.resize((int(mask.width * sc * SS), int(mask.height * sc * SS)), Image.BICUBIC)
    b.im.paste(255, (int(ox * SS), int(oy * SS)), big.point(lambda v: 255 if v >= 128 else 0))
    b.poly(T([(25.5, 2), (54, 2), (54, 23.5), (25.5, 23.5)]), fill=0)  # clear the PCB's own beak edges
    b.poly(T([(19, 14.8), (25, 14.8), (26, 22.3), (21, 23.5)]))          # the face, up to the beak
    mix = lambda a, c: [(x0 + (x1 - x0) * beak, y0 + (y1 - y0) * beak) for (x0, y0), (x1, y1) in zip(a, c)]
    up, lo = mix(UPPER_SHUT, UPPER_OPEN), mix(LOWER_SHUT, LOWER_OPEN)
    # the beak is part of the bird: solid. The PCB shows its shape with cut-out lines; here they
    # are dark lines on the white bird: the mouth line when shut, the open gap when cawing.
    b.poly(T(up))
    b.poly(T(lo))
    cut = max(0.7, 0.7 * sc)
    if beak > 0.35:
        b.poly(T([(26.6, 20.4), up[7], up[6], up[5], lo[3], lo[2], lo[1]]), fill=0)
    else:  # shut: where the halves meet, the nostril, and the feathers at the beak's base
        b.line(T([(25.5, 20.3), (31, 19.6), (36.5, 19), (40.5, 18.6)]), max(1.0, 1.0 * sc), fill=0)
        b.ell(*T([(29.5, 16.6)])[0], max(0.8, 0.8 * sc), max(0.65, 0.65 * sc), fill=0)
        b.line(T([(25.2, 15.2), (24.6, 17.3), (24.9, 19.6)]), max(0.8, 0.8 * sc), fill=0)
    b.line(T([(60, 66), (63, 90)]), 1.6 * sc)            # the second leg, behind
    b.line(T([(58, 91), (68, 91)]), 1.3 * sc)
    lights = []
    b.ell(*T([(17, 20)])[0], 3 * sc, 3 * sc)  # fill the PCB's eye outline...
    if eye:  # ...and put one clear eye in it (shut for a blink)
        r = max(1.6, 1.9 * sc) if sc < 1 else 1.6
        b.ell(*T([(17, 20)])[0], r, r, fill=0)
    return lights


def apply_cuts(b, cut):
    for c in cut:
        if c[0] == "poly":
            b.poly(c[1], fill=0)
        elif c[0] == "ell":
            (x, y), r = c[1], c[2]
            b.ell(x, y, r, r, fill=0)
        else:
            b.line(c[1], c[2], fill=0)


def sign_shape(b, text_size=13, ox=0, oy=0, sc=1.0):
    """The weathered "UTAH BSIDES" plank the raven stands on (white, black lettering)."""
    T = lambda pts: [(ox + x * sc, oy + y * sc) for x, y in pts]
    b.poly(T([(6, 96), (152, 92), (154, 118), (4, 124)]))
    for k, y in enumerate((101, 116)):  # wood grain
        b.line(T([(12 + k * 20, y), (60 + k * 30, y - 1)]), 0.6 * sc, fill=0)
    for x, y in ((12, 100), (146, 96), (12, 119), (146, 114)):
        b.ell(*T([(x, y)])[0], 1.3 * sc, 1.3 * sc, fill=0)
    f = font(SERIF, int(text_size * SS * sc))
    w = b.d.textlength("UTAH BSIDES", font=f)
    b.d.text(((ox + 79 * sc) * SS - w / 2, (oy + 100 * sc) * SS), "UTAH BSIDES", font=f, fill=0)


def raven():
    seq = [  # (pose, ms): caws like on the PCB, blinks
        (dict(beak=0, eye=True), 700),
        (dict(beak=0, eye=False), 120),
        (dict(beak=0, eye=True), 500),
        (dict(beak=0.5, eye=True), 90),
        (dict(beak=1, eye=True), 420),     # CAW
        (dict(beak=0.4, eye=True), 90),
        (dict(beak=0, eye=True), 240),
        (dict(beak=1, eye=True), 420),     # CAW
        (dict(beak=0.4, eye=True), 90),
        (dict(beak=0, eye=True), 900),
        (dict(beak=0, eye=False), 120),
        (dict(beak=0, eye=True), 600),
    ]
    frames, durs = [], []
    for i, (pose, ms) in enumerate(seq):
        b = Big()
        b.ell(136, 22, 13, 13)                       # crescent moon
        b.ell(141, 18, 12, 12, fill=0)
        for c in raven_shape(b, pose, ox=10, oy=0):  # white raven; the open beak dark inside
            b.poly(c[1], fill=0)
        sign_shape(b, ox=0, oy=0)
        im = b.small()
        im = xor(im, stars_layer(3, 30, i, 86, lambda x, y: (12 < x < 132 and y > 10) or (x > 118 and y < 40)))
        d = ImageDraw.Draw(im)
        if pose["beak"] > 0.8:  # sound lines from the open beak
            for r in (6, 11):
                d.arc([58 - r, 6 - r, 58 + r, 6 + r], 300, 30, fill=255, width=2)
        frames.append(im)
        durs.append(ms)
    frames[0].save(f"{OUT}/99_utahtastic.gif", save_all=True, append_images=frames[1:], duration=durs, loop=0)


def boot_logo(userprefs):
    """The still raven (cawing, white) for Meshtastic's boot screen: USERPREFS_OEM_IMAGE_* in
    userPrefs.jsonc, 100x87 XBM."""
    bw, bh = 100, 87
    big = Image.new("L", (bw * SS, bh * SS), 0)
    b = Big()
    b.im, b.d = big, ImageDraw.Draw(big)
    sc = 0.74
    for c in raven_shape(b, dict(beak=1, eye=True), ox=0, oy=-3, sc=sc):
        b.poly(c[1], fill=0)
    sign_shape(b, text_size=13, ox=-6, oy=-1, sc=0.7)
    im = big.resize((bw, bh), Image.LANCZOS).point(lambda v: 255 if v >= 128 else 0)
    stride = (bw + 7) // 8
    data = bytearray(stride * bh)
    for y in range(bh):
        for x in range(bw):
            if im.getpixel((x, y)):
                data[y * stride + x // 8] |= 1 << (x % 8)
    im.save(f"{OUT}/boot_raven_preview.png")
    if not userprefs:
        return
    import re
    with open(userprefs) as fh:
        txt = fh.read()
    arr = "{ " + ", ".join("0x%02X" % v for v in data) + " }"
    txt = re.sub(r'("USERPREFS_OEM_IMAGE_WIDTH":\s*")[^"]*"', r'\g<1>%d"' % bw, txt, count=1)
    txt = re.sub(r'("USERPREFS_OEM_IMAGE_HEIGHT":\s*")[^"]*"', r'\g<1>%d"' % bh, txt, count=1)
    txt = re.sub(r'("USERPREFS_OEM_IMAGE_DATA":\s*")[^"]*"', lambda m: m.group(1) + arr + '"', txt, count=1)
    with open(userprefs, "w") as fh:
        fh.write(txt)


def text_white(d, y, text, f):
    w = d.textlength(text, font=f)
    d.text(((W - w) / 2, y), text, font=f, fill=255)


def campfire():
    """A fire in the dark; something at the edge of the light watches, blinks, and moves."""
    random.seed(11)
    sparks = [(random.uniform(-12, 12), random.uniform(0, 1), random.uniform(0.6, 1.4)) for _ in range(12)]
    frames, durs = [], []
    for f in range(18):
        b = Big()
        b.line([(50, 112), (110, 98)], 5)                     # logs
        b.line([(50, 98), (110, 112)], 5)
        b.line([(52, 110), (108, 100)], 1, fill=0)            # bark lines
        b.line([(52, 100), (108, 110)], 1, fill=0)
        for k in range(10):                                   # ring of stones
            a = k / 10 * math.tau
            b.ell(80 + 38 * math.cos(a), 108 + 9 * math.sin(a), 4, 3)
        for dx, h, ph in ((-13, 34, 0.0), (-4, 52, 1.7), (6, 44, 3.1), (14, 30, 4.4)):  # flames
            sway = 4 * math.sin(f * 0.9 + ph)
            hh = h * (0.82 + 0.18 * math.sin(f * 1.3 + ph * 2))
            b.poly([(80 + dx - 9, 104), (80 + dx - 7 + sway * 0.3, 104 - hh * 0.45), (80 + dx + sway, 104 - hh),
                    (80 + dx + 5 + sway * 0.5, 104 - hh * 0.5), (80 + dx + 9, 104)])
        b.poly([(72, 104), (78 + math.sin(f) * 2, 82), (84, 88), (88, 104)], fill=0)   # dark heart of the fire
        im = b.small()
        d = ImageDraw.Draw(im)
        for sx, sp, speed in sparks:                           # sparks drifting up
            t = (f * 0.07 * speed + sp) % 1
            d.point((80 + sx + math.sin(t * 9) * 3, 62 - t * 44), fill=255)
        eyes = None                                            # in the dark beyond the light...
        if 5 <= f < 9 or 10 <= f < 13:
            eyes = (22, 62, 3)
        elif f >= 13:
            eyes = (140, 70, 3.5)                              # ...and now on the other side
        if eyes:
            ex, ey, r = eyes
            for x in (ex - r * 2.2, ex + r * 2.2):
                d.polygon([(x - r * 1.3, ey + 1), (x + r * 1.3, ey - r * 0.7), (x + r * 0.9, ey + r * 0.8)], fill=255)
        text_white(d, 4, "KEEP THE FIRE LIT", font(SERIFC, 11))
        frames.append(im)
        durs.append(600 if f in (8, 12, 17) else 140)
    frames[0].save(f"{OUT}/09_campfire.gif", save_all=True, append_images=frames[1:], duration=durs, loop=0)


def coyote_profile(b, x, y, s, head_up):
    """A coyote sitting on its haunches, side view facing left; head_up 0..1 tips the muzzle up to howl."""
    T = lambda pts: [(x + px * s, y + py * s) for px, py in pts]
    b.poly(T([(-6, 0), (-8, -10), (-7, -18), (-4, -25), (2, -27), (8, -21), (13, -10), (16, -3), (16, 0)]))  # body
    b.poly(T([(12, -3), (22, -1), (28, 2), (24, 4), (14, 2)]))                                               # tail
    b.poly(T([(-8, 0), (-7, -9), (-4, -9), (-4, 0)]))                                                        # front legs
    head = [(-5, -24), (-12, -25), (-16, -27.5), (-15, -29), (-11, -30), (-6, -31.5), (-5, -37), (-2, -32),
            (0, -38), (1, -30), (2, -27)]
    px, py, a = -1, -26, 0.95 * head_up  # pivot at the neck; positive angle lifts the muzzle
    ca, sa = math.cos(a), math.sin(a)
    rot = [(px + (hx - px) * ca - (hy - py) * sa, py + (hx - px) * sa + (hy - py) * ca) for hx, hy in head]
    b.poly(T(rot))


def silhouette(sky, body):
    """A black figure on a picture: black where it is, with a 1 px light edge where it stands
    against the dark, so its shape always reads."""
    rim = body.filter(ImageFilter.MaxFilter(3))
    return ImageChops.subtract(ImageChops.lighter(sky, rim), body)


def jaws(b, lights, T, px, py, a, gape, s):
    """A long skull-like snout pointing left from (px, py), rotated by a, jaw dropped by gape
    0..1, with fangs. Black shapes go on b, light ones (teeth, eye) into lights."""
    ca, sa = math.cos(a), math.sin(a)
    R = lambda pts: T([(px + x * ca - y * sa, py + x * sa + y * ca) for x, y in pts])
    b.poly(R([(4, -5), (-6, -7), (-20, -4), (-23, -1), (-20, 0), (-6, 0), (5, 3)]))           # skull and upper jaw
    g = 2 + 7 * gape
    b.poly(R([(2, 2), (-6, 2 + g * 0.4), (-18, 2 + g), (-19, 4 + g), (-6, 5 + g * 0.5), (4, 6)]))  # lower jaw
    b.poly(R([(-2, -6), (0, -15), (3, -6)]))                                                 # ears, tall and ragged
    b.poly(R([(2, -6), (6, -14), (6, -4)]))
    for k in range(4):                                                                       # fangs
        x = -18 + k * 4
        lights.append(R([(x, 0), (x + 1.2, 0), (x + 0.6, 2 + (k % 2) * 1.5)]))
        lights.append(R([(x + 2, 2 + g), (x + 3.2, 2 + g), (x + 2.6, g - 0.5)]))
    return R([(-7, -4)])[0]  # the eye


def skinwalker_figure(b, x, y, s, rise=1.0, arms=0.0, gape=0.5):
    """A gaunt thing on two backward-bending legs with a canine skull, standing at (x, y) (its
    feet), facing left. rise 0..1 straightens it from a crouch; arms 0..1 lifts its claws.
    Returns light details (eyes, fangs, ribs) to draw over the black."""
    T = lambda pts: [(x + px * s, y + py * s) for px, py in pts]
    lights = []
    h = 0.6 + 0.4 * rise
    for dx in (-3, 3):  # digitigrade legs: thigh forward, shin back, long clawed feet
        b.line(T([(dx, -26 * h), (dx - 6, -14 * h), (dx + 2, -4), (dx - 5, 0)]), 2.0 * s)
    b.poly(T([(-6, -24 * h), (-9, -36 * h), (-7, -48 * h), (2, -55 * h), (11, -46 * h), (8, -30 * h), (5, -24 * h)]))
    for k in range(6):  # mangy spikes over the shoulders and spine
        bx = -6 + k * 3
        b.poly(T([(bx, -48 * h + k * 2), (bx + 1 + k * 0.6, -58 * h + k * 2.4), (bx + 3, -47 * h + k * 2)]))
    for k in range(4):  # ribs
        lights.append(("line", T([(-6, (-42 + k * 4) * h), (2, (-40 + k * 4) * h), (6, (-37 + k * 4) * h)]), 0.7 * s))
    for side in (-1, 1):  # arms past the knees, long claws
        sx = -6 if side < 0 else 6
        ey = -6 * h - arms * 32
        b.line(T([(sx, -46 * h), (sx + side * 7, -30 * h - arms * 16), (sx + side * 3 - 4, ey)]), 1.6 * s)
        for c in range(4):
            b.line(T([(sx + side * 3 - 4, ey), (sx + side * 3 - 8 + c * 2.6, ey + 8)]), 0.7 * s)
    eye = jaws(b, [], T, 0, -54 * h, -0.15 - 0.2 * arms, gape, s)
    fangs = []
    jaws(Big(), fangs, T, 0, -54 * h, -0.15 - 0.2 * arms, gape, s)
    lights += [("poly", f) for f in fangs]
    lights.append(("ell", eye, 1.4 * s))
    return lights


def beast(b, x, y, s, head_up=0.0, gape=0.3):
    """A starved, wrong-looking coyote on all fours, facing left: arched spiky spine, ribs,
    a rat tail, glowing eyes and bared fangs. head_up 0..1 throws its head back to howl."""
    T = lambda pts: [(x + px * s, y + py * s) for px, py in pts]
    lights = []
    b.poly(T([(-14, -24), (-12, -18), (-4, -15), (10, -16), (20, -16), (25, -22), (18, -30), (6, -33),
              (-6, -29)]))
    for k in range(9):  # spikes of mangy fur along the spine
        bx = -6 + k * 3.4
        top = -29 - 4 * math.sin(k / 8 * math.pi)
        b.poly(T([(bx, top), (bx + 1.2, top - 5 - (k % 2) * 2), (bx + 3, top + 0.5)]))
    for x0, x1, x2 in ((-12, -13, -15), (-6, -5, -4)):            # front legs, bony
        b.line(T([(x0, -18), (x1, -8), (x2, 0)]), 1.4 * s)
    for x0 in (14, 21):                                             # hind legs, bent backwards
        b.line(T([(x0, -16), (x0 + 5, -8), (x0 - 1, 0)]), 1.6 * s)
    b.line(T([(24, -21), (32, -12), (35, -3), (38, 0)]), 0.9 * s)   # rat tail
    for k in range(4):                                              # ribs
        lights.append(("line", T([(-2 + k * 4, -27), (0 + k * 4, -21), (-1 + k * 4, -17)]), 0.6 * s))
    fangs = []
    a = -0.1 - 1.0 * head_up  # head low, or thrown back
    eye = jaws(b, fangs, T, -12, -25, a, gape, s)
    lights += [("poly", f) for f in fangs]
    lights.append(("ell", eye, 1.2 * s))
    return lights


def draw_lights(im, lights):
    L = Big()
    for kind, *rest in lights:
        if kind == "poly":
            L.poly(rest[0])
        elif kind == "ell":
            (lx, ly), r = rest
            L.ell(lx, ly, r, r)
        else:
            L.line(rest[0], rest[1])
    return ImageChops.lighter(im, L.small())


def face_closeup():
    """The thing, turned to look at you: a coyote skull with something alive in the sockets."""
    b = Big()
    b.poly([(54, 30), (66, 10), (72, 30), (88, 30), (94, 10), (106, 30), (112, 50), (102, 70), (94, 104),
            (88, 118), (72, 118), (66, 104), (58, 70), (48, 50)])                         # ears and skull
    for side in (-1, 1):
        b.poly([(80 + side * 4, 46), (80 + side * 24, 38), (80 + side * 20, 58), (80 + side * 8, 60)], fill=0)  # sockets
    b.poly([(76, 86), (84, 86), (82, 96), (78, 96)], fill=0)                                   # nose
    b.poly([(70, 104), (90, 104), (88, 116), (72, 116)], fill=0)                               # mouth
    im = b.small()
    d = ImageDraw.Draw(im)
    for k in range(5):                                                                          # teeth
        tx = 72 + k * 4
        d.polygon([(tx, 104), (tx + 3, 104), (tx + 1.5, 109 + (k % 2) * 3)], fill=255)
        d.polygon([(tx, 116), (tx + 3, 116), (tx + 1.5, 112 - (k % 2) * 2)], fill=255)
    for ex in (68, 92):                                                                         # eyes
        d.ellipse([ex - 2, 49, ex + 2, 53], fill=255)
    for k in range(8):                                                                          # cracks
        d.line([(60 + k * 6, 30 + (k % 3) * 2), (62 + k * 6, 26 + (k % 2) * 3)], fill=0)
    return im


# A coyote seen side-on, facing left, feet at (0, 0): a plain, realistic silhouette.
COYOTE = [(-36, -31), (-33, -28), (-27, -27), (-22, -24), (-19, -20), (-18, -14), (-17, -8), (-16, -2), (-18, 0),
          (-13, 0), (-13, -6), (-12, -12), (-8, -13), (2, -12), (10, -13), (14, -12), (13, -6), (11, -1), (9, 0),
          (14, 0), (16, -4), (19, -9), (21, -14), (23, -18), (30, -15), (36, -8), (37, -3), (33, -4), (29, -9),
          (24, -14), (22, -21), (12, -24), (-2, -26), (-14, -28), (-18, -31), (-19, -37), (-22, -31), (-24, -38),
          (-26, -31), (-30, -32)]
COYOTE_FAR_LEGS = [[(-11, -12), (-9, -6), (-9, 0), (-6, 0), (-7, -7), (-7, -12)],
                   [(17, -12), (20, -6), (17, 0), (20, 0), (23, -6), (21, -13)]]
COYOTE_HEAD = list(range(0, 5)) + list(range(34, 40))  # the snout, jaw and ears, for tilting


def rotate(pts, cx, cy, a):
    ca, sa = math.cos(a), math.sin(a)
    return [(cx + (x - cx) * ca - (y - cy) * sa, cy + (x - cx) * sa + (y - cy) * ca) for x, y in pts]


def coyote_real(b, x, y, s, rear=0.0):
    """The coyote, rearing up by `rear` radians about its hind feet. Returns its eye."""
    pts = rotate(COYOTE, 12, 0, rear)
    T = lambda q: [(x + px * s, y + py * s) for px, py in q]
    b.poly(T(pts))
    for leg in COYOTE_FAR_LEGS:
        b.poly(T(rotate(leg, 12, 0, rear)))
    return T(rotate([(-27, -31)], 12, 0, rear))[0]


def walker(b, x, y, s, tilt=0.0, arms=0.0):
    """Something tall and starved standing like a man, with the coyote's head. Plain silhouette,
    a ragged edge; tilt turns the head over too far. Returns its two eyes."""
    T = lambda q: [(x + px * s, y + py * s) for px, py in q]
    random.seed(19)
    j = lambda: random.uniform(-0.6, 0.6)
    for dx in (-2.5, 2.5):  # legs that bend the wrong way
        b.line(T([(dx, -34), (dx - 5, -20), (dx + 1.5, -6), (dx - 3, 0)]), 2.2 * s)
    torso = [(-5, -33), (-6, -44), (-7, -54), (-9, -60), (-3, -63), (6, -62), (8, -55), (6, -44), (4, -33)]
    b.poly(T([(px + j(), py + j()) for px, py in torso]))
    for side in (-1, 1):  # arms far too long, hanging past the knees
        sx = (-7 if side < 0 else 6)
        hand_y = -12 - arms * 26
        b.line(T([(sx, -58), (sx + side * 4, -40 - arms * 10), (sx + side * 2.5, hand_y)]), 1.5 * s)
        for c in (-1.5, 0, 1.5):
            b.line(T([(sx + side * 2.5, hand_y), (sx + side * 2.5 + c, hand_y + 6)]), 0.7 * s)
    # the coyote's head on a short neck, tipped over at an angle no neck should allow
    head = [(-1, -61), (-4, -62), (-9, -63), (-15, -64), (-18, -65.5), (-17, -67), (-11, -68), (-6, -69),
            (-5, -75), (-2, -69.5), (0, -76), (2.5, -69), (4, -64)]
    b.poly(T(rotate(head, 0, -63, tilt)))
    return T(rotate([(-7, -67), (-9, -66.5)], 0, -63, tilt))


def howl():
    """A coyote on the ridge in front of the moon. It stands still and looks at you. It rears up
    and unfolds into something that stands like a man, and its head tips over too far. Then it's
    much closer. Then it's gone, and something blinks in the dark right in front of you."""
    plan = (["still"] * 4 + ["rear1", "rear2", "rear3", "stand", "stand", "tilt1", "tilt2", "tilt2",
             "close", "close", "gone", "eyes", "blink", "eyes", "dark"])
    times = {"still": 450, "rear1": 220, "rear2": 220, "rear3": 220, "stand": 500, "tilt1": 400, "tilt2": 700,
             "close": 650, "gone": 900, "eyes": 600, "blink": 120, "dark": 1400}
    frames, durs = [], []
    for f, kind in enumerate(plan):
        sky = Big()
        if kind not in ("eyes", "blink", "dark"):
            sky.ell(66, 44, 36, 36)
            sky.poly([(0, 92), (34, 92), (40, 84), (110, 84), (116, 94), (W, 94), (W, H), (0, H)])
        c = Big()
        eyes = []
        if kind == "still":
            eyes = [coyote_real(c, 64, 84, 1.25)]
        elif kind.startswith("rear"):
            eyes = [coyote_real(c, 64, 84, 1.25, rear=0.38 * int(kind[-1]))]
        elif kind in ("stand", "tilt1", "tilt2"):
            tilt = {"stand": 0.0, "tilt1": 0.5, "tilt2": 1.1}[kind]
            eyes = walker(c, 64, 84, 0.95, tilt)
        elif kind == "close":  # right in front of you
            eyes = walker(c, 74, 234, 2.8, 0.55, arms=0.2)
        im = silhouette(sky.small(), c.small()) if kind != "close" else ImageChops.subtract(sky.small(), c.small())
        d = ImageDraw.Draw(im)
        if kind not in ("eyes", "blink", "dark", "close"):
            for i2, (sx, sy) in enumerate([(8, 10), (112, 14), (138, 6), (150, 30), (12, 50), (150, 64), (126, 40), (22, 26)]):
                d.point((sx, sy), fill=255 if (i2 + f) % 6 else 0)
        for ex, ey in eyes:  # eyeshine: the only light on it
            r = 1 if kind == "close" else 0
            d.rectangle([round(ex) - r, round(ey) - r, round(ex), round(ey)], fill=255)
        if kind in ("eyes", "blink"):  # close, low, in the dark
            for x in (66, 86):
                if kind == "eyes":
                    d.polygon([(x - 5, 100), (x + 5, 97), (x + 3, 102)], fill=255)
                else:
                    d.line([(x - 5, 100), (x + 5, 99)], fill=255)
        frames.append(im)
        durs.append(times[kind])
    frames[0].save(f"{OUT}/10_howl.gif", save_all=True, append_images=frames[1:], duration=durs, loop=0)


def lightning():
    """Dark and raining. Each flash shows the figure closer. Then only its eyes."""
    random.seed(5)
    rain = [(random.randrange(W), random.randrange(H)) for _ in range(40)]
    plan = [("dark", 0), ("flash", 0.45), ("dark", 0), ("dark", 0), ("flash", 0.8), ("dark", 0), ("dark", 0),
            ("flash", 1.5), ("dark", 0), ("eyes", 0), ("eyes", 0), ("dark", 0)]
    frames, durs = [], []
    for f, (kind, near) in enumerate(plan):
        if kind == "flash":
            sky = Big()
            sky.poly([(0, 0), (W, 0), (W, 92), (0, 92)])                                 # white sky
            bolt = [(118, 0), (110, 22), (120, 26), (104, 54), (112, 56), (98, 84)]
            sky.line(bolt, 1.6, fill=0)
            c = Big()
            walker(c, 70, 92 + (near - 0.45) * 30, near * 0.85, 0.35 * near)
            im = ImageChops.subtract(sky.small(), c.small())  # a black shape against the flash
        else:
            im = Image.new("L", (W, H), 0)
        d = ImageDraw.Draw(im)
        d.line([(0, 92), (W, 92)], fill=255 if kind != "flash" else 0)                 # horizon
        for k, (rx, ry) in enumerate(rain):                                              # rain
            y = (ry + f * 17) % H
            d.line([(rx, y), (rx - 2, y + 5)], fill=0 if kind == "flash" and y < 92 else 255)
        if kind == "eyes":
            for x in (74, 86):
                d.polygon([(x - 4, 60), (x + 4, 57), (x + 3, 62)], fill=255)
        frames.append(im)
        durs.append({"flash": 110, "eyes": 1100}.get(kind, 700 if f in (2, 5) else 400))
    frames[0].save(f"{OUT}/13_lightning.gif", save_all=True, append_images=frames[1:], duration=durs, loop=0)


def skull():
    """A cow skull on a fence post; its empty eyes light up and look around. A vulture circles."""
    frames, durs = [], []
    look = [None, None, 0, 0, -2, -2, 0, 2, 2, 0, None, None]
    for f, lk in enumerate(look):
        b = Big()
        b.line([(80, 74), (80, 122)], 7)                     # fence post
        b.line([(77, 80), (77, 120)], 0.8, fill=0)
        b.line([(0, 96), (W, 92)], 0.9)                       # barbed wire
        for x in range(8, W, 18):
            y = 96 - x * 4 / W
            b.line([(x - 2, y - 2), (x + 2, y + 2)], 0.8)
            b.line([(x - 2, y + 2), (x + 2, y - 2)], 0.8)
        # the skull: wide brow, long narrow face, horns sweeping out and up
        b.poly([(60, 38), (100, 38), (102, 48), (92, 58), (88, 82), (84, 90), (76, 90), (72, 82), (68, 58), (58, 48)])
        for side in (-1, 1):
            horn = [(80 + side * (18 + 26 * math.sin(t * 1.4)), 42 - 30 * t + 6 * math.sin(t * 3.0)) for t in [k / 8 for k in range(9)]]
            for k in range(8):  # curving, and thinner toward the tip
                b.line([horn[k], horn[k + 1]], 3.6 - k * 0.32)
            b.poly([(80 + side * 3, 50), (80 + side * 15, 44), (80 + side * 12, 56), (80 + side * 5, 57)], fill=0)  # slanted sockets
        b.poly([(76, 64), (84, 64), (80, 76)], fill=0)                 # nose cavity
        b.poly([(77, 82), (83, 82), (82, 88), (78, 88)], fill=0)       # nostrils
        b.line([(68, 44), (72, 50)], 0.6, fill=0)                      # a crack across the brow
        if lk is not None:                                            # something looks out of it
            for side in (-1, 1):
                b.ell(80 + side * 9 + lk, 51, 1.7, 1.7)
        # a vulture circling high up
        a = f / len(look) * math.tau
        vx, vy = 80 + 50 * math.cos(a), 16 + 8 * math.sin(a)
        b.line([(vx - 9, vy - 3), (vx - 4, vy), (vx, vy - 1), (vx + 4, vy), (vx + 9, vy - 3)], 1.1)
        frames.append(b.small())
        durs.append(700 if lk is None else 260)
    frames[0].save(f"{OUT}/14_skull.gif", save_all=True, append_images=frames[1:], duration=durs, loop=0)


def mothman():
    """A winged figure on the bridge in front of the moon: red eyes flare, wings open, it lifts off."""
    frames, durs = [], []
    for f in range(16):
        sky = Big()
        sky.ell(80, 42, 34, 34)                               # moon
        # truss bridge across the bottom
        sky.line([(0, 98), (W, 98)], 2.4)
        sky.line([(0, 78), (W, 78)], 1.6)
        for x in range(0, W + 1, 20):
            sky.line([(x, 78), (x, 98)], 1.2)
            sky.line([(x, 78), (x + 20, 98)], 0.9)
        sky.line([(0, 104), (W, 104)], 0.6)
        c = Big()
        lift = 0 if f < 10 else (f - 9) * 6                  # it takes off
        x, y = 80, 78 - lift
        spread = 0 if f < 5 else min(1, (f - 4) / 3)
        flap = math.sin(f * 1.6) * 0.25 if f >= 10 else 0
        # wings: folded around it, then spread wide: big rounded forewings, smaller hindwings
        for side in (-1, 1):
            w = 8 + 48 * spread
            lift_w = flap * 18
            fore = [(x + side * 4, y - 40)]
            for k in range(7):  # rounded leading edge out to the tip
                a = k / 6
                fore.append((x + side * (6 + w * a), y - 40 - (14 + 10 * spread) * math.sin(a * math.pi * 0.85) - lift_w * a))
            fore += [(x + side * (w + 2), y - 34 - lift_w), (x + side * w * 0.7, y - 26), (x + side * 6, y - 28)]
            c.poly(fore)
            c.poly([(x + side * 5, y - 28), (x + side * (6 + w * 0.6), y - 24 + lift_w * 0.3),
                    (x + side * (4 + w * 0.5), y - 10), (x + side * (2 + w * 0.25), y - 14), (x + side * 4, y - 12)])
            if spread > 0.5:  # eye-spots on the forewings, like a moth
                c.ell(x + side * (6 + w * 0.6), y - 40 - 10 * spread - lift_w * 0.6, 3, 2.5, fill=0)
        c.poly([(x - 6, y - 38), (x + 6, y - 38), (x + 5, y - 12), (x + 3, y), (x - 3, y), (x - 5, y - 12)])  # body
        c.ell(x, y - 44, 6, 7)                                # head, no neck
        im = silhouette(sky.small(), c.small())
        d = ImageDraw.Draw(im)
        glow = 2.4 if f in (3, 4, 9) else 1.6                 # the eyes flare
        for ex in (x - 3, x + 3):
            d.ellipse([ex - glow, y - 46 - glow, ex + glow, y - 46 + glow], fill=255)
            if glow > 2:
                for a in range(0, 360, 60):
                    r = math.radians(a)
                    d.line([(ex + math.cos(r) * 4, y - 46 + math.sin(r) * 4), (ex + math.cos(r) * 7, y - 46 + math.sin(r) * 7)], fill=255)
        frames.append(im)
        durs.append({4: 700, 8: 500, 15: 900}.get(f, 150))
    frames[0].save(f"{OUT}/15_mothman.gif", save_all=True, append_images=frames[1:], duration=durs, loop=0)


def tracks():
    """Footprints walk up the trail; halfway the boots become paws."""
    steps = []
    for k in range(9):
        steps.append((80 + (9 if k % 2 else -9), 118 - k * 10, k >= 5))
    frames, durs = [], []
    f1, f2 = font(SERIFC, 12), font(SANS, 8)
    for f in range(14):
        b = Big()
        shown = min(f + 1, len(steps))
        for x, y, paw in steps[:shown]:
            if not paw:  # boot: sole and heel
                b.ell(x, y - 2, 3, 4)
                b.ell(x, y + 4, 2.4, 2)
            else:  # paw: pad and four toes with claws
                b.ell(x, y + 2, 3, 2.6)
                for tx, ty in ((-3.5, -2), (-1.2, -4), (1.2, -4), (3.5, -2)):
                    b.ell(x + tx, y + ty, 1.2, 1.4)
                    b.line([(x + tx, y + ty - 1.4), (x + tx * 1.1, y + ty - 3)], 0.6)
        im = b.small()
        d = ImageDraw.Draw(im)
        text_white(d, 2, "THE TRACKS", f1)
        if f >= 8:
            text_white(d, 15, "CHANGE...", f1)
        frames.append(im)
        durs.append(1600 if f == 13 else 360)
    frames[0].save(f"{OUT}/11_tracks.gif", save_all=True, append_images=frames[1:], duration=durs, loop=0)


def mine():
    """The mine entrance at night: a lantern sways, the light flickers, eyes open in the shaft."""
    frames, durs = [], []
    f2 = font(SANS, 8)
    for f in range(16):
        b = Big()
        b.poly([(0, 30), (40, 18), (120, 16), (W, 28), (W, H), (0, H)])     # hillside
        b.poly([(52, 44), (108, 44), (110, 112), (50, 112)], fill=0)        # the shaft mouth
        for x0 in (46, 108):                                                 # timber frame
            b.line([(x0 + 3, 44), (x0 + 3, 112)], 3, fill=0)
            b.line([(x0 + 3, 46), (x0 + 3, 110)], 1.6)
        b.line([(44, 42), (116, 42)], 4, fill=0)
        b.line([(46, 42), (114, 42)], 2.4)
        b.line([(30, 112), (130, 112)], 1.2, fill=0)                         # rails into the dark
        for k in range(5):
            y = 112 - k * 13
            w = 22 - k * 3
            b.line([(80 - w, y), (80 + w, y)], 0.9, fill=0)
        sway = 5 * math.sin(f * 0.7)                                          # the lantern on its hook
        lx, ly = 64 + sway, 64
        b.line([(64, 46), (lx, ly - 7)], 0.6)
        lit = f not in (6, 7, 12)                                            # it gutters...
        b.poly([(lx - 4, ly - 6), (lx + 4, ly - 6), (lx + 5, ly + 6), (lx - 5, ly + 6)], fill=255 if lit else 0)
        b.line([(lx - 5, ly - 6), (lx + 5, ly - 6)], 1)
        b.line([(lx - 5, ly + 6), (lx + 5, ly + 6)], 1)
        if lit:
            for r in (9, 13):
                b.arc(lx, ly, r, 0, 360, 0.5)
        if f in (6, 7, 12, 13, 14):                                          # ...and in the dark, eyes
            ex, ey = (88, 80) if f < 12 else (80, 70)
            for x in (ex - 6, ex + 6):
                b.poly([(x - 4.5, ey + 1), (x + 4.5, ey - 3), (x + 3, ey + 3)])
        im = b.small()
        d = ImageDraw.Draw(im)
        d.rectangle([118, 72, 154, 92], fill=0)                               # sign on a post
        d.rectangle([118, 72, 154, 92], outline=255)
        d.line([(136, 92), (136, 112)], fill=255)
        for i, ln in enumerate(("KEEP", "OUT")):
            w = d.textlength(ln, font=f2)
            d.text((136 - w / 2, 73 + i * 9), ln, font=f2, fill=255)
        frames.append(im)
        durs.append(700 if f in (7, 14) else 150)
    frames[0].save(f"{OUT}/12_mine.gif", save_all=True, append_images=frames[1:], duration=durs, loop=0)


def jackalope():
    """A jackalope on the desert floor under the moon: a jackrabbit wearing a buck's antlers. It
    sits with its ears and nose twitching, then hops away off the side of the screen."""
    #       x,   lift, ear,   nose
    seq = [(30, 0, 0.00, 1), (30, 0, -0.22, 0), (30, 0, 0.16, 1), (30, 0, 0.00, 1),  # sit and twitch
           (46, 9, 0.30, 0), (64, 13, 0.40, 0), (82, 6, 0.30, 0), (98, 0, 0.10, 1),  # first hop
           (116, 10, 0.35, 0), (136, 13, 0.40, 0), (156, 7, 0.30, 0), (176, 2, 0.20, 0),  # bounding off
           (200, 0, 0.00, 0)]                                                        # gone, so the loop is clean
    frames, durs = [], []
    for f in range(len(seq)):
        xi, lift, ear_a, nose_on = seq[f]
        c = Big()                                      # drawn white on black, like the raven
        c.ell(132, 26, 15, 15)                         # moon, upper right
        c.line([(0, 101), (W, 101)], 1.4)              # the desert horizon
        c.line([(24, 101), (24, 70)], 5)               # a saguaro cactus, left
        c.line([(24, 86), (15, 86), (15, 76)], 3)
        c.line([(24, 80), (34, 80), (34, 67)], 3)
        x, y = xi, 100 - lift
        c.ell(x - 10, y - 12, 13, 15)                  # haunch
        c.ell(x + 4, y - 17, 9, 12)                    # chest
        c.ell(x + 12, y - 28, 6, 6)                    # head
        c.poly([(x + 16, y - 30), (x + 25, y - 28), (x + 16, y - 25)])  # muzzle, pointing right
        c.ell(x - 16, y - 8, 4, 5)                     # tail
        c.line([(x + 8, y - 10), (x + 9, y - 1)], 3)   # front leg
        c.line([(x - 6, y - 2), (x + 7, y - 1)], 3)    # hind foot on the ground
        a = ear_a
        for k in range(2):                             # two long ears, swept back and up
            bx, by = x + 9 + k * 3, y - 33
            ang = 2.25 + a + k * 0.18
            c.line([(bx, by), (bx + 16 * math.cos(ang), by - 16 * math.sin(ang))], 2.4)
        for side in (0, 1):                            # the antlers, forked, over the brow
            bx, by = x + 10 + side * 3, y - 33
            m = (bx + 6, by - 12)
            c.line([(bx, by), m], 1.8)
            c.line([m, (bx + 12, by - 16)], 1.4)
            c.line([m, (bx + 2, by - 21)], 1.4)
            c.line([(bx + 9, by - 14), (bx + 14, by - 23)], 1.2)
        im = c.small()
        d = ImageDraw.Draw(im)
        for i2, (sx, sy) in enumerate([(10, 12), (40, 8), (70, 16), (100, 10), (112, 50), (150, 60), (16, 40), (90, 36)]):
            d.point((sx, sy), fill=255 if (i2 + f) % 5 else 0)
        d.point((round(x + 13), round(y - 29)), fill=0)            # eye (dark on the white head)
        if nose_on:
            d.point((round(x + 24), round(y - 27)), fill=0)        # nose (dark)
        frames.append(im)
        durs.append(130 if lift else (300 if f % 3 else 560))  # a touch slower while airborne
    frames[0].save(f"{OUT}/03_jackalope.gif", save_all=True, append_images=frames[1:], duration=durs, loop=0)


def wendigo():
    """A wendigo among the pines: a tall, starved shape with a cervine skull and branching antlers,
    standing in the mist. It leans and watches; its eyes catch the light; the fog drifts."""
    frames, durs = [], []
    N = 12
    for f in range(N):
        sky = Big()
        sky.ell(80, 44, 30, 30)                        # moon, behind the figure so it reads
        sky.poly([(0, 108), (W, 108), (W, H), (0, H)]) # ground
        trees = Big()                                  # a treeline of pines
        random.seed(5)
        for tx in range(6, W, 19):
            th = random.randint(26, 42)
            trees.poly([(tx, 108 - th), (tx - 7, 108), (tx + 7, 108)])
        scene = ImageChops.lighter(sky.small(), silhouette(sky.small(), trees.small()))
        c = Big()
        x, y = 80, 112
        sway = 0.12 * math.sin(f / N * 2 * math.pi)
        arms = 0.12 + 0.05 * math.sin(f / N * 2 * math.pi)
        eyes = walker(c, x, y, 1.0, tilt=sway, arms=arms)
        hx, hy = x - 3, y - 76                          # antlers from the skull
        for side in (-1, 1):
            m = (hx + side * 10, hy - 11)
            c.line([(hx + side * 2, hy), m], 1.8)
            c.line([m, (hx + side * 19, hy - 17)], 1.4)
            c.line([m, (hx + side * 8, hy - 21)], 1.4)
            c.line([(hx + side * 14, hy - 14), (hx + side * 21, hy - 25)], 1.2)
        im = silhouette(scene, c.small())
        d = ImageDraw.Draw(im)
        for row, yy in enumerate((100, 106, 112)):      # drifting fog
            off = f * 2 + row * 3
            for xx in range(0, W, 3):
                if ((xx + off) // 3) % 2 == 0:
                    d.point((xx, yy), fill=255)
        for i2, (sx, sy) in enumerate([(10, 10), (40, 6), (70, 14), (150, 22), (20, 30), (110, 8)]):
            d.point((sx, sy), fill=255 if (i2 + f) % 5 else 0)
        if f % 7 != 5:                                  # eyeshine, with a blink
            for ex, ey in eyes:
                d.rectangle([round(ex) - 1, round(ey) - 1, round(ex), round(ey)], fill=255)
        frames.append(im)
        durs.append(230)
    frames[0].save(f"{OUT}/16_wendigo.gif", save_all=True, append_images=frames[1:], duration=durs, loop=0)


def thunderbird():
    """A great pale eagle flies the length of the night sky, wings beating wide, and passes on
    off the far edge. Drawn white on black (like the raven) so the bird shape always reads."""
    frames, durs = [], []
    N = 18
    for f in range(N):
        c = Big()
        c.ell(136, 24, 12, 12)                           # a moon up in the corner
        x = -40 + f * (240 / (N - 1))                     # off the left, across, off the right
        y = 48 + 7 * math.sin(f / N * 2 * math.pi)
        wt = -20 * math.sin(f / N * 4 * math.pi)          # wing tips rise and fall, two beats across
        c.poly([(x - 16, y + 1), (x + 5, y - 5), (x + 20, y - 1), (x + 7, y + 7), (x - 12, y + 6)])  # body
        c.ell(x + 19, y - 3, 5, 4)                        # head
        c.poly([(x + 23, y - 4), (x + 34, y - 2), (x + 23, y + 1)])  # hooked beak, forward
        c.poly([(x - 16, y + 1), (x - 36, y - 5), (x - 31, y + 2), (x - 36, y + 9)])  # forked tail, back
        # two broad wings swept from the shoulders; the tips beat together
        c.poly([(x + 3, y - 4), (x + 22, y - 15 + wt), (x + 44, y - 11 + wt), (x + 50, y - 5 + wt), (x + 28, y + 4), (x + 7, y + 3)])
        c.poly([(x - 3, y - 4), (x - 20, y - 15 + wt), (x - 42, y - 11 + wt), (x - 48, y - 5 + wt), (x - 26, y + 4), (x - 5, y + 3)])
        for tip, s in ((50, 1), (-48, -1)):               # splayed primary feathers at each tip
            for k in range(3):
                c.line([(x + tip - s * k * 2, y - 6 + wt + k * 3), (x + tip + s * 6, y - 12 + wt + k * 4)], 1.3)
        im = c.small()
        d = ImageDraw.Draw(im)
        for i2, (sx, sy) in enumerate([(10, 10), (40, 18), (90, 12), (120, 20), (150, 44), (24, 80), (140, 96), (70, 104)]):
            d.point((sx, sy), fill=255 if (i2 + f) % 6 else 0)
        frames.append(im)
        durs.append(180)
    frames[0].save(f"{OUT}/17_thunderbird.gif", save_all=True, append_images=frames[1:], duration=durs, loop=0)


def chupacabra():
    """The chupacabra creeps along the fenceline: a hairless, spiny-backed hound with a ridge of
    quills down its spine and eyes and fangs that catch the moonlight."""
    frames, durs = [], []
    N = 15
    for f in range(N):
        sky = Big()
        sky.ell(80, 74, 34, 34)                         # big moon behind, so the creature reads
        sky.poly([(0, 104), (W, 104), (W, H), (0, H)])  # ground
        fen = Big()                                     # a ranch fence behind
        for fx in range(10, W, 22):
            fen.line([(fx, 104), (fx, 90)], 2)
        fen.line([(6, 95), (W - 6, 95)], 1.5)
        scene = ImageChops.lighter(sky.small(), silhouette(sky.small(), fen.small()))
        c = Big()
        x = -34 + f * (230 / (N - 1))                   # out of the dark, across the moon, into the dark
        lights = beast(c, x, 104, 1.2, head_up=0.0, gape=0.3 if f % 3 == 0 else 0.1)
        im = silhouette(scene, c.small())
        im = draw_lights(im, lights)
        d = ImageDraw.Draw(im)
        for i2, (sx, sy) in enumerate([(10, 10), (44, 16), (80, 8), (150, 54), (20, 30), (100, 20)]):
            d.point((sx, sy), fill=255 if (i2 + f) % 5 else 0)
        frames.append(im)
        durs.append(240)
    frames[0].save(f"{OUT}/18_chupacabra.gif", save_all=True, append_images=frames[1:], duration=durs, loop=0)


def dogman():
    """A dogman in the full moon: a wolf that stands like a man, throwing its head back to howl."""
    frames, durs = [], []
    N = 12
    for f in range(N):
        sky = Big()
        sky.ell(80, 54, 44, 44)                         # a full moon behind it
        c = Big()
        x, y = 80, 114
        arms = 0.12 + 0.14 * math.sin(f / N * 2 * math.pi)
        eyes = walker(c, x, y, 1.1, tilt=0.0, arms=arms)
        c.line([(x + 6, y - 30), (x + 15, y - 22), (x + 22, y - 26), (x + 19, y - 15)], 2.6)  # bushy tail
        hx, hy = x - 7, y - 72                          # taller wolf ears over the head
        c.poly([(hx - 1, hy), (hx - 5, hy - 11), (hx + 2, hy - 3)])
        c.poly([(hx + 4, hy), (hx + 8, hy - 11), (hx + 10, hy - 2)])
        im = silhouette(sky.small(), c.small())
        d = ImageDraw.Draw(im)
        for i2, (sx, sy) in enumerate([(12, 12), (40, 8), (140, 16), (150, 48), (22, 34), (120, 10)]):
            d.point((sx, sy), fill=255 if (i2 + f) % 5 else 0)
        for ex, ey in eyes:
            d.rectangle([round(ex) - 1, round(ey) - 1, round(ex), round(ey)], fill=255)
        frames.append(im)
        durs.append(190)
    frames[0].save(f"{OUT}/19_dogman.gif", save_all=True, append_images=frames[1:], duration=durs, loop=0)


def phantom_rider():
    """A phantom rider crosses the flat at a gallop: a horse and a hunched rider in a hat, trailing dust."""
    frames, durs = [], []
    N = 14
    for f in range(N):
        c = Big()                                        # drawn white on black (a pale, moonlit rider)
        c.ell(20, 24, 12, 12)                            # a low moon, far left
        c.line([(0, 100), (W, 100)], 1.4)                # the flat horizon
        x, y = -34 + f * (236 / (N - 1)), 98             # gallops in from the left and off the right
        bob = 1.6 * math.sin(f * 1.3)
        T = lambda pts: [(x + px, y - bob + py) for px, py in pts]
        c.poly(T([(-20, -20), (-8, -25), (10, -24), (18, -20), (14, -15), (-4, -15), (-16, -16)]))  # body
        c.poly(T([(13, -21), (23, -31), (30, -31), (28, -24), (19, -18)]))  # neck and head
        c.poly(T([(28, -31), (35, -32), (33, -28), (27, -27)]))             # muzzle
        c.poly(T([(21, -31), (23, -37), (25, -31)]))                        # ear
        c.line(T([(-20, -20), (-31, -16), (-35, -9)]), 2)                   # streaming tail
        ph = f % 4                                                          # four-phase gallop
        legs = ([(-6, -15), (-1, -6), (3, 1)], [(0, -15), (6, -7), (11, -1)],
                [(10, -15), (14, -6), (18, 1)], [(14, -15), (19, -7), (24, -1)]) if ph < 2 else \
               ([(-6, -15), (-10, -7), (-13, -1)], [(0, -15), (-4, -7), (-7, 0)],
                [(10, -15), (6, -7), (3, -1)], [(14, -15), (10, -6), (8, 1)])
        for leg in legs:
            c.line(T(leg), 1.7)
        c.poly(T([(-2, -22), (3, -35), (9, -35), (6, -23)]))                # rider torso
        c.ell(*T([(7, -38)])[0], 4, 4)                                      # head
        c.poly(T([(0, -41), (14, -41), (11, -38), (2, -38)]))               # hat brim
        c.poly(T([(4, -45), (11, -45), (10, -41), (5, -41)]))               # hat crown
        im = c.small()
        d = ImageDraw.Draw(im)
        for i2, (sx, sy) in enumerate([(12, 12), (54, 16), (120, 10), (150, 30), (90, 22), (140, 60)]):
            d.point((sx, sy), fill=255 if (i2 + f) % 6 else 0)
        for k in range(4):                                                  # kicked-up dust behind
            dxp = round(x - 26 - k * 7)
            if 0 < dxp < W:
                d.point((dxp, 97 - (f + k) % 3), fill=255)
        frames.append(im)
        durs.append(190)
    frames[0].save(f"{OUT}/20_phantom_rider.gif", save_all=True, append_images=frames[1:], duration=durs, loop=0)


def mesh_relay():
    """A LoRa relay on the ridge: a timber lattice mast that throws signal rings out over the hills."""
    frames, durs = [], []
    N, tx, ty = 12, 80, 50
    for f in range(N):
        c = Big()
        c.poly([(0, 104), (30, 96), (70, 100), (110, 92), (160, 100), (160, H), (0, H)])  # hills
        c.line([(70, 104), (tx - 2, ty)], 1.6)          # the mast, two legs
        c.line([(90, 104), (tx + 2, ty)], 1.6)
        for k in range(6):                              # cross-braces, tapering in
            yy = 104 - (104 - ty) * k / 6
            wx = 10 - 8 * k / 6
            c.line([(80 - wx, yy), (80 + wx, yy - 4)], 0.8)
            c.line([(80 - wx, yy - 4), (80 + wx, yy)], 0.8)
        c.line([(tx, ty), (tx, ty - 7)], 1.3)           # whip antenna
        by = ty - 9
        for r0 in (6, 14, 22, 30):                      # signal rings pushing outward (on the crisp canvas)
            r = r0 + (f % 4) * 2
            c.arc(tx, by, r, 200, 340, 1.3)
        im = c.small()
        d = ImageDraw.Draw(im)
        if f % 4 < 2:                                   # the beacon blinks
            d.ellipse([tx - 2, by - 2, tx + 2, by + 2], fill=255)
        for i2, (sx, sy) in enumerate([(14, 14), (40, 10), (120, 12), (150, 32), (60, 18)]):
            d.point((sx, sy), fill=255 if (i2 + f) % 4 else 0)
        frames.append(im)
        durs.append(230)
    frames[0].save(f"{OUT}/22_mesh_relay.gif", save_all=True, append_images=frames[1:], duration=durs, loop=0)


def windmill():
    """A ranch windmill turning on the mesa, a little relay box bolted to its tower beaming rings out."""
    frames, durs = [], []
    N, cx, cy = 12, 82, 46
    for f in range(N):
        c = Big()
        c.poly([(0, 106), (60, 100), (100, 104), (160, 98), (160, H), (0, H)])  # mesa
        c.line([(74, 106), (cx - 1, cy + 6)], 1.4)      # tower legs
        c.line([(90, 106), (cx + 1, cy + 6)], 1.4)
        for k in range(5):
            yy = 106 - (106 - (cy + 6)) * k / 5
            wx = 8 - 6 * k / 5
            c.line([(82 - wx, yy), (82 + wx, yy - 3)], 0.7)
            c.line([(82 - wx, yy - 3), (82 + wx, yy)], 0.7)
        c.poly([(cx - 2, cy), (cx - 22, cy - 6), (cx - 22, cy + 6)])  # tail vane
        a0 = f * 0.5                                     # the fan wheel turns
        for b in range(14):
            a = a0 + b * 2 * math.pi / 14
            c.line([(cx, cy), (cx + 13 * math.cos(a), cy + 13 * math.sin(a))], 0.9)
        c.ell(cx, cy, 3, 3)                              # hub
        bx, by = cx + 6, cy + 18                         # relay box on the tower, throwing rings
        for r0 in (6, 13, 20):
            r = r0 + (f % 4) * 2
            c.arc(bx, by, r, 248, 358, 1.3)
        im = c.small()
        d = ImageDraw.Draw(im)
        d.rectangle([bx - 2, by - 2, bx + 2, by + 2], fill=255)
        for i2, (sx, sy) in enumerate([(16, 12), (44, 8), (120, 12), (150, 34)]):
            d.point((sx, sy), fill=255 if (i2 + f) % 4 else 0)
        frames.append(im)
        durs.append(190)
    frames[0].save(f"{OUT}/23_windmill.gif", save_all=True, append_images=frames[1:], duration=durs, loop=0)


def telegraph():
    """The talking wire: telegraph poles marching to the horizon, a packet of light running the line."""
    frames, durs = [], []
    N = 12
    poles = [(16, 66, 44), (52, 72, 34), (86, 78, 26), (112, 83, 19), (132, 87, 14)]  # x, top-y, height
    tops = [(px, py) for px, py, h in poles]
    for f in range(N):
        c = Big()
        c.line([(0, 114), (W, 102)], 1.4)               # ground sloping to the horizon
        for px, py, h in poles:
            c.line([(px, py + h), (px, py)], 1.6)        # pole
            c.line([(px - 5, py + 3), (px + 5, py + 3)], 1.1)  # crossarm
        wire = [(px, py + 3) for px, py, h in poles]
        for a, b in zip(wire, wire[1:]):                 # the wire, sagging pole to pole
            c.line([a, ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2 + 3), b], 1.2)
        seg = len(wire) - 1                              # a bright packet runs the wire
        t = (f / N) * seg
        i = min(int(t), seg - 1)
        fr = t - i
        a, b = wire[i], wire[i + 1]
        mx, my = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2 + 3
        px = (1 - fr) ** 2 * a[0] + 2 * (1 - fr) * fr * mx + fr ** 2 * b[0]
        py = (1 - fr) ** 2 * a[1] + 2 * (1 - fr) * fr * my + fr ** 2 * b[1]
        c.ell(px, py, 2.2, 2.2)                          # the packet (crisp canvas)
        im = c.small()
        d = ImageDraw.Draw(im)
        for i2, (sx, sy) in enumerate([(14, 12), (60, 10), (140, 14), (100, 22)]):
            d.point((sx, sy), fill=255 if (i2 + f) % 4 else 0)
        frames.append(im)
        durs.append(210)
    frames[0].save(f"{OUT}/24_telegraph.gif", save_all=True, append_images=frames[1:], duration=durs, loop=0)


def rattlesnake():
    """A rattler coiled on the sand, head up and swaying, tongue flicking, its rattle a blur."""
    frames, durs = [], []
    N = 10
    cx, cy = 62, 98
    for f in range(N):
        c = Big()
        c.line([(0, 110), (W, 110)], 1.4)                # the sand
        for r in (23, 17, 11):                           # the coiled body, a few loops
            c.arc(cx, cy, r, 20, 340, 2.6)
        sway = 0.12 * math.sin(f * 0.8)                  # the raised neck sways
        hx, hy = cx + 30 + 6 * math.sin(f * 0.8), cy - 40
        c.line([(cx + 9, cy - 7), (cx + 20, cy - 24), (hx, hy)], 2.8)   # neck
        c.poly([(hx, hy), (hx + 9, hy - 3), (hx + 11, hy + 2), (hx + 2, hy + 3)])  # wedge head
        if f % 3 < 2:                                    # the forked tongue flicks
            tx, ty = hx + 11, hy
            c.line([(tx, ty), (tx + 7, ty - 2)], 0.8)
            c.line([(tx + 7, ty - 2), (tx + 11, ty - 4)], 0.7)
            c.line([(tx + 7, ty - 2), (tx + 11, ty)], 0.7)
        for s in range(4):                               # the rattle, buzzing (jitters each frame)
            jx = (1.4 if f % 2 else -1.4) * (s + 1)
            ry = cy - 10 - s * 5
            c.poly([(cx - 26 + jx, ry), (cx - 21 + jx, ry - 1), (cx - 22 + jx, ry - 5), (cx - 27 + jx, ry - 4)])
        im = c.small()
        d = ImageDraw.Draw(im)
        d.point((round(hx + 3), round(hy)), fill=0)      # eye
        frames.append(im)
        durs.append(130 if f % 2 else 240)
    frames[0].save(f"{OUT}/26_rattlesnake.gif", save_all=True, append_images=frames[1:], duration=durs, loop=0)


def lora_brand():
    """A branding iron glows in the dark -- the mark is an antenna throwing waves, burning hot,
    shimmering and spitting sparks."""
    frames, durs = [], []
    N = 10
    mx, my = 86, 78
    for f in range(N):
        c = Big()
        c.line([(mx, my), (mx, my - 30)], 2.8)           # the mark: antenna mast
        c.line([(mx - 11, my), (mx + 11, my)], 2.8)      # base bar
        for k, rr in enumerate((9, 17, 25)):             # waves, the outer ones shimmer on and off
            if k < 2 or f % 2 == 0:
                c.arc(mx, my - 30, rr, 200, 340, 1.8)
        c.line([(mx - 11, my), (mx - 42, my + 22), (mx - 54, my + 28)], 2.4)  # the iron's rod
        c.ell(mx - 58, my + 30, 5, 3)                    # handle knob
        im = c.small()
        d = ImageDraw.Draw(im)
        random.seed(f)
        for k in range(6):                               # sparks off the hot tip
            ang = random.uniform(0.2, math.pi - 0.2)
            rr = random.randint(2, 11)
            d.point((int(mx + rr * math.cos(ang)), int(my - 30 - rr * math.sin(ang))), fill=255)
        frames.append(im)
        durs.append(200)
    frames[0].save(f"{OUT}/27_lora_brand.gif", save_all=True, append_images=frames[1:], duration=durs, loop=0)


def tumbleweed_roll():
    """A tumbleweed bounds across the flat and off into the dark."""
    frames, durs = [], []
    N = 14
    random.seed(7)
    strands = [(random.uniform(0, math.pi), random.uniform(7, 14)) for _ in range(10)]
    for f in range(N):
        c = Big()
        c.line([(0, 110), (W, 106)], 1.4)                # the ground
        x = -16 + f * (196 / (N - 1))                    # rolls in from the left and off the right
        y = 98 - abs(math.sin(f * 0.9)) * 11             # bouncing
        a0 = f * 0.6                                     # and rolling
        for a, r in strands:
            aa = a + a0
            c.line([(x - r * math.cos(aa), y - r * math.sin(aa)), (x + r * math.cos(aa), y + r * math.sin(aa))], 1.0)
        c.arc(x, y, 13, 0, 360, 1.0)                     # the rough outline
        im = c.small()
        d = ImageDraw.Draw(im)
        for i2, (sx, sy) in enumerate([(20, 16), (60, 10), (120, 14), (150, 40)]):
            d.point((sx, sy), fill=255 if (i2 + f) % 4 else 0)
        frames.append(im)
        durs.append(160)
    frames[0].save(f"{OUT}/28_tumbleweed.gif", save_all=True, append_images=frames[1:], duration=durs, loop=0)


raven()
boot_logo(sys.argv[2] if len(sys.argv) > 2 else None)
wanted()
skinwalker()
star()
ghost_town()
dont_answer()
campfire()
howl()
tracks()
mine()
lightning()
skull()
mothman()
jackalope()
wendigo()
thunderbird()
chupacabra()
dogman()
phantom_rider()
mesh_relay()
windmill()
telegraph()
rattlesnake()
lora_brand()
tumbleweed_roll()
print("done")

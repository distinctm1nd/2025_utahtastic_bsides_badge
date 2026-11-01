#!/usr/bin/env python3
"""Draws the Old West sample pictures for the name tag (160x128, white on black).

    python3 make_west_images.py ../images
"""
import math
import random
import sys

from PIL import Image, ImageDraw, ImageFont

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


wanted()
skinwalker()
star()
ghost_town()
dont_answer()
print("done")

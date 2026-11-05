#!/usr/bin/env python3
"""Builds BadgeFonts.h: extra fonts for the name tag, in the OLEDDisplay font format
(the same format as Meshtastic's ArialMT fonts), from open-licence TrueType fonts.

    python3 make_fonts.py          (needs Pillow and the fonts below; output is committed)

OLEDDisplay font format: [max width, height, first char, char count], then 4 bytes per
character (data offset MSB, LSB, byte count, advance width; offset 0xFFFF = no glyph), then
the glyph data: column by column, ceil(height/8) bytes per column, least significant bit at
the top. A byte count of 0 means "width x ceil(height/8)" (for glyphs over 255 bytes).
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(HERE, "BadgeFonts.h")
FIRST, LAST = 32, 126

FONTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fonts")
# (C name, display name, TTF, horizontal stretch, licence[, "ink": size by the letters themselves,
# for display fonts whose line metrics are much taller than their letters[, a variable font's
# named style, e.g. "Bold"]]).
# Add new families at the end: the badge keeps the chosen font as an index.
FAMILIES = [
    ("Wood", "Wood type", "/usr/share/fonts/truetype/dejavu/DejaVuSerifCondensed-Bold.ttf", 1.25, "DejaVu (Bitstream Vera licence)"),
    ("Serif", "Serif", "/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf", 1.0, "Liberation (SIL OFL 1.1)"),
    ("Type", "Typewriter", "/usr/share/fonts/truetype/freefont/FreeMonoBold.ttf", 1.0, "GNU FreeFont (GPL + font exception)"),
    ("Fancy", "Fancy", "/usr/share/fonts/truetype/freefont/FreeSerifBoldItalic.ttf", 1.0, "GNU FreeFont (GPL + font exception)"),
    # tools/fonts: from Google Fonts (github.com/google/fonts), licences next to them
    ("Ultra", "Wanted", os.path.join(FONTS, "Ultra-Regular.ttf"), 1.0, "Ultra (Apache 2.0)", "ink"),
    ("Holtwood", "Frontier", os.path.join(FONTS, "HoltwoodOneSC-Regular.ttf"), 1.0, "Holtwood One SC (SIL OFL 1.1)", "ink"),
    ("Fell", "Old print", os.path.join(FONTS, "IMFellEnglishSC-Regular.ttf"), 1.0, "IM Fell English SC (SIL OFL 1.1)", "ink"),
    ("Gazette", "Gazette", os.path.join(FONTS, "OldStandard-Bold.ttf"), 1.0, "Old Standard TT (SIL OFL 1.1)", "ink"),
    ("Tomb", "Tombstone", os.path.join(FONTS, "Cinzel-Variable.ttf"), 1.0, "Cinzel (SIL OFL 1.1)", "ink", "Bold"),
    ("Gothic", "Gothic", os.path.join(FONTS, "GrenzeGotisch-Variable.ttf"), 1.0, "Grenze Gotisch (SIL OFL 1.1)", "ink", "Bold"),
    ("Grimoire", "Grimoire", os.path.join(FONTS, "PirataOne-Regular.ttf"), 1.0, "Pirata One (SIL OFL 1.1)", "ink"),
    ("Undertaker", "Undertaker", os.path.join(FONTS, "PlayfairDisplaySC-Black.ttf"), 1.0, "Playfair Display SC (SIL OFL 1.1)", "ink"),
    ("Fraktur", "Fraktur", os.path.join(FONTS, "UnifrakturMaguntia-Book.ttf"), 1.0, "UnifrakturMaguntia (SIL OFL 1.1)", "ink"),
    ("Scripture", "Scripture", os.path.join(FONTS, "Texturina-Variable.ttf"), 1.0, "Texturina (SIL OFL 1.1)", "ink", "Bold"),
    ("Saloon", "Saloon", os.path.join(FONTS, "VastShadow-Regular.ttf"), 1.0, "Vast Shadow (SIL OFL 1.1)", "ink"),
    ("Weathered", "Weathered", os.path.join(FONTS, "FrederickatheGreat-Regular.ttf"), 1.0, "Fredericka the Great (SIL OFL 1.1)", "ink"),
]
SAMPLE = "".join(chr(c) for c in range(33, 127))
# target line heights in pixels: Big, Medium, Small
SIZES = [("Big", 40), ("Med", 28), ("Small", 19)]


VARIATION = None  # named style of the variable font being built


def truetype(path, size):
    f = ImageFont.truetype(path, size)
    if VARIATION:
        f.set_variation_by_name(VARIATION)
    return f


def fit_ink(path, height):
    """Largest point size whose printable letters (top of the tallest to bottom of the lowest)
    fit the line height. Returns the font and where the baseline goes."""
    for size in range(height * 2, 5, -1):
        f = truetype(path, size)
        top = min(f.getbbox(c, anchor="ls")[1] for c in SAMPLE)
        bottom = max(f.getbbox(c, anchor="ls")[3] for c in SAMPLE)
        if bottom - top <= height:
            return f, -top
    raise SystemExit("font too small")


def fit_font(path, height):
    """Largest point size whose ascent+descent fits the line height."""
    size = height
    while size > 6:
        f = ImageFont.truetype(path, size)
        a, d = f.getmetrics()
        if a + d <= height:
            return f, a
        size -= 1
    raise SystemExit("font too small")


def glyph(f, ascent, height, ch, stretch):
    adv = f.getlength(ch)
    w = max(1, int(round(adv)) + 2)
    im = Image.new("1", (w + 8, height), 0)
    xo = 1 - min(0, f.getbbox(ch, anchor="ls")[0])  # italics lean left of the origin
    ImageDraw.Draw(im).text((xo, ascent), ch, font=f, fill=1, anchor="ls")
    box = im.getbbox()
    # advance: the font's own, so spacing matches; keep any overhang inside the cell
    right = max(int(round(adv)), box[2] if box else 0)
    im = im.crop((0, 0, right, height))
    if stretch != 1.0:
        im = im.resize((max(1, int(round(im.width * stretch))), height), Image.NEAREST)
    return im


def encode(im):
    w, h = im.size
    rows = (h + 7) // 8
    px = im.load()
    data = []
    for x in range(w):
        for r in range(rows):
            b = 0
            for bit in range(8):
                y = r * 8 + bit
                if y < h and px[x, y]:
                    b |= 1 << bit
            data.append(b)
    full = len(data)
    while data and data[-1] == 0:  # trailing empty bytes needn't be stored
        data.pop()
    if len(data) > 255:  # too big for the size byte: store all of it, size 0
        data = data + [0] * (full - len(data))
        return data, 0
    return data, len(data)


def build(path, height, stretch, ink=False):
    f, ascent = fit_ink(path, height) if ink else fit_font(path, height)
    table, blob, maxw = [], [], 0
    for c in range(FIRST, LAST + 1):
        g = glyph(f, ascent, height, chr(c), stretch)
        maxw = max(maxw, g.width)
        data, size = encode(g)
        if not data:
            table.append((0xFF, 0xFF, 0, g.width))  # blank (space)
            continue
        off = len(blob)
        if off > 0xFFFE:
            raise SystemExit("font too large")
        table.append((off >> 8, off & 0xFF, size, g.width))
        blob.extend(data)
    head = [maxw, height, FIRST, LAST - FIRST + 1]
    out = head + [b for t in table for b in t] + blob
    return out


def main():
    lines = [
        "// Generated by tools/make_fonts.py: name tag fonts in the OLEDDisplay format. Don't edit.",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "namespace badge",
        "{",
    ]
    names = []
    global VARIATION
    for cname, disp, path, stretch, lic, *opt in FAMILIES:
        VARIATION = opt[1].encode() if len(opt) > 1 else None
        if not os.path.exists(path):
            sys.exit("missing font " + path)
        lines.append(f"// {disp}: {lic}")
        row = []
        for sname, h in SIZES:
            data = build(path, h, stretch, bool(opt) and opt[0] == "ink")
            var = f"Font{cname}{sname}"
            row.append(var)
            lines.append(f"static const uint8_t {var}[] = {{ // height {h}, {len(data)} bytes")
            for i in range(0, len(data), 24):
                lines.append("    " + ", ".join(str(b) for b in data[i:i + 24]) + ",")
            lines.append("};")
        names.append((disp, row))
    lines.append("")
    lines.append(f"static const int BADGE_FONT_FAMILIES = {len(names)};")
    lines.append("static const char *const BADGE_FONT_NAMES[] = {" + ", ".join(f'"{n}"' for n, _ in names) + "};")
    lines.append("// [family][0 big, 1 medium, 2 small]")
    lines.append("static const uint8_t *const BADGE_FONTS[][3] = {")
    for _, row in names:
        lines.append("    {" + ", ".join(row) + "},")
    lines.append("};")
    lines.append("")
    lines.append("} // namespace badge")
    open(OUT, "w").write("\n".join(lines) + "\n")
    print("wrote", OUT)


if __name__ == "__main__":
    main()

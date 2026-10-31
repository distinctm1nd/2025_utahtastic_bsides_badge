#!/usr/bin/env python3
"""Turn scene_preview's JSON into an animated GIF of the badge LEDs (needs Pillow).

usage: python3 scene_preview.py scenes.json [scene name ...]
"""
import json
import sys

from PIL import Image, ImageDraw, ImageFilter

W, H, CELL = 220, 200, 4  # one tile per scene


def tile(leds, colors, name):
    im = Image.new("RGB", (W, H), (12, 12, 16))
    glow = Image.new("RGB", (W, H), (0, 0, 0))
    g = ImageDraw.Draw(glow)
    for (x, y, grp), c in zip(leds, colors):
        px, py = 10 + x * (W - 20), 22 + y * (H - 32)
        r = 9 if grp == 0 else 6
        g.ellipse([px - r, py - r, px + r, py + r], fill=tuple(c))
    glow = glow.filter(ImageFilter.GaussianBlur(3))
    im = Image.blend(im, glow, 0.9)
    d = ImageDraw.Draw(im)
    d.text((6, 4), name, fill=(220, 220, 220))
    return im


def main():
    data = json.load(open(sys.argv[1]))
    want = set(sys.argv[2:])
    scenes = [s for s in data["scenes"] if not want or s["name"] in want]
    cols = 6
    rows = (len(scenes) + cols - 1) // cols
    frames = []
    nframes = len(scenes[0]["frames"])
    for f in range(0, nframes, 2):
        sheet = Image.new("RGB", (cols * W, rows * H))
        for k, s in enumerate(scenes):
            flat = s["frames"][f]
            colors = [flat[i:i + 3] for i in range(0, len(flat), 3)]
            sheet.paste(tile(data["leds"], colors, s["name"]), ((k % cols) * W, (k // cols) * H))
        frames.append(sheet.convert("P", palette=Image.ADAPTIVE, colors=128))
    frames[0].save("scenes.gif", save_all=True, append_images=frames[1:], duration=int(2000 / data["fps"]), loop=0)
    print("wrote scenes.gif:", len(scenes), "scenes,", len(frames), "frames")


if __name__ == "__main__":
    main()

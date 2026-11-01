#!/usr/bin/env python3
"""mine.raw (frames from mine_preview) -> mine.gif at 2x, with a HUD strip like the badge."""
import sys
from PIL import Image
W, H = 160, 108
data = open(sys.argv[1], "rb").read()
n = len(data) // (W // 8 * H)
frames = []
for f in range(n):
    im = Image.new("L", (W, 128), 0)
    px = im.load()
    base = f * (W // 8 * H)
    for y in range(H):
        for x in range(W):
            if data[base + y * 20 + x // 8] >> (x & 7) & 1:
                px[x, y] = 255
    frames.append(im.resize((W * 2, 256), Image.NEAREST))
frames[0].save("mine.gif", save_all=True, append_images=frames[1:], duration=130, loop=0)
print("wrote mine.gif:", n, "frames")

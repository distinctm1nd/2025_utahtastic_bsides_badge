#!/usr/bin/env python3
"""Turn the pictures in variants/esp32s3/utah_bsides/images/ into BadgeImagesData.h.

Runs automatically before every PlatformIO build (extra_scripts in platformio.ini), and can
be run by hand:  python3 make_images.py

Drop .png .jpg .jpeg .gif .bmp or .webp files into images/. Each one is scaled to fit the
160x128 screen, turned black-and-white with dithering (the Meshtastic screen driver draws
one colour on black), and shown on the badge's name tag pages in file-name order. Animated
GIFs keep up to 24 frames. Name files 01_first.png, 02_next.gif ... to choose the order.
Images with light backgrounds look best inverted: put "_inv" in the file name.

Frames are stored packed (see BadgeImageCodec.h): each frame XOR the one before, then PackBits.
Black areas and the parts of an animation that don't change take almost no space.
"""
import hashlib
import os
import sys

VERSION = "3"  # 2: frames packed (XOR with the previous frame, then PackBits); 3: per-frame timing
W, H = 160, 128
MAX_FRAMES = 24
MAX_SHOWN = 96  # frames after repeating slow ones
BUDGET = 2 * 1024 * 1024  # bytes of image data in the firmware (6 MB app slot; more go on the file system)
EXTS = (".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp")

try:  # running inside PlatformIO
    Import("env")  # noqa: F821
    ENV = env  # noqa: F821
    HERE = os.path.join(ENV.subst("$PROJECT_DIR"), "variants", "esp32s3", "utah_bsides")
except NameError:
    ENV = None
    HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

IMG_DIR = os.path.join(HERE, "images")
OUT = os.path.join(HERE, "BadgeImagesData.h")


def log(*a):
    print("[badge images]", *a)


def list_images():
    if not os.path.isdir(IMG_DIR):
        return []
    return sorted(f for f in os.listdir(IMG_DIR) if f.lower().endswith(EXTS))


def fingerprint(files):
    h = hashlib.sha1(VERSION.encode())
    for f in files:
        h.update(f.encode())
        with open(os.path.join(IMG_DIR, f), "rb") as fh:
            h.update(fh.read())
    return h.hexdigest()


def current_fingerprint():
    try:
        with open(OUT) as fh:
            first = fh.readline()
        return first.split("fingerprint ")[1].strip() if "fingerprint " in first else None
    except OSError:
        return None


def get_pillow():
    try:
        from PIL import Image  # noqa: F401
        return True
    except ImportError:
        pass
    if ENV is not None:
        log("installing Pillow into PlatformIO's Python")
        ENV.Execute('"$PYTHONEXE" -m pip install -q pillow')
        try:
            from PIL import Image  # noqa: F401
            return True
        except ImportError:
            pass
    return False


def to_xbm(img, invert):
    from PIL import Image, ImageOps

    img = img.convert("RGBA")
    bg = Image.new("RGBA", img.size, (0, 0, 0, 255))
    img = Image.alpha_composite(bg, img).convert("L")
    scale = min(W / img.width, H / img.height)
    size = (max(1, int(img.width * scale)), max(1, int(img.height * scale)))
    img = img.resize(size, Image.LANCZOS)
    img = ImageOps.autocontrast(img, cutoff=1)
    if invert:
        img = ImageOps.invert(img)
    bw = img.convert("1")  # Floyd-Steinberg dithering
    w, h = bw.size
    stride = (w + 7) // 8
    px = bw.load()
    out = bytearray(stride * h)
    for y in range(h):
        for x in range(w):
            if px[x, y]:
                out[y * stride + x // 8] |= 1 << (x % 8)
    return w, h, bytes(out)


def packbits(data):
    out, i, n = bytearray(), 0, len(data)
    while i < n:
        run = 1
        while i + run < n and run < 128 and data[i + run] == data[i]:
            run += 1
        if run >= 3:
            out += bytes([257 - run, data[i]])
            i += run
            continue
        j = i  # literals until the next run of 3 or more
        while j < n and j - i < 128:
            if j + 2 < n and data[j] == data[j + 1] == data[j + 2]:
                break
            j += 1
        out += bytes([j - i - 1]) + data[i:j]
        i = j
    return bytes(out)


def fnv(h, data):
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def convert(files):
    from PIL import Image, ImageSequence

    images, total = [], 0
    for f in files:
        path = os.path.join(IMG_DIR, f)
        try:
            src = Image.open(path)
        except Exception as e:  # noqa: BLE001
            log("skipping", f, "-", e)
            continue
        invert = "_inv" in f.lower()
        frames, times = [], []
        for fr in ImageSequence.Iterator(src):
            frames.append(fr.copy())
            times.append(int(fr.info.get("duration", src.info.get("duration", 100)) or 100))
        step = max(1, (len(frames) + MAX_FRAMES - 1) // MAX_FRAMES)
        frames = frames[::step][:MAX_FRAMES]
        times = [sum(times[k:k + step]) for k in range(0, step * len(frames), step)][: len(frames)]
        # the badge plays every frame for the same time: show slow frames several times (a
        # repeated frame packs to almost nothing), so pauses and quick moves keep their timing
        duration = max(40, min(times))
        while sum(max(1, round(t / duration)) for t in times) > MAX_SHOWN:
            duration += 10
        frames = [fr for fr, t in zip(frames, times) for _ in range(max(1, round(t / duration)))]
        data, w, h, raw, check = b"", 0, 0, 0, 2166136261
        prev = None
        for fr in frames:
            w, h, bits = to_xbm(fr, invert)
            delta = bits if prev is None else bytes(a ^ b for a, b in zip(bits, prev))
            data += packbits(delta)
            prev = bits
            raw += len(bits)
            check = fnv(check, bits)
        if total + len(data) > BUDGET:
            log("skipping", f, "- image budget full (%d KB)" % (BUDGET // 1024))
            continue
        total += len(data)
        name = os.path.splitext(f)[0]
        images.append((name, w, h, len(frames), max(40, min(duration, 5000)), data, check))
        log("%s: %dx%d, %d frame(s), %d KB packed from %d KB" % (f, w, h, len(frames), (len(data) + 1023) // 1024, (raw + 1023) // 1024))
    return images, total


def write(images, fp):
    lines = ["// generated by tools/make_images.py from images/ - fingerprint %s" % fp,
             "#pragma once",
             "#include <stdint.h>",
             "",
             "namespace badge",
             "{",
             "struct BadgeImage {",
             "    const char *name;",
             "    uint16_t w, h, frames, frameMs;",
             "    const uint8_t *bits; // packed frames, see BadgeImageCodec.h",
             "    uint32_t check;      // FNV-1a of all unpacked frames (for tools/image_test.cpp)",
             "};",
             ""]
    for i, (name, w, h, n, ms, data, check) in enumerate(images):
        lines.append("static const uint8_t BADGE_IMG_%d[] = {" % i)
        for j in range(0, len(data), 24):
            lines.append("    " + ", ".join("0x%02x" % b for b in data[j:j + 24]) + ",")
        lines.append("};")
    if images:
        lines.append("static const BadgeImage BADGE_IMAGE_TABLE[] = {")
        for i, (name, w, h, n, ms, data, check) in enumerate(images):
            safe = name.replace("\\", "/").replace('"', "'")
            lines.append('    {"%s", %d, %d, %d, %d, BADGE_IMG_%d, 0x%08xu},' % (safe, w, h, n, ms, i, check))
        lines.append("};")
        lines.append("static const BadgeImage *const BADGE_IMAGES = BADGE_IMAGE_TABLE;")
    else:
        lines.append("static const BadgeImage *const BADGE_IMAGES = nullptr;")
    lines.append("static const int BADGE_IMAGE_COUNT = %d;" % len(images))
    lines.append("} // namespace badge")
    with open(OUT, "w") as fh:
        fh.write("\n".join(lines) + "\n")


def main():
    files = list_images()
    fp = fingerprint(files)
    if fp == current_fingerprint():
        return
    if not files:
        write([], fp)
        log("no pictures in images/")
        return
    if not get_pillow():
        if os.path.exists(OUT):
            log("Pillow not available; keeping the existing BadgeImagesData.h")
        else:
            write([], "none")
            log("Pillow not available; building without pictures")
        return
    images, total = convert(files)
    write(images, fp)
    log("%d picture(s), %d KB" % (len(images), total // 1024))


main()

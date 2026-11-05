#!/usr/bin/env python3
"""Sign built firmware for WiFi updates, outside PlatformIO (the release workflow's signing job).

  BADGE_SIGNING_KEY=... python3 sign_file.py firmware-utah_bsides-X.bin [...]

Writes firmware-utah_bsides-X.update.bin next to each: the image, the 64-byte Ed25519
signature of its SHA-256, then "BSIG". Uses only the Python standard library."""
import hashlib
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ed25519_sign  # noqa: E402
import update_key  # noqa: E402

if __name__ == "__main__":
    files = [f for f in sys.argv[1:] if f.endswith(".bin") and not f.endswith((".factory.bin", ".update.bin"))]
    if not files:
        sys.exit(__doc__)
    seed = update_key.load_seed()
    if not seed:
        sys.exit("no signing key (BADGE_SIGNING_KEY)")
    pub = ed25519_sign.public_key(seed)
    with open(update_key.HEADER) as fh:
        trusted = bytes(int(h, 16) for h in re.findall(r"0x([0-9a-f]{2})", fh.read()))
    if trusted != pub:
        sys.exit("this key doesn't match BadgeUpdateKey.h: badges would reject the update")
    for f in files:
        with open(f, "rb") as fh:
            image = fh.read()
        out = f[: -len(".bin")] + ".update.bin"
        with open(out, "wb") as fh:
            fh.write(image + ed25519_sign.sign(seed, hashlib.sha256(image).digest()) + b"BSIG")
        print("signed:", os.path.basename(out))

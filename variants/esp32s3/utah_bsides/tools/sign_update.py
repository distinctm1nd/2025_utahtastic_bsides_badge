"""After each build: write firmware-*.update.bin, the firmware signed for WiFi updates
(image, then the 64-byte Ed25519 signature of its SHA-256, then "BSIG"). Skipped, with a
note, when the signing key isn't available (see update_key.py)."""
import hashlib
import os
import sys

Import("env")  # noqa: F821

TOOLS = os.path.join(env.subst("$PROJECT_DIR"), "variants", "esp32s3", "utah_bsides", "tools")  # noqa: F821
sys.path.insert(0, TOOLS)
import ed25519_sign  # noqa: E402
import update_key  # noqa: E402


def sign(source, target, env):
    image_path = str(target[0])
    seed = update_key.load_seed()
    if not seed:
        print("[badge update] no signing key: no .update.bin (tools/update_key.py)")
        return
    with open(image_path, "rb") as fh:
        image = fh.read()
    sig = ed25519_sign.sign(seed, hashlib.sha256(image).digest())
    out = image_path[: -len(".bin")] + ".update.bin"
    with open(out, "wb") as fh:
        fh.write(image + sig + b"BSIG")
    print("[badge update] signed:", os.path.basename(out))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", sign)  # noqa: F821

#!/usr/bin/env python3
"""The signing key for WiFi firmware updates.

  python3 update_key.py new    make a key pair: the private key goes to
                               ~/.config/utah-bsides/update-signing.key (keep it safe, never
                               commit it), the public key into ../BadgeUpdateKey.h (commit that)

The badge only installs updates signed with the private key. Builds sign automatically when
the key is in that file or in the BADGE_SIGNING_KEY environment variable (64 hex digits, e.g.
a GitHub Actions secret). Losing the key means updating over USB / the web installer once
with a new key's build.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ed25519_sign  # noqa: E402

KEY_FILE = os.path.expanduser("~/.config/utah-bsides/update-signing.key")
HEADER = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "BadgeUpdateKey.h")


def load_seed():
    hexkey = os.environ.get("BADGE_SIGNING_KEY", "").strip()
    if not hexkey and os.path.exists(KEY_FILE):
        with open(KEY_FILE) as fh:
            hexkey = fh.read().strip()
    if len(hexkey) == 64:
        return bytes.fromhex(hexkey)
    return None


def write_header(pub):
    with open(HEADER, "w") as fh:
        fh.write("// Public key for signed WiFi firmware updates (tools/update_key.py). All zeros: WiFi\n")
        fh.write("// updates are switched off in this build.\n#pragma once\n#include <stdint.h>\n\n")
        fh.write("namespace badge\n{\nstatic const uint8_t UPDATE_PUBLIC_KEY[32] = {\n")
        fh.write("    " + ", ".join("0x%02x" % b for b in pub[:16]) + ",\n")
        fh.write("    " + ", ".join("0x%02x" % b for b in pub[16:]) + "};\n} // namespace badge\n")


if __name__ == "__main__":
    if sys.argv[1:] != ["new"]:
        sys.exit(__doc__)
    if os.path.exists(KEY_FILE):
        sys.exit("%s already exists; move it away first if you really want a new key" % KEY_FILE)
    seed = os.urandom(32)
    os.makedirs(os.path.dirname(KEY_FILE), mode=0o700, exist_ok=True)
    fd = os.open(KEY_FILE, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w") as fh:
        fh.write(seed.hex() + "\n")
    write_header(ed25519_sign.public_key(seed))
    print("private key:", KEY_FILE, "(back it up; for GitHub Actions add it as secret BADGE_SIGNING_KEY)")
    print("public key: ", HEADER, "(commit it)")

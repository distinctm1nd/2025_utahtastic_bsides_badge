#!/usr/bin/env python3
# Bring MeshCore's mesh engine into the badge build as the badge's second mesh (Meshtastic or
# MeshCore, picked at startup - see BadgeMeshCore.cpp).
#
# Only MeshCore's core is used: the protocol (Mesh, Dispatcher, Packet, Identity), the chat layer
# (BaseChatMesh) and its RadioLib wrapper. Those files are linked into generated/meshcore/ (git
# ignored, compiled with the badge like everything under this folder) from a MeshCore checkout:
#   $MESHCORE_DIR, else ../utahtastic-meshcore next to this repo, else .pio/meshcore
# (the badge's fork, github.com/distinctm1nd/MeshCore). With no checkout the badge builds without
# MeshCore: BADGE_MESHCORE stays undefined and BadgeMeshCore.cpp compiles to nothing.
import os
import shutil

Import("env")  # noqa: F821

project = env["PROJECT_DIR"]  # noqa: F821
here = os.path.join(project, "variants", "esp32s3", "utah_bsides")
dest = os.path.join(here, "generated", "meshcore")

CORE = [
    "src/MeshCore.h",
    "src/Mesh.h",
    "src/Mesh.cpp",
    "src/Dispatcher.h",
    "src/Dispatcher.cpp",
    "src/Packet.h",
    "src/Packet.cpp",
    "src/Identity.h",
    "src/Identity.cpp",
    "src/Utils.h",
    "src/Utils.cpp",
    "src/helpers/BaseChatMesh.h",
    "src/helpers/BaseChatMesh.cpp",
    "src/helpers/AdvertDataHelpers.h",
    "src/helpers/AdvertDataHelpers.cpp",
    "src/helpers/TxtDataHelpers.h",
    "src/helpers/TxtDataHelpers.cpp",
    "src/helpers/UTF8Helpers.h",
    "src/helpers/ContactInfo.h",
    "src/helpers/ChannelDetails.h",
    "src/helpers/StaticPoolPacketManager.h",
    "src/helpers/StaticPoolPacketManager.cpp",
    "src/helpers/SimpleMeshTables.h",
    "src/helpers/ArduinoHelpers.h",
    "src/helpers/radiolib/RadioLibWrappers.h",
    "src/helpers/radiolib/RadioLibWrappers.cpp",
]


def find_checkout():
    for d in (
        os.environ.get("MESHCORE_DIR", ""),
        os.path.join(os.path.dirname(project), "utahtastic-meshcore"),
        os.path.join(project, ".pio", "meshcore"),
    ):
        if d and os.path.isfile(os.path.join(d, "src", "Mesh.h")):
            return os.path.abspath(d)
    return None


def link(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if os.path.islink(dst) and os.readlink(dst) == src:
        return
    if os.path.lexists(dst):
        os.unlink(dst)
    try:
        os.symlink(src, dst)
    except OSError:
        shutil.copyfile(src, dst)


checkout = find_checkout()
if not checkout:
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    print("[badge meshcore] no MeshCore checkout found: building without MeshCore")
else:
    wanted = set()
    for rel in CORE:
        # src/X -> generated/meshcore/X, so <Mesh.h> and <helpers/...> resolve from one include dir
        out = os.path.join(dest, rel[len("src/") :])
        link(os.path.join(checkout, rel), out)
        wanted.add(out)
    ed = os.path.join(checkout, "lib", "ed25519")
    for name in sorted(os.listdir(ed)):
        if name.endswith((".c", ".h")):
            out = os.path.join(dest, "ed25519", name)
            link(os.path.join(ed, name), out)
            wanted.add(out)
    for root, _dirs, files in os.walk(dest):  # drop links to files no longer on the list
        for f in files:
            p = os.path.join(root, f)
            if p not in wanted:
                os.unlink(p)
    env.Append(  # noqa: F821
        CPPPATH=[dest, os.path.join(dest, "ed25519")],
        CPPDEFINES=[
            "BADGE_MESHCORE",
            # sizes and defaults MeshCore's sources expect as build flags
            ("MAX_CONTACTS", 100),
            ("MAX_GROUP_CHANNELS", 8),
            ("LORA_FREQ", "910.525"),
            ("LORA_BW", "62.5"),
            ("LORA_SF", 7),
            ("LORA_TX_POWER", 22),
        ],
    )
    print("[badge meshcore] MeshCore core from", checkout)

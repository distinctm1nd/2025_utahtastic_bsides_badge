#!/usr/bin/env python3
# Make the badge sources visible to PlatformIO as part of src/ WITHOUT editing Meshtastic's src/.
#
# The badge .cpp files live in variants/esp32s3/utah_bsides/, which is outside PlatformIO's
# src_dir. Adding that folder to build_src_filter with a +<../...> entry re-roots the whole
# filter to the repo root (so +<*> stops matching src/) and also puts src/ outside the library
# dependency finder's scan (so Arduino lib_deps like arduino-fsm/ArduinoThread never build).
#
# Instead, drop a symlink src/utah_bsides -> ../variants/esp32s3/utah_bsides. The inherited
# esp32 filter's +<*> then compiles the badge alongside src/ with no ../ entry, and the LDF
# scans everything normally. The symlink is git-ignored and generated per build, so Meshtastic's
# src/ tree is never modified and upstream merges are unaffected.
import os

Import("env")  # noqa: F821

project_dir = env["PROJECT_DIR"]  # noqa: F821
link = os.path.join(project_dir, "src", "utah_bsides")
target = os.path.join("..", "variants", "esp32s3", "utah_bsides")  # relative to src/

try:
    if os.path.islink(link):
        if os.readlink(link) != target:
            os.unlink(link)
            os.symlink(target, link)
    elif not os.path.exists(link):
        os.symlink(target, link)
    # If a real (non-symlink) path already exists, leave it; the user put it there.
except OSError as e:
    print("*** link_badge_sources: could not create src/utah_bsides symlink: %s ***" % e)

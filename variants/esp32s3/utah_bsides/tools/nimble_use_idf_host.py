#!/usr/bin/env python3
# NimBLE-Arduino 2.x ships its own copy of the NimBLE host (src/nimble/...). On ESP32 the
# Arduino framework already provides the IDF NimBLE host, so compiling both yields "multiple
# definition of ble_gap_*" at link. Patch the installed library's library.json to drop the
# bundled host (srcFilter -<nimble/>), leaving only the C++ wrapper, which then binds to the
# framework's IDF NimBLE. We enable the NimBLE central+observer roles in custom_sdkconfig so
# that host (rebuilt from source) actually exports the ble_gattc_*/scan symbols the wrapper needs.
#
# Runs as a pre: script, after lib_deps are installed and before the build graph is built.
import json
import os

Import("env")  # noqa: F821

libdeps = env.get("PROJECT_LIBDEPS_DIR") or os.path.join(  # noqa: F821
    env["PROJECT_DIR"], ".pio", "libdeps"  # noqa: F821
)
lib_json = os.path.join(libdeps, env["PIOENV"], "NimBLE-Arduino", "library.json")  # noqa: F821

EXCLUDE = "-<nimble/>"
lib_dir = os.path.dirname(lib_json)
if not os.path.isdir(lib_dir):
    # Library not installed yet; PlatformIO installs lib_deps before this pre: script runs, so
    # this should not happen on a real build. Skip quietly if it does.
    print("*** nimble_use_idf_host: NimBLE-Arduino not installed at %s; skipped ***" % lib_dir)
else:
    # NimBLE-Arduino ships a library.properties (no library.json), so there's usually nothing to
    # read; CREATE a library.json, which PlatformIO prefers, with srcFilter dropping the bundled
    # host (src/nimble/) while keeping the C++ wrapper. The wrapper then binds to the framework's
    # IDF NimBLE host (which we build with the central+observer roles via custom_sdkconfig).
    data = {}
    try:
        with open(lib_json) as f:
            data = json.load(f)
    except (OSError, ValueError):
        data = {}
    data.setdefault("name", "NimBLE-Arduino")
    build = data.setdefault("build", {})
    src_filter = build.get("srcFilter")
    if not isinstance(src_filter, list):
        build["srcFilter"] = ["+<*>", EXCLUDE]
    elif EXCLUDE not in src_filter:
        src_filter.append(EXCLUDE)
    with open(lib_json, "w") as f:
        json.dump(data, f, indent=2)
    print("*** nimble_use_idf_host: NimBLE-Arduino set to use the framework IDF NimBLE host ***")

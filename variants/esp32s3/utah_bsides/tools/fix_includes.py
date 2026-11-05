# The badge's .cpp files live in this variant folder (pulled into the build via build_src_filter)
# and include Meshtastic headers that reach into library headers (nanopb's <pb.h>, ArduinoThread's
# "Thread.h", etc.). Meshtastic's own src/ gets those library include paths; files compiled from
# variants/ don't, so add every installed library's headers to the PROJECT build env.
#
# This touches projenv (project sources only), NOT the global env: adding these globally would put
# library headers like syscfg.h / os.h ahead of the ones the ESP-IDF components compile against and
# break the framework build. NimBLE-Arduino is skipped for the same reason (it ships its own
# syscfg/ and nimble/); the badge's files reach it through their direct #include instead.
import glob
import json
import os

Import("projenv")  # noqa: F821

libdeps = os.path.join(projenv.subst("$PROJECT_LIBDEPS_DIR"), projenv.subst("$PIOENV"))  # noqa: F821
paths = []
for lib in sorted(glob.glob(os.path.join(libdeps, "*"))):
    if not os.path.isdir(lib) or os.path.basename(lib) == "NimBLE-Arduino":
        continue
    cands = [lib, os.path.join(lib, "src"), os.path.join(lib, "include")]
    manifest = os.path.join(lib, "library.json")
    if os.path.isfile(manifest):
        try:
            inc = json.load(open(manifest)).get("build", {}).get("includeDir")
            if inc is not None:
                cands.insert(0, os.path.join(lib, inc) if inc else lib)
        except (ValueError, OSError):
            pass
    for d in cands:
        if os.path.isdir(d) and d not in paths:
            paths.append(d)
if paths:
    projenv.Append(CPPPATH=paths)  # noqa: F821

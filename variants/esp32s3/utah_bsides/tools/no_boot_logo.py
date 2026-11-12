#!/usr/bin/env python3
# No Meshtastic logo at startup, without changing Meshtastic's source.
#
# platformio.ini tells the linker (-Wl,--wrap) to send Screen.cpp's one call of
# UIRenderer::drawBootIconScreen() to the badge's __wrap_ function in variant.cpp, which draws the
# raven. The linker can only do that for a call it actually sees, and with link-time optimisation
# the compiler resolves the call first. So Screen.cpp alone is built without LTO.
# (Only the compile command line is changed, with the flag put last: every other setting is
# still read when the file is built, after Meshtastic's own build script has added its flags.)
Import("env")  # noqa: F821


def without_lto(env, node):
    return env.Object(node, CXXCOM=env["CXXCOM"].replace("$SOURCES", "-fno-lto $SOURCES"))


env.AddBuildMiddleware(without_lto, "*/graphics/Screen.cpp")  # noqa: F821

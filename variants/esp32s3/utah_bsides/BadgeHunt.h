// Scavenger hunt: organisers hide beacon nodes around the venue; a badge that hears a
// beacon collects its flag and shows the clue to the next one.
//
// A beacon is any badge built with -D BADGE_HUNT_BEACON=<flag number> (see README.md). It
// broadcasts "BHNF" + flag number + token on the private app port every HUNT_BEACON_MS, with
// hop limit 0 so the mesh never relays it. A badge collects the flag only when it hears the
// beacon directly at HUNT_MIN_RSSI or stronger (about the same room); weaker, it says
// "get closer".
//
// Each badge build env utah_bsides_beacon<N> in platformio.ini is a ready-made beacon.
// Edit the flag table in BadgeHunt.cpp for your event (names, tokens, clues).
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifndef HUNT_MIN_RSSI
#define HUNT_MIN_RSSI -75 // dBm
#endif
#ifndef HUNT_BEACON_MS
#define HUNT_BEACON_MS 20000
#endif

namespace badge
{

struct HuntFlag {
    const char *name;  // shown when found
    const char *token; // 8 characters the beacon sends
    const char *clue;  // shown after finding it: where to look next
};

int huntFlagCount();
const HuntFlag &huntFlag(int i);

// a hunt packet arrived (payload after the "BHN" magic); returns the flag found, or -1
int huntHandlePacket(const uint8_t *p, size_t len);
// beacon badges: build the packet to broadcast (returns length, 0 if not a beacon)
size_t huntBeaconPacket(uint8_t *out, size_t max);

} // namespace badge

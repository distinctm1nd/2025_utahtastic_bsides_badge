// Sightings log for the fox hunt watch: a CSV file on the badge's file system
// (/badge/sightings.csv, moved to sightings.old.csv at 512 KB) plus an index of the marked
// devices seen before, so a tracker that turns up again later ("seen 5 h ago") stands out.
//
// Line: epoch (0 if the badge doesn't know the time), boot, uptime s, event, code, address,
// dBm, latitude, longitude (1e-7 degrees, 0 if the badge doesn't know where it is), title,
// extra. Events: SEEN, FOLW (10 min with you), MOVED (followed you), GONE (with minutes),
// AGAIN (seen before), ATTACK, MESH (another badge's sighting).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace badge
{

void logEvent(const char *event, const char *code, const uint8_t mac[6], int rssi, const char *title,
              const char *extra = "");

// a marked device has just turned up: how many earlier sightings, and seconds since the last
// one ended (0 if unknown). Notes this sighting.
int logSeenBefore(const uint8_t mac[6], uint32_t *agoS);

// newest lines first, for the log viewer; returns how many were read
struct LogLine {
    char text[100];
};
int logReadRecent(LogLine *out, int max);
void logClear();

} // namespace badge

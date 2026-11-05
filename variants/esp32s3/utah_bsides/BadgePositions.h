// Where the badge and the nodes it has heard are, from Meshtastic's node database: other
// nodes' shared positions, and the badge's own position (shared from the phone, or a fixed
// position set in the app). Used by the node radar and the Badge WiFi map.
#pragma once
#include <stdint.h>

namespace badge
{

struct NodePos {
    uint32_t num;
    char name[40];
    int32_t lat, lon;  // 1e-7 degrees
    float distM;       // from the badge, -1 if the badge doesn't know where it is
    float bearing;     // degrees from north, clockwise
    uint32_t ageS;     // since last heard, 0 if unknown
    int hops;          // -1 unknown
};

bool posMine(int32_t &lat, int32_t &lon); // false: the badge has no position
// nodes with a shared position, nearest first; returns how many
int posList(NodePos *out, int max);
const char *posCompass(float bearing); // "N", "NE", ...

} // namespace badge

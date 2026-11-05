// What you've told the badge about particular devices: trusted (never alerts, still listed),
// a nickname, and the watchlist (alert when it turns up / when it's gone). Kept in
// /badge/devices.bin across restarts, by address (a device that changes its address, like a
// phone or an AirTag near its owner, is a new device to the badge).
#pragma once
#include <stdint.h>

namespace badge
{

enum DevFlags : uint8_t {
    DEV_TRUSTED = 1,    // known / yours: no alerts, no key flashing
    DEV_WATCH_HERE = 2, // alert when it turns up
    DEV_WATCH_GONE = 4, // alert when it's been gone a few minutes
    DEV_WIFI = 8,       // a WiFi network (else Bluetooth)
};

struct DevMemo {
    uint8_t mac[6];
    uint8_t flags;
    char nick[17];
};

const DevMemo *devFind(const uint8_t mac[6]);
bool devTrusted(const uint8_t mac[6]);
// a nickname if set, else the fallback (the device's own name)
const char *devName(const uint8_t mac[6], const char *fallback);
// set flags and nickname (nick nullptr: keep); forgets the device when nothing is left
void devSet(const uint8_t mac[6], uint8_t flags, const char *nick, bool wifi);
int devCount();
const DevMemo &devAt(int i);

} // namespace badge

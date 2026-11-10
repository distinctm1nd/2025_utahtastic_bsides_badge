// "Following you" check and alerts for the fox hunt's surveillance / police / camera /
// tracker devices, plus an optional background Bluetooth watch while the hunt isn't open.
//
// A device counts as with you once it has stayed in range for 10 minutes (gaps under 5
// minutes). Fixed devices around when it first showed up (registered-address speakers,
// TVs, access points) are the landmarks: when most of them are gone but it is still here,
// you moved and it came along.
#pragma once
#include "BadgeRadioScan.h"
#include "BadgeRules.h"

namespace badge
{

struct FollowInfo {
    uint32_t withYouMs; // time in range without a 5-minute gap
    uint32_t seenMs;    // millis() when last heard
    int8_t bestRssi;
    uint8_t refs, refsLeft; // landmarks when it showed up / still around now
    bool following;         // 10+ minutes with you
    bool moved;             // following, and you have moved since it showed up
    uint16_t seenBefore;    // earlier sightings in the log (30+ minutes apart)
    uint32_t agoS;          // seconds since the last of those (0 if unknown)
    int8_t trend;           // recent-vs-baseline RSSI, dB: >0 getting closer, <0 moving away
};

// tracking information for an alert device (false if it isn't tracked)
bool followGet(const uint8_t mac[6], FollowInfo &out);

// every ~2 s with a scan snapshot: tracks alert devices and returns an alert message for
// something new (or nullptr). canAlert: false while nothing can be shown (no message used up)
const char *watchStep(const ScanEntry *list, int n, bool wifi, bool canAlert);

bool watchAlerts(); // alert messages on / off (NVS, default on)
void watchSetAlerts(bool on);
// background watch while the fox hunt is closed: 0 off (default), 1 Bluetooth, 2 Bluetooth and
// a short WiFi sweep every 2 minutes
int watchBackground();
void watchSetBackground(int mode);
// light background WiFi attack sweeps (deauth / karma / evil-twin): a short WiFi sweep every ~2 min
// in the watch thread, logging + alerting new high-severity attacks. NVS, default off.
bool watchAttackScan();
void watchSetAttackScan(bool on);
// Append a threat to /badge/events.csv (per-type 60 s dedup). Returns true if it was newly recorded
// (a new type, or its detail changed, past the cooldown) so the caller can decide to alert. mac may
// be nullptr. Shared by the scan-stats app and the background watch so there is one log, no dupes.
bool threatRecord(const char *type, const char *detail, const uint8_t *mac, uint8_t chan, int8_t rssi);
// share one sighting with other badges over the mesh, only when the user asks (the hunt
// screen's 1 key). Returns what happened, for the screen.
const char *watchShareNow(const ScanEntry &e, bool wifi);
void watchPause(bool paused); // the fox hunt app owns the radio while it is open

// Alert settings (Fox hunt > Alert settings). Categories:
enum AlertCat {
    AC_TRACKERS = 1,
    AC_CAMERAS = 2,
    AC_SURVEILLANCE = 4,
    AC_POLICE = 8,
    AC_SKIMMERS = 16,
    AC_DRONES = 32,
    AC_ATTACKS = 64,
    AC_OTHER = 128,
    AC_ALL = 255,
};
int watchFollowMin(); // 5, 10 (default) or 20: in range this long counts as with you
void watchSetFollowMin(int minutes);
int watchAlertMinRssi(); // 0: any signal (default); else no alerts for devices weaker than this dBm
void watchSetAlertMinRssi(int dbm);
int watchAlertCats(); // AlertCat bits that may alert (all by default)
void watchSetAlertCats(int cats);
int watchQuietStart(); // quiet hours (no pop-ups; still logged): start hour, -1 off
int watchQuietEnd();
void watchSetQuiet(int startHour, int endHour);
bool watchQuietNow();
bool watchListFaint(); // the hunt lists show faint devices too (default on)
void watchSetListFaint(bool on);
int watchCategoryOf(const char *code); // a mark's AlertCat (0 none)
// may this device alert (not trusted, its category on, strong enough, not quiet hours)?
bool watchAlertAllowed(const ScanEntry &e, const RuleHit &h);

// what the watch is keeping track of now (for the flagged devices screen)
struct TrackedView {
    uint8_t mac[6];
    const char *code, *title;
    int8_t best;
    int8_t trend;              // recent-vs-baseline RSSI, dB: >0 getting closer
    uint32_t withYouMs, agoMs; // how long with you; since last heard
    bool following, moved, wifi;
};
int watchTrackedList(TrackedView *out, int max);
void watchStart();            // background thread (from lateInitVariant)
// marked devices (surveillance, trackers, cameras...) heard in the last 2 minutes, how many of
// them are trackers, and how many have been with you 10+ minutes
void watchSummary(int &near, int &trackers, int &following);

} // namespace badge

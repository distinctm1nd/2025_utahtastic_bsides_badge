// WiFi access points and Bluetooth LE devices around the badge, for the WiFi / Bluetooth
// fox hunt and the surveillance watch. Uses the radios as Meshtastic left them: WiFi is
// switched on only for the scan (and off again afterwards), Bluetooth scanning needs
// Meshtastic's Bluetooth to be on.
//
// WiFi: when the badge owns WiFi it listens to management frames itself (beacons, deauths),
// hopping channels, or staying on the channel of the network being hunted. With Meshtastic's
// WiFi connected it falls back to ordinary scans (no attack detection then).
// Bluetooth: also decodes drone Remote ID broadcasts and counts pairing pop-up spam.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace badge
{

enum ScanKind { SCAN_WIFI, SCAN_BLE, SCAN_ALL }; // ALL: takes turns, WiFi entries have channel > 0
static const uint16_t NO_COMPANY = 0xFFFF;

enum ScanFlags : uint8_t {
    SF_OPEN = 1,      // WiFi: no encryption
    SF_DRONE = 2,     // Bluetooth: drone Remote ID (radioDroneGet has the details)
    SF_MANYNAMES = 4, // WiFi: one radio beaconing several network names (karma / Pineapple style)
    SF_TWIN = 8,      // WiFi: same network name both open and protected (set in snapshots)
    SF_FMDN = 16,     // Bluetooth: a tracker on Google's Find My Device network (Android)
    SF_AWAY = 32,     // Bluetooth: a tracker that says it's away from its owner (Find My / FMDN)
    SF_FINDMY = 64,   // Bluetooth: an Apple Find My advert (AirTag and compatible, or a lost Apple device)
};

struct ScanEntry {
    uint8_t mac[6];       // BSSID or Bluetooth address, in the order it is printed
    char name[33];        // SSID or Bluetooth name, "" if hidden / not advertised
    int8_t rssi;          // dBm, last heard
    uint8_t channel;      // WiFi only
    uint16_t company;     // Bluetooth only: maker's company ID from the advert, NO_COMPANY if none
    bool publicAddr;      // a registered address, so its first 3 bytes name the maker
    int16_t mfgByte;      // Bluetooth: first byte after the company ID (-1 if none), e.g. Apple's message type
    uint8_t nServices;    // Bluetooth: 16-bit service UUIDs the advert lists
    uint16_t services[3];
    uint8_t flags;        // ScanFlags
    uint8_t names;        // WiFi: different network names heard from this radio
    uint32_t ssidHash;    // WiFi: the last one
    uint32_t seenMs;      // millis() when last heard
    uint32_t firstMs;     // millis() when first heard in this scan
};

struct DroneInfo {
    char id[21]; // serial number / registration (Basic ID)
    char operatorId[21];
    bool hasPos, hasOperator;
    int32_t lat, lon; // 1e-7 degrees
    int32_t opLat, opLon;
    float altM, heightM, speedMs;
    uint16_t heading;
};

// nullptr when scanning started, otherwise why not (a short text for the screen).
// light: a lower Bluetooth duty cycle for the background watch
const char *radioScanStart(ScanKind kind, bool light = false);
void radioScanStop();
bool radioScanRunning(ScanKind *kind = nullptr);
void radioScanPoll(); // often (app tick): hops WiFi channels / restarts scans
void radioScanHold(bool hold); // pause the Bluetooth scan while the radio talks to one device
// follow one device (WiFi then listens only on its channel, so readings come quicker); nullptr: all
void radioScanFocus(const uint8_t *mac, uint8_t channel);
// devices heard in the last 30 s, strongest first
int radioScanSnapshot(ScanEntry *out, int max);
bool radioScanFind(const uint8_t *mac, ScanEntry &out);
bool radioDroneGet(const uint8_t *mac, DroneInfo &out);

// attacks: deauth / disassociation frames heard in the last 10 s (and the channel of the
// last one), and distinct addresses sending pairing pop-up adverts in the last 10 s
int radioDeauthRecent(uint8_t *channel);
int radioPopupSpamRecent();

} // namespace badge

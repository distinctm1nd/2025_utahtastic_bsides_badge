#include "configuration.h"

#include "BadgeRadioScan.h"

#include "BadgeCapture.h"

#include "NodeDB.h"
#include "mesh/Throttle.h"
#include "mesh/wifi/WiFiAPClient.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <algorithm>
#include <esp_heap_caps.h>
#include <esp_wifi.h>
#include <string.h>

namespace badge
{

static const int MAX_ENTRIES = 40;
static const uint32_t STALE_MS = 30000;
static const uint32_t WIFI_MIN_HEAP = 60000; // the WiFi driver needs roughly this much to start
static const uint32_t HOP_MS = 220;          // per WiFi channel (beacons come every ~100 ms)
static const uint32_t ATTACK_WINDOW_MS = 10000;

static ScanEntry table[MAX_ENTRIES];
static int used = 0;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED; // results arrive on the NimBLE / WiFi tasks

// raw frames queued by the WiFi / BLE callbacks, drained to the pcap files from radioScanPoll
// (flash writes must not happen in those callbacks). kind 0 = WiFi 802.11, 1 = BLE advert.
struct CapEntry {
    uint8_t kind, chan, advType, rnd, mac[6];
    uint16_t len;
    uint8_t data[256];
};
static const int CAP_RING = 8;
static CapEntry *capRing = nullptr;
static volatile int capHead = 0, capTail = 0;

static void capPush(uint8_t kind, const uint8_t *data, int len, uint8_t chan, const uint8_t mac[6], uint8_t advType, bool rnd)
{
    if (!capRing || len <= 0)
        return;
    if (len > 256)
        len = 256;
    portENTER_CRITICAL(&lock);
    int next = (capHead + 1) % CAP_RING;
    if (next != capTail) {
        CapEntry &e = capRing[capHead];
        e.kind = kind;
        e.chan = chan;
        e.advType = advType;
        e.rnd = rnd;
        e.len = len;
        if (mac)
            memcpy(e.mac, mac, 6);
        memcpy(e.data, data, len);
        capHead = next;
    }
    portEXIT_CRITICAL(&lock);
}

static bool capWanted()
{
    return captureWifiOn() || captureBleOn() || captureStreamKind() == STREAM_BLE;
}

void radioScanDrainCapture()
{
    if (!capRing)
        return;
    for (int guard = 0; guard < CAP_RING; guard++) {
        CapEntry e;
        portENTER_CRITICAL(&lock);
        if (capTail == capHead) {
            portEXIT_CRITICAL(&lock);
            break;
        }
        e = capRing[capTail];
        capTail = (capTail + 1) % CAP_RING;
        portEXIT_CRITICAL(&lock);
        if (e.kind == 0)
            captureWifiFrame(e.data, e.len, e.chan);
        else
            captureBleAdv(e.mac, e.rnd, e.advType, e.data, (uint8_t)e.len);
    }
}

static bool running = false;
static ScanKind active = SCAN_WIFI;
static bool ownWifi = false;  // we switched WiFi on, so we switch it off again
static bool sniffing = false; // listening to WiFi frames ourselves (needs ownWifi)
static bool focused = false;
static uint8_t focusMac[6];
static uint8_t focusChan = 0, hopChan = 1;
static uint32_t lastStart = 0, startGap = 0, lastHop = 0;

struct Extra {
    int16_t mfgByte;
    uint8_t nServices;
    uint16_t services[3];
    uint8_t flags;
    uint32_t ssidHash;
};

static uint32_t hash32(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++)
        h = (h ^ p[i]) * 16777619u;
    return h | 1;
}

static void record(const uint8_t *mac, const char *name, int rssi, uint8_t chan, uint16_t company, bool publicAddr,
                   const Extra *x)
{
    uint32_t now = millis();
    portENTER_CRITICAL(&lock);
    int slot = -1, oldest = 0;
    for (int i = 0; i < used; i++) {
        if (memcmp(table[i].mac, mac, 6) == 0) {
            slot = i;
            break;
        }
        if (now - table[i].seenMs > now - table[oldest].seenMs)
            oldest = i;
    }
    if (slot < 0) {
        slot = used < MAX_ENTRIES ? used++ : oldest;
        ScanEntry &n = table[slot];
        memset(&n, 0, sizeof(n));
        memcpy(n.mac, mac, 6);
        n.company = NO_COMPANY;
        n.mfgByte = -1;
        n.firstMs = millis();
    }
    ScanEntry &e = table[slot];
    if (name && *name) { // keep a name we heard before when this advert has none
        strncpy(e.name, name, sizeof(e.name) - 1);
        e.name[sizeof(e.name) - 1] = 0;
    }
    if (company != NO_COMPANY) // like the name, not every advert carries it
        e.company = company;
    e.publicAddr = publicAddr;
    if (x) {
        if (x->mfgByte >= 0)
            e.mfgByte = x->mfgByte;
        if (x->nServices) {
            e.nServices = x->nServices;
            memcpy(e.services, x->services, sizeof(e.services));
        }
        e.flags = (e.flags & ~SF_OPEN) | x->flags;
        if (x->ssidHash && x->ssidHash != e.ssidHash) {
            if (e.names < 255)
                e.names++;
            e.ssidHash = x->ssidHash;
        }
        if (e.names >= 3)
            e.flags |= SF_MANYNAMES;
    }
    e.rssi = (int8_t)rssi;
    e.channel = chan;
    e.seenMs = now;
    portEXIT_CRITICAL(&lock);
}

// ---- attack counters ----------------------------------------------------------------------
struct Hit {
    uint32_t id, ms;
};
static Hit deauths[64], popups[64];
static int deauthHead = 0, popupHead = 0;
static uint8_t deauthChan = 0;

static void addHit(Hit *ring, int &head, uint32_t id, bool distinct)
{
    uint32_t now = millis();
    if (distinct)
        for (int i = 0; i < 64; i++)
            if (ring[i].id == id && now - ring[i].ms < ATTACK_WINDOW_MS) {
                ring[i].ms = now;
                return;
            }
    ring[head] = {id, now};
    head = (head + 1) % 64;
}

static int countHits(const Hit *ring)
{
    int n = 0;
    portENTER_CRITICAL(&lock);
    for (int i = 0; i < 64; i++)
        n += ring[i].id && Throttle::isWithinTimespanMs(ring[i].ms, ATTACK_WINDOW_MS);
    portEXIT_CRITICAL(&lock);
    return n;
}

int radioDeauthRecent(uint8_t *channel)
{
    if (channel)
        *channel = deauthChan;
    return countHits(deauths);
}

int radioPopupSpamRecent()
{
    return countHits(popups);
}

// ---- Bluetooth -----------------------------------------------------------------------------
static const int MAX_DRONES = 8;
struct DroneSlot {
    uint8_t mac[6];
    uint32_t seenMs;
    DroneInfo info;
};
static DroneSlot drones[MAX_DRONES];

static int32_t le32s(const uint8_t *p)
{
    return (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24));
}

// one 25-byte ASTM F3411 (Open Drone ID) message
static void droneMessage(DroneInfo &d, const uint8_t *m)
{
    switch (m[0] >> 4) {
    case 0x0: // Basic ID
        memcpy(d.id, m + 2, 20);
        d.id[20] = 0;
        break;
    case 0x1: { // Location
        uint8_t st = m[1];
        d.heading = m[2] + ((st & 0x02) ? 180 : 0);
        d.speedMs = (st & 0x01) ? m[3] * 0.75f + 255 * 0.25f : m[3] * 0.25f;
        d.lat = le32s(m + 5);
        d.lon = le32s(m + 9);
        d.altM = (m[15] | (m[16] << 8)) * 0.5f - 1000; // geodetic
        d.heightM = (m[17] | (m[18] << 8)) * 0.5f - 1000;
        d.hasPos = d.lat || d.lon;
        break;
    }
    case 0x4: // System: where the operator is
        d.opLat = le32s(m + 2);
        d.opLon = le32s(m + 6);
        d.hasOperator = d.opLat || d.opLon;
        break;
    case 0x5: // Operator ID
        memcpy(d.operatorId, m + 2, 20);
        d.operatorId[20] = 0;
        break;
    }
}

// app code 0x0D, counter, then a message (or a pack of them): Bluetooth service data 0xFFFA,
// and the WiFi beacon's vendor element (ASTM OUI FA:0B:BC) after its OUI
static bool droneAdvert(const uint8_t *mac, const uint8_t *raw, size_t size)
{
    if (size < 2 + 25 || raw[0] != 0x0D)
        return false;
    const uint8_t *m = raw + 2;
    portENTER_CRITICAL(&lock);
    DroneSlot *s = nullptr;
    for (auto &c : drones)
        if (memcmp(c.mac, mac, 6) == 0) {
            s = &c;
            break;
        }
    if (!s) { // a free slot, else the oldest
        s = &drones[0];
        for (auto &c : drones)
            if (!c.seenMs || millis() - c.seenMs > millis() - s->seenMs)
                s = &c;
        memset(s, 0, sizeof(*s));
        memcpy(s->mac, mac, 6);
    }
    s->seenMs = millis();
    if ((m[0] >> 4) == 0xF) { // message pack (Bluetooth 5, WiFi)
        int msgSize = m[1], count = m[2];
        for (int i = 0; i < count && msgSize == 25 && 2 + 3 + (i + 1) * 25 <= (int)size; i++)
            droneMessage(s->info, m + 3 + i * 25);
    } else {
        droneMessage(s->info, m);
    }
    portEXIT_CRITICAL(&lock);
    return true;
}

bool radioDroneGet(const uint8_t *mac, DroneInfo &out)
{
    bool found = false;
    portENTER_CRITICAL(&lock);
    for (const auto &c : drones)
        if (c.seenMs && memcmp(c.mac, mac, 6) == 0) {
            out = c.info;
            found = true;
        }
    portEXIT_CRITICAL(&lock);
    return found;
}

class BleResults : public NimBLEScanCallbacks
{
    void onResult(const NimBLEAdvertisedDevice *d) override
    {
        std::string n = d->haveName() ? d->getName() : std::string();
        NimBLEAddress a = d->getAddress();
        const uint8_t *le = a.getVal(); // little-endian
        uint8_t mac[6];
        for (int i = 0; i < 6; i++)
            mac[i] = le[5 - i];
        uint16_t company = NO_COMPANY;
        std::string m;
        if (d->haveManufacturerData()) {
            m = d->getManufacturerData();
            if (m.size() >= 2)
                company = (uint8_t)m[0] | ((uint8_t)m[1] << 8);
        }
        Extra x = {};
        x.mfgByte = m.size() >= 3 ? (uint8_t)m[2] : -1;
        // Apple Find My: type 0x12 with length 0x19 is the full "offline finding" advert an AirTag
        // sends once it has been away from its owner's devices for a while; 0x02 is the short
        // "nearby" one it sends while they're around
        if (company == 0x004C && x.mfgByte == 0x12) {
            x.flags |= SF_FINDMY;
            if (m.size() >= 4 && (uint8_t)m[3] == 0x19)
                x.flags |= SF_AWAY;
        }
        bool popup = (company == 0x004C && (x.mfgByte == 0x07 || x.mfgByte == 0x0F)) || // Apple pairing / nearby action
                     (company == 0x0006 && x.mfgByte == 0x03);                          // Microsoft Swift Pair
        for (uint8_t i = 0; i < d->getServiceUUIDCount(); i++) {
            NimBLEUUID u = d->getServiceUUID(i);
            if (x.nServices < 3 && u.bitSize() == 16)
                x.services[x.nServices++] = (uint16_t)(u.getValue()[0] | (u.getValue()[1] << 8));
        }
        for (uint8_t i = 0; i < d->getServiceDataCount(); i++) {
            NimBLEUUID u = d->getServiceDataUUID(i);
            if (u.bitSize() != 16)
                continue;
            uint16_t v = (uint16_t)(u.getValue()[0] | (u.getValue()[1] << 8));
            if (x.nServices < 3)
                x.services[x.nServices++] = v;
            if (v == 0xFFFA) {
                std::string sd = d->getServiceData(i);
                if (droneAdvert(mac, (const uint8_t *)sd.data(), sd.size()))
                    x.flags |= SF_DRONE;
            }
            if (v == 0xFE2C) // Google Fast Pair
                popup = true;
            if (v == 0xFEAA) { // Eddystone; frame 0x40 / 0x41 = Find My Device network tracker
                std::string sd = d->getServiceData(i);
                uint8_t frame = sd.empty() ? 0 : (uint8_t)sd[0];
                if (frame == 0x40 || frame == 0x41)
                    x.flags |= SF_FMDN;
                if (frame == 0x41) // "unwanted tracking protection": away from its owner
                    x.flags |= SF_AWAY;
            }
        }
        if (popup) { // spam tools send these from a new random address every time
            portENTER_CRITICAL(&lock);
            addHit(popups, popupHead, hash32(mac, 6), true);
            portEXIT_CRITICAL(&lock);
        }
        if (captureBleOn() || captureStreamKind() == STREAM_BLE)
            capPush(1, d->getPayload().data(), (int)d->getPayload().size(), 0, mac, d->getAdvType(),
                    a.getType() != BLE_ADDR_PUBLIC);
        record(mac, n.c_str(), d->getRSSI(), 0, company, a.getType() == BLE_ADDR_PUBLIC, &x);
    }
};
static BleResults bleResults;

// ---- WiFi frames ---------------------------------------------------------------------------
// NAN service discovery frame (ASTM F3411): public action, Wi-Fi Alliance NAN, then attributes;
// the Open Drone ID service carries [counter][message pack] as its service info
static void nanFrame(const uint8_t *p, int len, int rssi, uint8_t chan)
{
    static const uint8_t ODID_SERVICE[6] = {0x88, 0x69, 0x19, 0x9D, 0x92, 0x09};
    if (len < 30 || p[24] != 0x04 || p[25] != 0x09 || p[26] != 0x50 || p[27] != 0x6F || p[28] != 0x9A || p[29] != 0x13)
        return;
    const uint8_t *src = p + 10;
    for (int i = 30; i + 3 <= len;) {
        int alen = p[i + 1] | (p[i + 2] << 8);
        if (i + 3 + alen > len)
            return;
        const uint8_t *a = p + i + 3;
        if (p[i] == 0x03 && alen >= 10 && memcmp(a, ODID_SERVICE, 6) == 0) {
            int infoLen = a[9];
            if (10 + infoLen > alen)
                return;
            uint8_t raw[1 + 255];
            raw[0] = 0x0D; // what the Bluetooth / beacon decoder expects first
            memcpy(raw + 1, a + 10, infoLen);
            if (!droneAdvert(src, raw, 1 + infoLen))
                return;
            DroneInfo info;
            char name[33] = "Remote ID drone";
            if (radioDroneGet(src, info) && info.id[0])
                snprintf(name, sizeof(name), "%s", info.id);
            Extra x = {};
            x.mfgByte = -1;
            x.flags = SF_DRONE;
            record(src, name, rssi, chan ? chan : 6, NO_COMPANY, !(src[0] & 0x02), &x);
            return;
        }
        i += 3 + alen;
    }
}

static void IRAM_ATTR wifiFrame(void *buf, wifi_promiscuous_pkt_type_t type)
{
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    const uint8_t *p = pkt->payload;
    int len = pkt->rx_ctrl.sig_len - 4; // without the FCS
    if (len < 10)
        return;
    if (captureWifiOn())
        capPush(0, p, len, pkt->rx_ctrl.channel, nullptr, 0, false);
    if (type != WIFI_PKT_MGMT || len < 24)
        return;
    uint8_t subtype = p[0] >> 4;
    if (subtype == 0xC || subtype == 0xA) { // deauthentication / disassociation
        portENTER_CRITICAL_ISR(&lock);
        addHit(deauths, deauthHead, millis(), false);
        deauthChan = pkt->rx_ctrl.channel;
        portEXIT_CRITICAL_ISR(&lock);
        return;
    }
    if (subtype == 0xD) { // action frame: Remote ID over WiFi NAN
        nanFrame(p, len, pkt->rx_ctrl.rssi, pkt->rx_ctrl.channel);
        return;
    }
    if ((subtype != 0x8 && subtype != 0x5) || len < 36) // beacon / probe response
        return;
    const uint8_t *bssid = p + 16;
    uint16_t capability = p[34] | (p[35] << 8);
    char ssid[33] = "";
    uint8_t chan = pkt->rx_ctrl.channel;
    bool drone = false;
    for (int i = 36; i + 2 <= len;) {
        uint8_t id = p[i], l = p[i + 1];
        if (i + 2 + l > len)
            break;
        if (id == 0 && l <= 32) {
            memcpy(ssid, p + i + 2, l);
            ssid[l] = 0;
        } else if (id == 3 && l == 1) {
            chan = p[i + 2];
        } else if (id == 221 && l > 3 + 2 + 25 && p[i + 2] == 0xFA && p[i + 3] == 0x0B && p[i + 4] == 0xBC) {
            drone = droneAdvert(bssid, p + i + 5, l - 3); // Remote ID over WiFi beacons
        }
        i += 2 + l;
    }
    if (!ssid[0] || (unsigned char)ssid[0] == 0) // hidden network
        ssid[0] = 0;
    Extra x = {};
    x.mfgByte = -1;
    x.flags = ((capability & 0x0010) ? 0 : SF_OPEN) | (drone ? SF_DRONE : 0);
    x.ssidHash = ssid[0] ? hash32((const uint8_t *)ssid, strlen(ssid)) : 0;
    record(bssid, ssid, pkt->rx_ctrl.rssi, chan, NO_COMPANY, !(bssid[0] & 0x02), &x);
}

static void sniffStart()
{
    wifi_promiscuous_filter_t f = {};
    f.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | (captureWifiOn() ? WIFI_PROMIS_FILTER_MASK_DATA : 0);
    esp_wifi_set_promiscuous_filter(&f);
    esp_wifi_set_promiscuous_rx_cb(wifiFrame);
    sniffing = esp_wifi_set_promiscuous(true) == ESP_OK;
    hopChan = 1;
    lastHop = 0;
}

// ---- start / stop / poll -------------------------------------------------------------------
static bool allBle = false, allWifi = false, phaseBle = false; // SCAN_ALL: which halves work, whose turn
static int hops = 0;

static const char *bleBegin(bool light)
{
    if (!NimBLEDevice::isInitialized())
        return isWifiAvailable() ? "Bluetooth is off\nwhile WiFi is on" : "Bluetooth is off\n(Mesh settings)";
    NimBLEScan *s = NimBLEDevice::getScan();
    s->setScanCallbacks(&bleResults, true);
    s->setMaxResults(0); // callbacks only, don't keep a list
    s->setActiveScan(true);
    s->setInterval(light ? 300 : 100);
    s->setWindow(light ? 30 : 60); // leave air time for Meshtastic's own Bluetooth link
    return nullptr;
}

static void bleEnd()
{
    if (!NimBLEDevice::isInitialized())
        return;
    NimBLEScan *s = NimBLEDevice::getScan();
    s->stop();
    s->setScanCallbacks(nullptr);
    s->setMaxResults(0xFF);
    s->clearResults();
}

static const char *wifiBegin()
{
    ownWifi = WiFi.getMode() == WIFI_OFF;
    if (ownWifi) {
        if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < WIFI_MIN_HEAP) { // the driver can't use PSRAM
            ownWifi = false;
            return "Not enough memory\nfor WiFi";
        }
        if (!WiFi.mode(WIFI_STA)) {
            WiFi.mode(WIFI_OFF);
            ownWifi = false;
            return "WiFi didn't start";
        }
        sniffStart();
    }
    WiFi.scanDelete();
    return nullptr;
}

static void wifiEnd()
{
    if (sniffing) {
        esp_wifi_set_promiscuous(false);
        esp_wifi_set_promiscuous_rx_cb(nullptr);
        sniffing = false;
    }
    WiFi.scanDelete();
    if (ownWifi)
        WiFi.mode(WIFI_OFF);
    ownWifi = false;
}

const char *radioScanStart(ScanKind kind, bool light)
{
    radioScanStop();
    if (capWanted() && !capRing)
        capRing = (CapEntry *)heap_caps_malloc(sizeof(CapEntry) * CAP_RING, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    capHead = capTail = 0;
    portENTER_CRITICAL(&lock);
    used = 0;
    portEXIT_CRITICAL(&lock);
    focused = false;
    lastStart = 0;
    startGap = 0;
    if (moduleConfig.paxcounter.enabled)
        return "Paxcounter is using\nWiFi and Bluetooth";

    if (kind == SCAN_BLE) {
        if (const char *why = bleBegin(light))
            return why;
    } else if (kind == SCAN_WIFI) {
        if (const char *why = wifiBegin())
            return why;
    } else {
        // Bluetooth first: WiFi may then still fit in memory, and either half alone is still useful
        const char *whyBle = bleBegin(light);
        allBle = !whyBle;
        const char *whyWifi = wifiBegin();
        allWifi = !whyWifi;
        if (allWifi && allBle && !sniffing) { // Meshtastic's WiFi link: scans only, no turns
            allBle = false;
            bleEnd();
        }
        if (!allBle && !allWifi)
            return whyWifi;
        phaseBle = !allWifi;
        hops = 0;
        if (sniffing && phaseBle)
            esp_wifi_set_promiscuous(false);
    }
    active = kind;
    running = true;
    return nullptr;
}

void radioScanStop()
{
    if (!running)
        return;
    running = false;
    if (active == SCAN_BLE || (active == SCAN_ALL && allBle))
        bleEnd();
    if (active == SCAN_WIFI || (active == SCAN_ALL && allWifi))
        wifiEnd();
    allBle = allWifi = false;
}

bool radioScanRunning(ScanKind *kind)
{
    if (kind)
        *kind = active;
    return running;
}

static volatile bool held = false; // the radio is lent to something else (a beep request)
void radioScanHold(bool hold)
{
    held = hold;
    if (hold && running && (active == SCAN_BLE || allBle) && NimBLEDevice::isInitialized())
        NimBLEDevice::getScan()->stop();
}

void radioScanPoll()
{
    if (capWanted())
        radioScanDrainCapture();
    if (!running || held)
        return;
    if (active == SCAN_ALL && allBle && allWifi) { // turns: one 3 s Bluetooth scan, one sweep of the channels
        if (phaseBle) {
            if (!NimBLEDevice::isInitialized())
                return;
            NimBLEScan *s = NimBLEDevice::getScan();
            if (s->isScanning())
                return;
            if (!lastStart) {
                lastStart = millis();
                s->start(3000, false); // 2.x: duration is ms; scan-end handled by the callbacks
                return;
            }
            phaseBle = false;
            hops = 0;
            lastHop = 0;
            esp_wifi_set_promiscuous(true);
            return;
        }
        if (hops >= 13 && !Throttle::isWithinTimespanMs(lastHop, HOP_MS)) {
            esp_wifi_set_promiscuous(false);
            phaseBle = true;
            lastStart = 0;
            return;
        }
    }
    if (active == SCAN_BLE || (active == SCAN_ALL && !allWifi)) {
        if (!NimBLEDevice::isInitialized())
            return;
        NimBLEScan *s = NimBLEDevice::getScan();
        // 3 s scans: each end clears devices whose scan response never came
        if (!s->isScanning() && !Throttle::isWithinTimespanMs(lastStart, 500)) {
            lastStart = millis();
            s->start(3000, false); // 2.x: duration is ms; scan-end handled by the callbacks
        }
        return;
    }

    if (sniffing) { // hop channels, or stay with the network being hunted
        if (Throttle::isWithinTimespanMs(lastHop, HOP_MS))
            return;
        lastHop = millis();
        uint8_t ch = focused ? focusChan : hopChan;
        if (!focused)
            hopChan = hopChan % 13 + 1;
        hops++;
        if (ch >= 1 && ch <= 13)
            esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        return;
    }

    int16_t n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING)
        return;
    if (n >= 0) {
        for (int i = 0; i < n; i++) {
            const uint8_t *b = WiFi.BSSID(i);
            Extra x = {};
            x.mfgByte = -1;
            x.flags = WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? SF_OPEN : 0;
            String ssid = WiFi.SSID(i);
            x.ssidHash = ssid.length() ? hash32((const uint8_t *)ssid.c_str(), ssid.length()) : 0;
            // locally administered (randomised, e.g. phone hotspots) addresses have bit 1 set
            record(b, ssid.c_str(), WiFi.RSSI(i), (uint8_t)WiFi.channel(i), NO_COMPANY, !(b[0] & 0x02), &x);
        }
        WiFi.scanDelete();
        lastStart = millis();
        startGap = ownWifi ? 0 : 3000; // Meshtastic's WiFi link is up: don't hog the radio
    }
    if (Throttle::isWithinTimespanMs(lastStart, startGap))
        return;
    lastStart = millis();
    startGap = 1000; // if this scan doesn't run, try again in a second
    if (focused)
        WiFi.scanNetworks(true, true, false, 100, focusChan, nullptr, focusMac);
    else
        WiFi.scanNetworks(true, true, false, 120);
}

void radioScanFocus(const uint8_t *mac, uint8_t channel)
{
    focused = mac != nullptr;
    if (mac)
        memcpy(focusMac, mac, 6);
    focusChan = channel;
    lastHop = 0;
}

int radioScanSnapshot(ScanEntry *out, int max)
{
    int n = 0;
    portENTER_CRITICAL(&lock);
    for (int i = 0; i < used && n < max; i++)
        if (Throttle::isWithinTimespanMs(table[i].seenMs, STALE_MS))
            out[n++] = table[i];
    portEXIT_CRITICAL(&lock);
    // evil twin: the same network name both open and protected
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (out[i].ssidHash && out[i].ssidHash == out[j].ssidHash && ((out[i].flags ^ out[j].flags) & SF_OPEN)) {
                out[i].flags |= SF_TWIN;
                out[j].flags |= SF_TWIN;
            }
    std::sort(out, out + n, [](const ScanEntry &a, const ScanEntry &b) { return a.rssi > b.rssi; });
    return n;
}

bool radioScanFind(const uint8_t *mac, ScanEntry &out)
{
    bool found = false;
    portENTER_CRITICAL(&lock);
    for (int i = 0; i < used; i++)
        if (memcmp(table[i].mac, mac, 6) == 0) {
            out = table[i];
            found = true;
            break;
        }
    portEXIT_CRITICAL(&lock);
    return found;
}

} // namespace badge

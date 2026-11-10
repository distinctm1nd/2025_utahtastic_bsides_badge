// Scan stats: a glanceable summary of what's on the air around the badge, so you don't need a
// laptop. One radio, so it shows WiFi or Bluetooth at a time (# / left-right switches). Numbers
// are "around you now" (what the scan has heard in the last ~30 s), not an all-time total.
//
//   # (or left / right) WiFi <-> Bluetooth, * done
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeDevices.h" // devTrusted(): is a deauth aimed at one of your devices?
#include "BadgeFollow.h"  // threatRecord(): shared /badge/events.csv threat log
#include "BadgeLeds.h"   // ledsFlourish() as the no-buzzer alert for a new threat
#include "BadgeRadioScan.h"
#include "BadgeRules.h"
#include "BadgeVendors.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "mesh/Throttle.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace badge
{

static const int MAXLIST = 80;

static void fitText(OLEDDisplay *d, char *s, int w)
{
    d->setFont(FONT_SMALL);
    if (d->getStringWidth(s) <= w)
        return;
    int n = strlen(s);
    while (n > 1 && d->getStringWidth(s, n) > w)
        n--;
    s[n] = 0;
}

class ScanStatsApp : public App
{
  public:
    bool numberShortcuts() const override { return true; }
    const char *name() const override { return "Scan stats"; }
    uint32_t frameMs() const override { return 1000; }
    bool keepAwake() const override { return true; }
    static ScanKind kindFor(int pg) { return pg == 0 ? SCAN_WIFI : pg == 1 ? SCAN_BLE : SCAN_ALL; }
    void begin() override
    {
        page = 0;
        error = radioScanStart(kindFor(page));
    }
    void end() override { radioScanStop(); }
    void tick(uint32_t) override
    {
        radioScanPoll();
        logAndAlert(); // record/flash new threats whenever the stats app is open
    }
    void key(int k, KeyAction a) override
    {
        if (k == AK_STAR && a == KA_RELEASE) {
            appClose();
            return;
        }
        if (a != KA_PRESS)
            return;
        if (page == 2 && k == AK_0) {
            trustTopThreat(); // Threats page: trust (allowlist) the top offender so it stops alerting
            appRedraw();
            return;
        }
        if (k == AK_HASH || k == AK_5 || k == AK_LEFT || k == AK_4 || k == AK_6 || k == AK_RIGHT) {
            page = (k == AK_LEFT || k == AK_4) ? (page + 2) % 3 : (page + 1) % 3; // WiFi -> BLE -> Threats
            radioScanStop();
            error = radioScanStart(kindFor(page));
        }
        appRedraw();
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        int lh = fontHeight(FONT_SMALL);
        const char *title = page == 0 ? "Scan stats: WiFi" : page == 1 ? "Scan stats: Bluetooth" : "Threats (live)";
        drawTitle(d, x, y, title, "#swap *done");
        if (error) {
            drawCentered(d, x, y + 2 * lh, error, FONT_SMALL);
            return;
        }
        int n = radioScanSnapshot(list, MAXLIST);
        d->setFont(FONT_SMALL);
        d->setTextAlignment(TEXT_ALIGN_LEFT);
        int ry = y + lh + 2;
        char b[48];
        if (page == 0)
            drawWifi(d, x, ry, lh, list, n, b, sizeof(b));
        else if (page == 1)
            drawBle(d, x, ry, lh, list, n, b, sizeof(b));
        else
            drawThreats(d, x, ry, lh, list, n, b, sizeof(b));
    }

  private:
    int page = 0; // 0 WiFi, 1 Bluetooth, 2 Threats
    const char *error = nullptr;
    ScanEntry list[MAXLIST];

    // a small running tally of maker names, to show the busiest few
    struct Vt {
        const char *name;
        int n;
    };
    void vAdd(Vt *v, int &vn, const char *name)
    {
        if (!name || !name[0])
            return;
        for (int i = 0; i < vn; i++)
            if (!strcmp(v[i].name, name)) {
                v[i].n++;
                return;
            }
        if (vn < 24)
            v[vn++] = {name, 1};
    }
    void topVendors(Vt *v, int vn, char *out, size_t n)
    {
        out[0] = 0;
        for (int pick = 0; pick < 3; pick++) {
            int best = -1;
            for (int i = 0; i < vn; i++)
                if (v[i].n > 0 && (best < 0 || v[i].n > v[best].n))
                    best = i;
            if (best < 0)
                break;
            char frag[24];
            snprintf(frag, sizeof(frag), "%s%s %d", out[0] ? ", " : "", v[best].name, v[best].n);
            strncat(out, frag, n - strlen(out) - 1);
            v[best].n = 0;
        }
    }

    void drawWifi(OLEDDisplay *d, int16_t x, int ry, int lh, const ScanEntry *list, int n, char *b, size_t nb)
    {
        int open = 0, hidden = 0, twin = 0, drone = 0, karma = 0, alert = 0;
        int chan[14] = {0};
        Vt v[24];
        int vn = 0;
        for (int i = 0; i < n; i++) {
            const ScanEntry &e = list[i];
            if (e.channel == 0)
                continue; // (shouldn't happen in a WiFi scan)
            if (e.flags & SF_OPEN)
                open++;
            if (!e.name[0])
                hidden++;
            if (e.flags & SF_TWIN)
                twin++;
            if (e.flags & SF_DRONE)
                drone++;
            if (e.flags & SF_MANYNAMES)
                karma++;
            if (e.channel >= 1 && e.channel <= 13)
                chan[e.channel]++;
            RuleHit h;
            if (ruleMatch(e, true, h) && h.alert)
                alert++;
            vAdd(v, vn, vendorFromAddress(e.mac));
        }
        int busy = 1, busyN = 0;
        for (int c = 1; c <= 13; c++)
            if (chan[c] > busyN)
                busyN = chan[c], busy = c;
        uint8_t dchan = 0;
        int deauth = radioDeauthRecent(&dchan);

        snprintf(b, nb, "Networks: %d", n);
        d->drawString(x + 2, ry, b);
        snprintf(b, nb, "Open %d   Hidden %d", open, hidden);
        d->drawString(x + 2, ry + lh, b);
        snprintf(b, nb, "Busiest ch %d (%d)", busy, busyN);
        d->drawString(x + 2, ry + 2 * lh, b);
        static const int FLOOD_NAMES = 6; // at/above this, the lean flips from targeted karma to spam flood
        uint8_t kmac[6] = {0};
        int kworst = radioKarmaWorst(kmac, nullptr); // worst offender's name count (0 if none)
        snprintf(b, nb, "Twins %d Drn %d Karma %s%d", twin, drone, kworst >= FLOOD_NAMES ? "!" : "", karma);
        d->drawString(x + 2, ry + 3 * lh, b);
        uint8_t dmac[6] = {0};
        if (deauth && radioDeauthTarget(dmac))
            // identify the spoofed/attacked AP (addr3 of the deauth), not just a count
            snprintf(b, nb, "! Deauth %02X:%02X ch%d x%d", dmac[4], dmac[5], dchan, deauth);
        else if (deauth)
            snprintf(b, nb, "! Deauths %d (ch %d, 10s)", deauth, dchan);
        else
            snprintf(b, nb, "Deauths: none");
        fitText(d, b, d->width() - 4);
        d->drawString(x + 2, ry + 4 * lh, b);
        snprintf(b, nb, "Flagged %d   Pwn %d", alert, radioPwnagotchiRecent());
        d->drawString(x + 2, ry + 5 * lh, b);
        if (kworst >= 3) {
            // One radio is faking many SSIDs; the signature is identical for beacon spam and a real
            // karma rig, so lean on two hints (the "?" keeps it a guess): what the faked names look
            // like (junk = scattershot spam, plausible = targeted evil-twin/karma) and, as a
            // fallback, the count. Shows the worst offender's last two MAC bytes to identify it.
            const ScanEntry *ke = nullptr;
            for (int i = 0; i < n; i++)
                if (list[i].channel && memcmp(list[i].mac, kmac, 6) == 0) {
                    ke = &list[i];
                    break;
                }
            const char *lean;
            if (ke && ke->names)
                lean = ke->junkNames * 2 >= ke->names ? "spam?" : "karma?"; // mostly-junk names => spam
            else
                lean = kworst >= FLOOD_NAMES ? "spam?" : "karma?"; // no name sample: fall back to count
            snprintf(b, nb, "! %s %02X:%02X x%d", lean, kmac[4], kmac[5], kworst);
        } else {
            char top[64];
            topVendors(v, vn, top, sizeof(top));
            snprintf(b, nb, "Top: %s", top[0] ? top : "-");
        }
        fitText(d, b, d->width() - 4);
        d->drawString(x + 2, ry + 6 * lh, b);
    }

    void drawBle(OLEDDisplay *d, int16_t x, int ry, int lh, const ScanEntry *list, int n, char *b, size_t nb)
    {
        // A separated ("away") AirTag slows its MAC rotation to ~24 h so a victim's phone can tell it
        // is the SAME tag following them; a with-owner tag rotates ~15 min. So a long dwell on one MAC
        // means different things: an AWAY tag that stays with you is the stalking signal (follow),
        // while a non-away tag static far past its rotation leans like a lazy spoof (fmStatic).
        static const uint32_t PERSIST_MS = 20 * 60 * 1000; // non-away tag static longer than this: suspect
        static const uint32_t FOLLOW_MS = 6 * 60 * 1000;   // away tag near you at least this long: following
        uint32_t now = millis();
        int pub = 0, named = 0, trackers = 0, findmy = 0, away = 0, alert = 0, fmStatic = 0, hid = 0;
        uint32_t followMs = 0;        // longest dwell among away trackers
        const uint8_t *followMac = nullptr;
        Vt v[24];
        int vn = 0;
        for (int i = 0; i < n; i++) {
            const ScanEntry &e = list[i];
            if (e.channel != 0)
                continue;
            if (e.publicAddr)
                pub++;
            if (e.name[0])
                named++;
            if (e.flags & (SF_FINDMY | SF_FMDN)) {
                findmy++;
                uint32_t dwell = (uint32_t)(now - e.firstMs);
                if (e.flags & SF_AWAY) {
                    if (dwell > followMs) { // away-from-owner tag: track the one that has stayed longest
                        followMs = dwell;
                        followMac = e.mac;
                    }
                } else if (dwell > PERSIST_MS) { // with-owner tag static past its rotation: leans fake
                    fmStatic++;
                }
            }
            if (e.flags & SF_AWAY)
                away++;
            if (e.flags & SF_HID)
                hid++;
            RuleHit h;
            if (ruleMatch(e, false, h)) {
                if (!strcmp(h.code, "TAG"))
                    trackers++;
                if (h.alert)
                    alert++;
            }
            if (e.company != NO_COMPANY)
                vAdd(v, vn, vendorFromCompanyId(e.company));
            else if (e.publicAddr)
                vAdd(v, vn, vendorFromAddress(e.mac));
        }
        int spam = radioPopupSpamRecent();

        snprintf(b, nb, "Devices: %d", n);
        d->drawString(x + 2, ry, b);
        snprintf(b, nb, "Public %d   Random %d", pub, n - pub);
        d->drawString(x + 2, ry + lh, b);
        if (hid)
            snprintf(b, nb, "Named %d   ! HID %d", named, hid);
        else
            snprintf(b, nb, "Named %d", named);
        d->drawString(x + 2, ry + 2 * lh, b);
        snprintf(b, nb, "Trackers %d (away %d)", trackers, away);
        d->drawString(x + 2, ry + 3 * lh, b);
        snprintf(b, nb, "FindMy %d   Flipper %d", findmy, radioFlipperRecent());
        d->drawString(x + 2, ry + 4 * lh, b);
        if (spam)
            snprintf(b, nb, "! Pairing spam: %d (10s)", spam);
        else
            snprintf(b, nb, "Flagged now: %d", alert);
        d->drawString(x + 2, ry + 5 * lh, b);
        static const int FINDMY_FLOOD = 5; // distinct Find My advertisers in 10 s that looks like a spoof flood
        int fmFlood = radioFindMyRecent();
        if (followMac && followMs >= FOLLOW_MS) {
            // Highest priority: an away-from-owner tracker that has stayed near you for a while is the
            // actual stalking signal (a tag traveling with you, not just passing by).
            snprintf(b, nb, "! Tag following %lum %02X:%02X", (unsigned long)(followMs / 60000), followMac[4],
                     followMac[5]);
        } else if (fmFlood >= FINDMY_FLOOD) {
            // Many distinct Find My tags at once is not normal: a spoofer broadcasting phantoms (or a
            // rotating fake that looks like a crowd of short-lived tags).
            snprintf(b, nb, "! FindMy flood %d (10s)", fmFlood);
        } else if (fmStatic) {
            snprintf(b, nb, "! FindMy static x%d", fmStatic); // non-rotating with-owner tag => suspect fake
        } else {
            char top[64];
            topVendors(v, vn, top, sizeof(top));
            snprintf(b, nb, "Top: %s", top[0] ? top : "-");
        }
        fitText(d, b, d->width() - 4);
        d->drawString(x + 2, ry + 6 * lh, b);
    }

    // ---- unified threats (page 3), shared by the screen, the log and the LED alert -------------
    struct Threat {
        const char *type; // short code for the log (DEAUTH / KARMA / SPAM / TWIN / FLOOD / FOLLOW / PWN / PAIR)
        char detail[24];  // self-describing line, e.g. "deauth x14", "tag 12m away"
        uint8_t mac[6];
        bool hasMac;
        uint8_t chan;
        int8_t rssi;
        bool high; // high severity => worth an LED flourish
    };
    static int typeIndex(const char *t) { return threatTypeIndex(t); } // one canonical list (BadgeRadioScan)
    // WiFi-side threats (the MAC is an AP) vs BLE-side (the MAC is a device); picks devSet's radio.
    static bool threatIsWifi(const char *t)
    {
        return !strcmp(t, "DEAUTH") || !strcmp(t, "KARMA") || !strcmp(t, "SPAM") || !strcmp(t, "TWIN") ||
               !strcmp(t, "ROGUE") || !strcmp(t, "LURE") || !strcmp(t, "MYTWIN") || !strcmp(t, "PWN");
    }
    // Gather every live threat the current scan can see into out[]; returns how many.
    int gatherThreats(const ScanEntry *l, int n, Threat *out, int max)
    {
        int c = 0;
        uint32_t now = millis();
        uint8_t ch = 0;
        int deauth = radioDeauthRecent(&ch);
        if (deauth && c < max) {
            Threat &t = out[c++];
            t = {"DEAUTH", "", {0}, false, ch, 0, true};
            uint8_t dd[6] = {0};
            bool you = radioDeauthDest(dd) && devTrusted(dd); // aimed at one of your own devices?
            snprintf(t.detail, sizeof(t.detail), you ? "deauth YOU x%d" : "deauth x%d", deauth);
            if (radioDeauthTarget(t.mac))
                t.hasMac = true;
        }
        uint8_t km[6] = {0};
        int kworst = radioKarmaWorst(km, nullptr);
        if (kworst >= 3 && c < max) {
            const ScanEntry *ke = nullptr;
            for (int i = 0; i < n; i++)
                if (l[i].channel && !memcmp(l[i].mac, km, 6)) {
                    ke = &l[i];
                    break;
                }
            bool junk = ke && ke->names ? (ke->junkNames * 2 >= ke->names) : (kworst >= 6);
            Threat &t = out[c++];
            t = {junk ? "SPAM" : "KARMA", "", {0}, true, 0, 0, junk ? (kworst >= 6) : true};
            memcpy(t.mac, km, 6);
            snprintf(t.detail, sizeof(t.detail), "%s x%d", junk ? "spam?" : "karma?", kworst);
        }
        for (int i = 0; i < n && c < max; i++)
            if (l[i].channel && (l[i].flags & SF_TWIN) && l[i].name[0]) {
                Threat &t = out[c++];
                t = {"TWIN", "", {0}, true, l[i].channel, 0, false};
                memcpy(t.mac, l[i].mac, 6);
                snprintf(t.detail, sizeof(t.detail), "twin %.17s", l[i].name);
                break;
            }
        int fmFlood = radioFindMyRecent();
        if (fmFlood >= 5 && c < max) {
            Threat &t = out[c++];
            t = {"FLOOD", "", {0}, false, 0, 0, true};
            snprintf(t.detail, sizeof(t.detail), "findmy flood %d", fmFlood);
        }
        uint32_t followMs = 0;
        const uint8_t *fmac = nullptr;
        int8_t frssi = 0;
        for (int i = 0; i < n; i++) {
            const ScanEntry &e = l[i];
            if (e.channel == 0 && (e.flags & SF_AWAY) && (e.flags & (SF_FINDMY | SF_FMDN))) {
                uint32_t dw = now - e.firstMs;
                if (dw > followMs) {
                    followMs = dw;
                    fmac = e.mac;
                    frssi = e.rssi;
                }
            }
        }
        if (fmac && followMs >= 6u * 60 * 1000 && c < max) {
            Threat &t = out[c++];
            t = {"FOLLOW", "", {0}, true, 0, frssi, true};
            memcpy(t.mac, fmac, 6);
            snprintf(t.detail, sizeof(t.detail), "tag %lum away", (unsigned long)(followMs / 60000));
        }
        int pwn = radioPwnagotchiRecent();
        if (pwn && c < max) {
            out[c] = {"PWN", "", {0}, false, 0, 0, false};
            snprintf(out[c].detail, sizeof(out[c].detail), "pwnagotchi %d", pwn);
            c++;
        }
        int ps = radioPopupSpamRecent();
        if (ps && c < max) {
            out[c] = {"PAIR", "", {0}, false, 0, 0, false};
            snprintf(out[c].detail, sizeof(out[c].detail), "ble pair spam %d", ps);
            c++;
        }
        int hidc = 0;
        const uint8_t *hmac = nullptr;
        for (int i = 0; i < n; i++)
            if (l[i].channel == 0 && (l[i].flags & SF_HID)) { // BLE HID peripheral(s) advertising nearby
                hidc++;
                if (!hmac)
                    hmac = l[i].mac;
            }
        if (hidc && c < max) {
            Threat &t = out[c++];
            t = {"HID", "", {0}, hmac != nullptr, 0, 0, false}; // not LED-high: legit keyboards also show
            if (hmac)
                memcpy(t.mac, hmac, 6);
            snprintf(t.detail, sizeof(t.detail), "ble HID x%d", hidc);
        }
        char ls[33];
        uint8_t lb[6];
        if (radioLureRecent(ls, sizeof(ls), lb) && c < max) { // a rogue AP answering a probed name
            Threat &t = out[c++];
            t = {"LURE", "", {0}, true, 0, 0, true};
            memcpy(t.mac, lb, 6);
            snprintf(t.detail, sizeof(t.detail), "lure %.14s", ls);
        }
        for (int i = 0; i < n && c < max; i++)
            if (l[i].channel && l[i].name[0]) {
                const char *tool = rogueApTool(l[i].name); // known attack-tool default SSID
                if (tool) {
                    Threat &t = out[c++];
                    t = {"ROGUE", "", {0}, true, l[i].channel, 0, true};
                    memcpy(t.mac, l[i].mac, 6);
                    snprintf(t.detail, sizeof(t.detail), "%s AP", tool);
                    break;
                }
            }
        // Targeted evil twin: one of YOUR networks showing up as an OPEN AP is almost certainly a
        // clone (your real one is secured) - the strongest "someone is phishing you" signal.
        for (int i = 0; i < n && c < max; i++)
            if (l[i].channel && (l[i].flags & SF_OPEN) && l[i].name[0] && myNetMatch(l[i].name)) {
                Threat &t = out[c++];
                t = {"MYTWIN", "", {0}, true, l[i].channel, 0, true};
                memcpy(t.mac, l[i].mac, 6);
                snprintf(t.detail, sizeof(t.detail), "YOUR net OPEN %.9s", l[i].name);
                break;
            }
        // Drop threats whose MAC the user has trusted (except deauth: the MAC there is the attacked
        // AP, so trusting your own AP must not hide an attack against it).
        int w = 0;
        for (int i = 0; i < c; i++) {
            if (out[i].hasMac && strcmp(out[i].type, "DEAUTH") != 0 && devTrusted(out[i].mac))
                continue;
            if (w != i)
                out[w] = out[i];
            w++;
        }
        return w;
    }
    void drawThreats(OLEDDisplay *d, int16_t x, int ry, int lh, const ScanEntry *l, int n, char *b, size_t nb)
    {
        Threat th[12];
        int c = gatherThreats(l, n, th, 12);
        if (c == 0) {
            d->drawString(x + 2, ry, "No threats seen now");
            d->drawString(x + 2, ry + lh, "WiFi + BLE watched");
            d->drawString(x + 2, ry + 2 * lh, "logged -> events.csv");
            return;
        }
        int shown = c < 6 ? c : 6;
        for (int i = 0; i < shown; i++) {
            if (th[i].hasMac)
                snprintf(b, nb, "! %s %02X:%02X", th[i].detail, th[i].mac[4], th[i].mac[5]);
            else
                snprintf(b, nb, "! %s", th[i].detail);
            fitText(d, b, d->width() - 4);
            d->drawString(x + 2, ry + i * lh, b);
        }
        bool trustable = false; // offer "trust" only when there is an allowlistable offender on screen
        for (int i = 0; i < shown; i++)
            if (th[i].hasMac && strcmp(th[i].type, "DEAUTH") != 0)
                trustable = true;
        if (trustable)
            d->drawString(x + 2, ry + 6 * lh, "0: trust top offender");
    }
    // Every tick: flash the LEDs on a newly-appearing high-severity threat and append new/changed
    // threats to /badge/events.csv (deduped per type on a 60 s cooldown).
    void logAndAlert()
    {
        int n = radioScanSnapshot(list, MAXLIST);
        Threat th[12];
        int c = gatherThreats(list, n, th, 12);
        uint32_t highMask = 0;
        for (int i = 0; i < c; i++)
            if (th[i].high)
                highMask |= 1u << typeIndex(th[i].type);
        if (highMask & ~prevHighMask)
            ledsFlourish(); // a new high-severity threat just appeared
        prevHighMask = highMask;
        for (int i = 0; i < c; i++) // shared log (deduped) so screen + background watch write one file
            threatRecord(th[i].type, th[i].detail, th[i].hasMac ? th[i].mac : nullptr, th[i].chan, th[i].rssi);
        for (int i = 0; i < c; i++) // remember for the "trust top offender" key on the Threats page
            lastThreats[i] = th[i];
        lastThreatCount = c;
    }
    // Trust the first on-screen offender (not a deauth-victim AP): it then stops alerting/logging.
    void trustTopThreat()
    {
        for (int i = 0; i < lastThreatCount; i++) {
            const Threat &t = lastThreats[i];
            if (!t.hasMac || !strcmp(t.type, "DEAUTH"))
                continue;
            const DevMemo *m = devFind(t.mac);
            uint8_t flags = (m ? m->flags : 0) | DEV_TRUSTED; // keep any existing flags, add trusted
            devSet(t.mac, flags, nullptr, threatIsWifi(t.type));
            return;
        }
    }
    uint32_t prevHighMask = 0;
    Threat lastThreats[12];
    int lastThreatCount = 0;
};

static ScanStatsApp &scanStatsApp()
{
    static ScanStatsApp *p = psramNew<ScanStatsApp>();
    return *p;
}

void openScanStats()
{
    appOpen(&scanStatsApp());
}

} // namespace badge

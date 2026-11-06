// WiFi / Bluetooth fox hunt: lists the access points or Bluetooth devices around, strongest
// first; pick one and the same meter, keypad bar graph and Warmer / Colder follow its RSSI.
//
// Devices that meet a rule (tools/data/rules) show its title; surveillance, police, camera and
// tracker ones go to the top with a bar down the left, and the keys flash while any are near.
//
// Anything following you (BadgeFollow.h) goes above that, and alerts pop up over the screen.
//
//   list: joystick (or 2 / 8) move, # (or 5) hunt it, * done
//   hunt: # details (signal graph, everything known about it), 9 turn in a circle to find
//         its direction, 1 share this sighting with other badges (asks first), 0 reset the
//         best-reading tick, * back to the list; details: joystick scrolls
//   hold * on a device (list or hunt): its menu (nickname, trusted, watchlist, make it beep)
//   an alert message: any key closes it
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeDeviceMenu.h"
#include "BadgeDevices.h"
#include "BadgeFollow.h"
#include "BadgeFox.h"
#include "BadgeListFilter.h"
#include "BadgeRadioScan.h"
#include "BadgeRules.h"
#include "BadgeTime.h"
#include "BadgeVendors.h"
#include "gps/RTC.h"
#include <algorithm>

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "mesh/Throttle.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace badge
{

// "14:02 (12m ago)" when the badge knows the time, else "12m ago"
static void whenText(char *b, size_t n, uint32_t ms)
{
    uint32_t ago = (millis() - ms) / 1000;
    char a[16];
    if (ago < 60)
        snprintf(a, sizeof(a), "%us ago", (unsigned)ago);
    else if (ago < 3600)
        snprintf(a, sizeof(a), "%um ago", (unsigned)(ago / 60));
    else
        snprintf(a, sizeof(a), "%uh%02um ago", (unsigned)(ago / 3600), (unsigned)(ago % 3600 / 60));
    if (timeKnown()) {
        uint32_t t = getValidTime(RTCQualityFromNet, true) - ago;
        snprintf(b, n, "%02u:%02u (%s)", (unsigned)(t % 86400 / 3600), (unsigned)(t % 3600 / 60), a);
    } else {
        snprintf(b, n, "%s", a);
    }
}

static const float RSSI_LO = -100, RSSI_HI = -30; // WiFi / BLE range, dBm
static const int MAXLIST = 40;

static void macText(const uint8_t *m, char *out, size_t n)
{
    snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}

// maker: from the advert's company ID, else the address prefix; "" if neither names one.
// detail: also say when the advert carries an ID nobody was assigned
static void makerText(const ScanEntry &e, char *out, size_t n, bool detail)
{
    const char *c = e.company != NO_COMPANY ? vendorFromCompanyId(e.company) : nullptr;
    if (!c && e.publicAddr)
        c = vendorFromAddress(e.mac);
    if (c)
        snprintf(out, n, "%s", c);
    else if (detail && e.company != NO_COMPANY)
        snprintf(out, n, "ID %04X unregistered", e.company);
    else
        out[0] = 0;
}

// a rule's short code ("SURV", "CAM", ...) in a box of the opposite colour; returns its width
static int drawCodeBox(OLEDDisplay *d, int x, int y, const char *code, bool onWhite)
{
    int tw = d->getStringWidth(code, (uint16_t)strlen(code)) + 4, h = fontHeight(FONT_SMALL);
    d->setColor(onWhite ? BLACK : WHITE);
    d->fillRect(x, y + 1, tw, h - 1);
    d->setColor(onWhite ? WHITE : BLACK);
    d->setTextAlignment(TEXT_ALIGN_LEFT);
    d->drawString(x + 2, y, code);
    d->setColor(onWhite ? BLACK : WHITE);
    return tw;
}

static int textW(OLEDDisplay *d, const char *s)
{
    return d->getStringWidth(s, (uint16_t)strlen(s));
}

static int nextChar(const char *s, int i)
{
    i++;
    while (s[i] && ((uint8_t)s[i] & 0xC0) == 0x80) // don't split a UTF-8 letter
        i++;
    return i;
}

// shorten s until it fits in w pixels
static void fitText(OLEDDisplay *d, char *s, int w)
{
    int len = strlen(s);
    while (len > 1 && d->getStringWidth(s, (uint16_t)len) > w)
        len--;
    while (len > 1 && ((uint8_t)s[len] & 0xC0) == 0x80)
        len--;
    s[len] = 0;
}

// a maker name in a w-pixel window from x: cut short, or (scroll) when it doesn't fit, shown
// a letter further on every STEP ms, with a pause at each end. Returns the width drawn.
static int drawMaker(OLEDDisplay *d, int x, int y, int w, const char *name, bool scroll, uint32_t sinceMs)
{
    static const uint32_t PAUSE = 1200, STEP = 250;
    char buf[64];
    snprintf(buf, sizeof(buf), "%s", name);
    d->setFont(FONT_SMALL);
    int len = strlen(buf), off = 0;
    if (scroll && textW(d, buf) > w) {
        int last = 0, steps = 0; // the first letter from which the rest fits
        while (last < len && d->getStringWidth(buf + last, (uint16_t)(len - last)) > w)
            last = nextChar(buf, last), steps++;
        uint32_t t = sinceMs % (2 * PAUSE + steps * STEP);
        int step = t < PAUSE ? 0 : (int)((t - PAUSE) / STEP);
        for (int i = 0; i < step && off < last; i++)
            off = nextChar(buf, off);
    }
    char *shown = buf + off;
    fitText(d, shown, w);
    d->setTextAlignment(TEXT_ALIGN_LEFT);
    d->drawString(x, y, shown);
    return textW(d, shown);
}

class RadioHuntApp : public App
{
  public:
    // Digits are this app's own controls (3 = the device options, etc.), so don't let a held digit
    // run a global shortcut - holding 3 used to fire the "ble" shortcut and relaunch the scan.
    bool numberShortcuts() const override { return false; }
    bool typingLights() const override { return filter.editing(); }
    void setKind(ScanKind k) { kind = listKind = k; }
    const char *name() const override
    {
        return listKind == SCAN_ALL ? "Everything hunt" : kind == SCAN_WIFI ? "WiFi hunt" : "Bluetooth hunt";
    }
    uint32_t frameMs() const override { return 250; } // fast enough for scrolling maker names
    bool keepAwake() const override { return true; }
    void begin() override
    {
        hunting = details = false;
        haveSel = false;
        popup[0] = 0;
        swallowKey = -1;
        menu.open = false;
        ignoreStarRelease = false;
        filter.clear();
        watchPause(true);
        error = radioScanStart(kind);
        if (!error && haveStart) { // opened to hunt one device (flagged devices, after a nickname)
            haveStart = false;
            target = {};
            memcpy(target.mac, startMac, 6);
            target.company = NO_COMPANY;
            target.mfgByte = -1;
            target.publicAddr = startPublic;
            snprintf(target.name, sizeof(target.name), "%s", devName(startMac, ""));
            hunting = true;
            details = askShare = false;
            turn.cancel();
            histHead = histCount = 0;
            heard = false;
            lastSeen = 0;
            peak = -999;
            trend = 0;
            radioScanFocus(target.mac, 0);
        }
    }
    // open straight onto hunting this device
    void startOn(const uint8_t mac[6], bool isPublic)
    {
        memcpy(startMac, mac, 6);
        startPublic = isPublic;
        haveStart = true;
    }
    void end() override
    {
        radioScanStop();
        watchPause(false);
    }
    void tick(uint32_t now) override
    {
        filter.tick(now);
        if (error)
            return;
        radioScanPoll();
        if (!Throttle::isWithinTimespanMs(lastWatch, 2000)) {
            lastWatch = now;
            int n = radioScanSnapshot(raw, MAXLIST);
            const char *msg;
            if (kind == SCAN_ALL) { // the watch takes one kind at a time
                int nWifi = std::stable_partition(raw, raw + n, [](const ScanEntry &e) { return e.channel != 0; }) - raw;
                msg = watchStep(raw, nWifi, true, true);
                const char *bt = watchStep(raw + nWifi, n - nWifi, false, !msg);
                msg = msg ? msg : bt;
            } else {
                msg = watchStep(raw, n, kind == SCAN_WIFI, true);
            }
            if (msg) {
                snprintf(popup, sizeof(popup), "%s", msg);
                popupAt = now;
                appRedraw();
            }
        }
        if (popup[0] && !Throttle::isWithinTimespanMs(popupAt, 10000)) {
            popup[0] = 0;
            appRedraw();
        }
        if (hunting && turn.active) {
            ScanEntry e;
            bool fresh = radioScanFind(target.mac, e) && Throttle::isWithinTimespanMs(e.seenMs, 1500);
            turn.sample(fresh ? foxScale(e.rssi, RSSI_LO, RSSI_HI) : -1, now);
            if (!turn.active) // just finished
                appRedraw();
        }
        if (hunting && !Throttle::isWithinTimespanMs(lastSample, 1000)) {
            lastSample = now;
            ScanEntry e;
            bool fresh = radioScanFind(target.mac, e) && Throttle::isWithinTimespanMs(e.seenMs, 2500);
            history[histHead] = fresh ? e.rssi : NO_SAMPLE;
            histHead = (histHead + 1) % HIST;
            if (histCount < HIST)
                histCount++;
        }
    }
    void key(int k, KeyAction a) override
    {
        if (k == AK_STAR && a == KA_RELEASE && ignoreStarRelease) { // the end of the hold that opened the menu
            ignoreStarRelease = false;
            return;
        }
        if (filter.editing()) { // the search box has the keys
            filter.key(k, a, millis());
            appRedraw();
            return;
        }
        if (menu.open) {
            DeviceMenu::Act act = menu.key(k, a);
            if (act == DeviceMenu::A_NICKNAME)
                deviceNicknameEdit(menu.mac, menu.wifi, [](const uint8_t mac[6], bool wifi) { openHuntFor(!wifi, mac); });
            else if (act == DeviceMenu::A_EXPLORE)
                openBleExplorer(menu.mac, menu.publicAddr, devName(menu.mac, ""),
                                [](const uint8_t mac[6], bool wifi) { openHuntFor(!wifi, mac); });
            appRedraw();
            return;
        }
        if (k == AK_3 && a == KA_PRESS) { // 3: the selected device's menu (nickname, watch, beep, Explore)
            const ScanEntry *e = nullptr;
            ScanEntry sel;
            if (hunting) {
                e = &target;
            } else if (count > 0) { // the highlighted device (the first one if you haven't moved yet)
                int i = (haveSel && selIndex() >= 0) ? selIndex() : 0;
                sel = list[i];
                e = &sel;
            }
            if (e) {
                RuleHit h;
                bool tracker = ruleMatch(*e, isWifi(*e), h) && !strcmp(h.code, "TAG");
                menu.show(e->mac, isWifi(*e), tracker, e->publicAddr, devName(e->mac, e->name));
                appRedraw();
            }
            return;
        }
        if (popup[0] && a == KA_PRESS) { // any key closes an alert message
            popup[0] = 0;
            swallowKey = k;
            appRedraw();
            return;
        }
        if (k == swallowKey) {
            if (a == KA_RELEASE)
                swallowKey = -1;
            return;
        }
        if (askShare) { // "Share this sighting?": # sends, anything else doesn't
            if (a == KA_PRESS) {
                askShare = false;
                swallowKey = k;
                if (k == AK_HASH || k == AK_5) {
                    snprintf(popup, sizeof(popup), "%s", watchShareNow(target, kind == SCAN_WIFI));
                    popupAt = millis();
                }
                appRedraw();
            }
            return;
        }
        if (turn.showing()) { // * cancels a turn; any key closes the result
            if ((turn.done && a == KA_PRESS) || (k == AK_STAR && a == KA_RELEASE)) {
                turn.cancel();
                if (a == KA_PRESS)
                    swallowKey = k;
                appRedraw();
            }
            return;
        }
        if (k == AK_STAR && a == KA_RELEASE) {
            if (hunting) {
                hunting = false;
                if (listKind != kind) { // back to the everything list
                    kind = listKind;
                    error = radioScanStart(kind);
                } else {
                    radioScanFocus(nullptr, 0);
                }
                appRedraw();
            } else {
                appClose();
            }
            return;
        }
        if (error || (a != KA_PRESS && a != KA_REPEAT))
            return;
        if (hunting) {
            if (k == AK_0 && a == KA_PRESS)
                peak = -999;
            else if (k == AK_9 && a == KA_PRESS)
                turn.begin(millis());
            else if (k == AK_1 && a == KA_PRESS)
                askShare = true;
            else if ((k == AK_HASH || k == AK_5) && a == KA_PRESS)
                details = !details, detailTop = 0;
            else if (details && (k == AK_UP || k == AK_2) && detailTop > 0)
                detailTop--;
            else if (details && (k == AK_DOWN || k == AK_8))
                detailTop++;
        } else if (k == AK_UP || k == AK_LEFT || k == AK_2) {
            move(-1);
        } else if (k == AK_DOWN || k == AK_RIGHT || k == AK_8) {
            move(1);
        } else if (k == AK_0 && a == KA_PRESS) {
            filter.open();
        } else if ((k == AK_HASH || k == AK_5) && a == KA_PRESS) {
            pick();
        }
        appRedraw();
    }
    bool keypadLeds(Rgb *k, uint32_t now) override
    {
        if (hunting) {
            foxKeypadBar(k, strength(), now);
            return true;
        }
        if (!alertNear)
            return false;
        for (int i = 0; i < KEYPAD_LEDS; i++) {
            if (alertNear == 3) { // something is following you: fast red
                float r = (now / 125) % 2 ? 0.9f : 0.05f;
                k[i][0] = r;
                k[i][1] = k[i][2] = 0;
            } else if (alertNear == 2) { // police-style red / blue
                bool red = (now / 250) % 2 == (uint32_t)(i % 2);
                k[i][0] = red ? 0.7f : 0;
                k[i][1] = 0;
                k[i][2] = red ? 0 : 0.7f;
            } else { // trackers only: slow white breathing
                float b = 0.1f + 0.4f * (0.5f + 0.5f * sinf(now / 600.0f));
                k[i][0] = k[i][1] = k[i][2] = b;
            }
        }
        return true;
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        if (hunting && turn.showing())
            turn.draw(d, x, y, millis());
        else if (hunting && details)
            drawDetails(d, x, y);
        else if (hunting)
            drawHunt(d, x, y);
        else
            drawList(d, x, y);
        if (menu.open) {
            menu.draw(d, x, y);
            return;
        }
        if (askShare)
            drawBox(d, x, y, "Share this sighting\nwith other badges?\n# yes   * no");
        else if (popup[0])
            drawBox(d, x, y, popup);
    }

  private:
    ListFilter filter;
    ScanKind kind = SCAN_WIFI, listKind = SCAN_WIFI; // kind: what's scanning now (a hunt uses its own radio)
    const char *error = nullptr;
    bool isWifi(const ScanEntry &e) const { return kind == SCAN_ALL ? e.channel != 0 : kind == SCAN_WIFI; }
    ScanEntry list[MAXLIST], raw[MAXLIST]; // raw: unsorted snapshot (kept off the stack)
    RuleHit hits[MAXLIST];
    bool hasHit[MAXLIST];
    int count = 0, alertCount = 0;
    int alertNear = 0;      // 0 none, 1 trackers only, 2 other alert devices, 3 following you
    int followMin[MAXLIST]; // minutes an alert device has been with you, -1 if not tracked
    bool followYes[MAXLIST];
    char popup[128] = "";
    uint32_t popupAt = 0, lastWatch = 0, lastSample = 0;
    int swallowKey = -1; // the key that closed a message: its release does nothing else
    bool details = false, askShare = false;
    TurnSweep turn;
    int detailTop = 0;
    static const int HIST = 120; // seconds of signal history on the details page
    static const int8_t NO_SAMPLE = -128;
    int8_t history[HIST];
    int histHead = 0, histCount = 0;
    bool haveSel = false, hunting = false;
    static const int FAINT_DBM = -85; // "faint" in Alert settings > Lists show faint devices
    DeviceMenu menu;
    bool ignoreStarRelease = false, haveStart = false, startPublic = false;
    uint8_t startMac[6] = {};
    uint8_t selMac[6] = {};
    int top = 0;
    uint32_t scrollFrom = 0; // a maker name scrolls from the start when the selection changes
    // hunt state
    ScanEntry target = {};
    uint32_t lastSeen = 0;
    float smooth = 0, peak = -999;
    int trend = 0;
    bool heard = false;

    int selIndex()
    {
        for (int i = 0; i < count; i++)
            if (memcmp(list[i].mac, selMac, 6) == 0)
                return i;
        return -1;
    }
    // fresh list: following you, then other alert devices, then the rest (each strongest first)
    // a lowercase haystack for the search: name, maker, address and any mark
    const char *entryHay(const ScanEntry &e, const RuleHit *h)
    {
        static char hay[96];
        char maker[32], addr[40];
        makerText(e, maker, sizeof(maker), true);
        macText(e.mac, addr, sizeof(addr));
        const DevMemo *m = devFind(e.mac);
        snprintf(hay, sizeof(hay), "%s %s %s %s %s %s", m && m->nick[0] ? m->nick : "", e.name, maker, addr, h ? h->code : "",
                 h ? h->title : "");
        for (char *c = hay; *c; c++)
            *c = (char)tolower(*c);
        return hay;
    }
    void refresh()
    {
        int n = radioScanSnapshot(raw, MAXLIST);
        count = alertCount = alertNear = 0;
        bool faint = watchListFaint();
        for (int pass = 0; pass < 3; pass++)
            for (int i = 0; i < n; i++) {
                if (!faint && raw[i].rssi < FAINT_DBM)
                    continue;
                RuleHit h;
                bool hit = ruleMatch(raw[i], isWifi(raw[i]), h);
                if (filter.active() && !filter.match(entryHay(raw[i], hit ? &h : nullptr)))
                    continue;
                bool alert = hit && h.alert && !devTrusted(raw[i].mac); // trusted: listed, never flagged
                FollowInfo f;
                bool tracked = alert && followGet(raw[i].mac, f);
                bool following = tracked && f.following;
                if ((pass == 0 && !following) || (pass == 1 && (!alert || following)) || (pass == 2 && alert))
                    continue;
                list[count] = raw[i];
                hasHit[count] = hit;
                if (hit)
                    hits[count] = h;
                followMin[count] = tracked ? (int)(f.withYouMs / 60000) : -1;
                followYes[count] = following;
                if (alert) {
                    alertCount++;
                    int level = following ? 3 : strcmp(h.tag, "Tracker") != 0 ? 2 : 1;
                    if (level > alertNear)
                        alertNear = level;
                }
                count++;
            }
    }
    void move(int delta)
    {
        refresh();
        if (!count)
            return;
        int i = haveSel ? selIndex() : -1;
        i = i < 0 ? 0 : i + delta;
        i = i < 0 ? 0 : (i >= count ? count - 1 : i);
        if (!haveSel || memcmp(selMac, list[i].mac, 6) != 0)
            scrollFrom = millis();
        memcpy(selMac, list[i].mac, 6);
        haveSel = true;
    }
    void pick()
    {
        refresh();
        int i = haveSel ? selIndex() : (count ? 0 : -1);
        if (i < 0)
            return;
        target = list[i];
        if (kind == SCAN_ALL) { // hunt it with its own radio only: readings come much quicker
            kind = isWifi(target) ? SCAN_WIFI : SCAN_BLE;
            error = radioScanStart(kind);
        }
        hunting = true;
        details = askShare = false;
        turn.cancel();
        histHead = histCount = 0;
        scrollFrom = millis();
        heard = false;
        lastSeen = 0;
        peak = -999;
        trend = 0;
        radioScanFocus(target.mac, target.channel);
    }

    // smoothed RSSI as 0..1, -1 when not heard in the last 30 s
    float strength()
    {
        ScanEntry e;
        if (!radioScanFind(target.mac, e) || !Throttle::isWithinTimespanMs(e.seenMs, 30000))
            return -1;
        if (!heard || e.seenMs != lastSeen) {
            float before = smooth;
            smooth = heard ? 0.6f * smooth + 0.4f * e.rssi : e.rssi; // WiFi / BLE RSSI jumps a few dB
            trend = !heard ? 0 : smooth > before + 1.0f ? 1 : smooth < before - 1.0f ? -1 : 0;
            if (e.rssi > peak)
                peak = e.rssi;
            heard = true;
            lastSeen = e.seenMs;
            target = e;
        }
        return foxScale(smooth, RSSI_LO, RSSI_HI);
    }

    int entryLines(int i) const { return hasHit[i] ? 3 : 2; } // name, [rule], address

    void drawList(OLEDDisplay *d, int16_t x, int16_t y)
    {
        int lh = fontHeight(FONT_SMALL);
        char title[32];
        if (error)
            count = alertCount = alertNear = 0;
        else
            refresh();
        if (alertCount)
            snprintf(title, sizeof(title), "%s %d  !%d",
                     kind == SCAN_ALL    ? "All"
                     : kind == SCAN_WIFI ? "WiFi"
                                         : "BT",
                     count, alertCount);
        else
            snprintf(title, sizeof(title), "%s (%d)",
                     kind == SCAN_ALL    ? "WiFi+BT"
                     : kind == SCAN_WIFI ? "WiFi"
                                         : "Bluetooth",
                     count);
        drawTitle(d, x, y, title, filter.active() ? "#hunt 3opts" : "#hunt 3opts *done");
        if (error) {
            drawCentered(d, x, y + 2 * lh, error, FONT_SMALL);
            return;
        }
        if (!count) {
            drawCentered(d, x, y + 2 * lh + 6, filter.active() ? "No match" : "Scanning...", FONT_MEDIUM);
            if (filter.active())
                filter.draw(d, x, y);
            return;
        }
        int sel = haveSel ? selIndex() : 0;
        if (sel < 0)
            sel = 0;
        int botRes = filter.active() ? lh + 2 : 0; // leave room for the search bar
        int listTop = y + lh + 2, room = d->height() - lh - 2 - botRes;
        if (top > sel)
            top = sel;
        if (top >= count)
            top = count - 1;
        for (;;) { // keep the selected entry on screen
            int h = 0;
            for (int i = top; i <= sel; i++)
                h += entryLines(i) * lh;
            if (h <= room || top == sel)
                break;
            top++;
        }

        d->setFont(FONT_SMALL);
        int ry = listTop;
        for (int i = top; i < count; i++) {
            int eh = entryLines(i) * lh;
            if (ry + eh > y + d->height() - botRes)
                break;
            const ScanEntry &e = list[i];
            bool isSel = i == sel, alert = hasHit[i] && hits[i].alert;
            if (isSel) {
                d->setColor(WHITE);
                d->fillRect(x, ry, d->width(), eh);
            }
            if (alert) { // a bar down the left of the entry
                d->setColor(isSel ? BLACK : WHITE);
                d->fillRect(x, ry, 3, eh);
            }
            d->setColor(isSel ? BLACK : WHITE);
            int cx = x + (alert ? 6 : 2);
            char rssi[8], addr[40];
            snprintf(rssi, sizeof(rssi), "%d", e.rssi);
            drawRow(d, cx, ry, x + d->width() - 30 - cx, e, isSel);
            d->setTextAlignment(TEXT_ALIGN_RIGHT);
            d->drawString(x + d->width() - 2, ry, rssi);
            d->setTextAlignment(TEXT_ALIGN_LEFT);
            int line = ry + lh;
            if (hasHit[i]) {
                int bw = drawCodeBox(d, cx, line, followYes[i] ? "FOLW" : hits[i].code, isSel);
                char t[56];
                if (followMin[i] >= 1) // how long it has been with you
                    snprintf(t, sizeof(t), "%dm %s", followMin[i], hits[i].title);
                else
                    snprintf(t, sizeof(t), "%s", hits[i].title);
                fitText(d, t, x + d->width() - 2 - (cx + bw + 3));
                d->drawString(cx + bw + 3, line, t);
                line += lh;
            }
            macText(e.mac, addr, sizeof(addr));
            int n = strlen(addr);
            if (isWifi(e))
                snprintf(addr + n, sizeof(addr) - n, " %s ch %d%s", e.publicAddr ? "pub" : "rnd", e.channel,
                         kind == SCAN_ALL ? " WiFi" : "");
            else
                snprintf(addr + n, sizeof(addr) - n, " %s%s", e.publicAddr ? "pub" : "rnd", kind == SCAN_ALL ? " BT" : "");
            fitText(d, addr, x + d->width() - 2 - (cx + 6));
            d->drawString(cx + 6, line, addr);
            d->setColor(WHITE);
            ry += eh;
        }
        if (filter.active())
            filter.draw(d, x, y);
    }

    // name (maker), the maker, or no name; only the maker part scrolls (selected row)
    void drawRow(OLEDDisplay *d, int x, int y, int w, const ScanEntry &e, bool isSel)
    {
        char maker[32], text[48];
        makerText(e, maker, sizeof(maker), false);
        const DevMemo *memo = devFind(e.mac);
        if (memo && memo->nick[0]) { // your nickname instead of what it calls itself
            snprintf(text, sizeof(text), "%s%s", memo->flags & DEV_TRUSTED ? "~" : "", memo->nick);
            fitText(d, text, w);
            d->setFont(FONT_SMALL);
            d->setTextAlignment(TEXT_ALIGN_LEFT);
            d->drawString(x, y, text);
            return;
        }
        uint32_t since = millis() - scrollFrom;
        d->setFont(FONT_SMALL);
        d->setTextAlignment(TEXT_ALIGN_LEFT);
        if (!maker[0]) {
            if (e.name[0])
                snprintf(text, sizeof(text), "%s", e.name);
            else
                snprintf(text, sizeof(text), isWifi(e) ? "(hidden network)" : "(no name)");
            fitText(d, text, w);
            d->drawString(x, y, text);
        } else if (e.name[0]) {
            snprintf(text, sizeof(text), "%s (", e.name);
            int closeW = textW(d, ")");
            fitText(d, text, w * 3 / 5); // leave the maker at least the rest of the row
            d->setTextAlignment(TEXT_ALIGN_LEFT);
            d->drawString(x, y, text);
            int mx = x + textW(d, text);
            int mw = drawMaker(d, mx, y, x + w - closeW - mx, maker, isSel, since);
            d->drawString(mx + mw, y, ")");
        } else {
            drawMaker(d, x, y, w, maker, isSel, since);
        }
    }

    void drawHunt(OLEDDisplay *d, int16_t x, int16_t y)
    {
        int lh = fontHeight(FONT_SMALL);
        drawTitle(d, x, y, kind == SCAN_WIFI ? "WiFi hunt" : "BT hunt", "#info *list");
        char b[64];
        if (devFind(target.mac) && devFind(target.mac)->nick[0])
            snprintf(b, sizeof(b), "%s", devFind(target.mac)->nick);
        else if (target.name[0])
            snprintf(b, sizeof(b), "%s", target.name);
        else
            snprintf(b, sizeof(b), kind == SCAN_WIFI ? "(hidden network)" : "(no name)");
        fitText(d, b, d->width() - 4);
        drawCentered(d, x, y + lh + 2, b, FONT_SMALL);
        float s = strength();
        int wordY = y + 2 * lh + 6;
        if (s < 0) {
            drawCentered(d, x, wordY + 10, heard ? "Lost it" : "Listening...", FONT_MEDIUM);
            drawCentered(d, x, wordY + 40, "Walk around", FONT_SMALL);
            return;
        }
        drawCentered(d, x, wordY, foxWord(s, trend), FONT_MEDIUM);
        int row = wordY + fontHeight(FONT_MEDIUM) + 4;
        snprintf(b, sizeof(b), "%d dBm", target.rssi);
        foxMeter(d, x, row, "RSSI", s, peak > -900 ? foxScale(peak, RSSI_LO, RSSI_HI) : -1, b);
        row += lh + 1;
        int age = (int)((millis() - target.seenMs) / 1000);
        char first[12] = "?";
        ScanEntry cur;
        if (radioScanFind(target.mac, cur) && cur.firstMs) { // how long since it first turned up
            uint32_t f = (millis() - cur.firstMs) / 1000;
            if (f < 60)
                snprintf(first, sizeof(first), "%us", (unsigned)f);
            else if (f < 3600)
                snprintf(first, sizeof(first), "%um", (unsigned)(f / 60));
            else
                snprintf(first, sizeof(first), "%uh", (unsigned)(f / 3600));
        }
        if (kind == SCAN_WIFI)
            snprintf(b, sizeof(b), "best %d ch%d last %ds 1st %s", (int)peak, target.channel, age, first);
        else
            snprintf(b, sizeof(b), "best %d  last %ds  first %s", (int)peak, age, first);
        fitText(d, b, d->width() - 4);
        drawCentered(d, x, row, b, FONT_SMALL);
        row += lh;
        makerText(target, b, sizeof(b), true);
        if (!b[0])
            snprintf(b, sizeof(b), "maker unknown");
        if (textW(d, b) <= d->width() - 4)
            drawCentered(d, x, row, b, FONT_SMALL);
        else
            drawMaker(d, x + 2, row, d->width() - 4, b, true, millis() - scrollFrom);
        row += lh;
        macText(target.mac, b, sizeof(b));
        strcat(b, target.publicAddr ? " pub" : " rnd"); // random addresses don't name a maker
        drawCentered(d, x, row, b, FONT_SMALL);
        row += lh;
        RuleHit h;
        if (ruleMatch(target, kind == SCAN_WIFI, h)) {
            FollowInfo f;
            bool tracked = h.alert && followGet(target.mac, f);
            int bw = drawCodeBox(d, x + 2, row, tracked && f.following ? "FOLW" : h.code, false);
            if (tracked && f.withYouMs >= 60000)
                snprintf(b, sizeof(b), "%um %s", (unsigned)(f.withYouMs / 60000), h.title);
            else
                snprintf(b, sizeof(b), "%s", h.title);
            fitText(d, b, d->width() - 8 - bw);
            d->drawString(x + 2 + bw + 4, row, b);
        }
    }

    // signal over the last two minutes, then everything known about the device
    void drawDetails(OLEDDisplay *d, int16_t x, int16_t y)
    {
        int lh = fontHeight(FONT_SMALL);
        strength(); // keeps target fresh
        drawTitle(d, x, y, "Details", "#back *list");
        int gx = x + 2, gy = y + lh + 3, gw = d->width() - 4, gh = 28;
        d->drawRect(gx, gy, gw, gh);
        int px = -1, py = 0;
        for (int i = 0; i < histCount; i++) {
            int8_t v = history[(histHead - histCount + i + HIST) % HIST];
            int sx = gx + 1 + (gw - 3) * (HIST - histCount + i) / (HIST - 1);
            if (v == NO_SAMPLE) {
                px = -1;
                continue;
            }
            int sy = gy + gh - 2 - (int)((gh - 4) * foxScale(v, RSSI_LO, RSSI_HI));
            if (px >= 0)
                d->drawLine(px, py, sx, sy);
            else
                d->setPixel(sx, sy);
            px = sx, py = sy;
        }
        d->setFont(FONT_SMALL);
        d->setTextAlignment(TEXT_ALIGN_RIGHT);
        d->drawString(gx + gw - 2, gy + gh - lh, "2 min");
        d->setTextAlignment(TEXT_ALIGN_LEFT);

        static char (*lines)[56] = (char (*)[56])psramArray<char>(22 * 56);
        int n = 0;
        auto add = [&](const char *fmt, auto... args) {
            if (n < 22)
                snprintf(lines[n++], sizeof(lines[0]), fmt, args...);
        };
        add("Name: %s", target.name[0] ? target.name : "(none)");
        int makerLine = n;
        add("Maker: ");
        char m[48];
        makerText(target, m, sizeof(m), true);
        if (kind == SCAN_BLE && target.company != NO_COMPANY) {
            if (target.mfgByte >= 0)
                add("Company ID %04X, type %02X", target.company, target.mfgByte);
            else
                add("Company ID %04X", target.company);
        }
        char a[24];
        macText(target.mac, a, sizeof(a));
        add("%s %s", a, target.publicAddr ? "registered" : "random");
        int age = (int)((millis() - target.seenMs) / 1000);
        add("Now %d dBm, best %d, %d s ago", target.rssi, (int)peak, age);
        char when[32];
        ScanEntry cur;
        if (radioScanFind(target.mac, cur) && cur.firstMs) {
            whenText(when, sizeof(when), cur.firstMs);
            add("First seen %s", when);
            whenText(when, sizeof(when), cur.seenMs);
            add("Last seen %s", when);
        }
        if (kind == SCAN_WIFI) {
            add("WiFi channel %d, %s", target.channel, (target.flags & SF_OPEN) ? "OPEN (no password)" : "protected");
            if (target.names > 1)
                add("Names from this radio: %u", target.names);
        }
        DroneInfo dr;
        if (kind == SCAN_BLE && radioDroneGet(target.mac, dr)) {
            add("Drone ID: %s", dr.id[0] ? dr.id : "(not heard yet)");
            if (dr.operatorId[0])
                add("Operator ID: %s", dr.operatorId);
            if (dr.hasPos) {
                add("Drone %.5f %.5f", dr.lat * 1e-7, dr.lon * 1e-7);
                add("Alt %d m, height %d m", (int)dr.altM, (int)dr.heightM);
                add("Speed %.1f m/s, heading %u", dr.speedMs, dr.heading);
            }
            if (dr.hasOperator)
                add("Pilot %.5f %.5f", dr.opLat * 1e-7, dr.opLon * 1e-7);
        }
        RuleHit h;
        bool hit = ruleMatch(target, kind == SCAN_WIFI, h);
        if (hit)
            add("%s: %s", h.code, h.title);
        else
            add("No rule matches");
        FollowInfo f;
        if (hit && h.alert && followGet(target.mac, f)) {
            add("With you %u min (%d = following)", (unsigned)(f.withYouMs / 60000), watchFollowMin());
            if (f.refs >= 3)
                add("Landmarks: %u of %u still here", f.refsLeft, f.refs);
            else
                add("Too few landmarks to tell moves");
            add(f.moved ? "FOLLOWING: you moved, it came" : f.following ? "FOLLOWING (or you stayed put)" : "Not following yet");
            if (f.seenBefore) {
                if (f.agoS >= 3600)
                    add("Seen before %u times, %u h ago", f.seenBefore, (unsigned)(f.agoS / 3600));
                else if (f.agoS)
                    add("Seen before %u times, %u min ago", f.seenBefore, (unsigned)(f.agoS / 60));
                else
                    add("Seen before %u times", f.seenBefore);
            }
        } else if (hit && h.alert) {
            add("Watching it (just seen)");
        }
        if (target.nServices) {
            char sv[32] = "Services:";
            for (int i = 0; i < target.nServices; i++)
                snprintf(sv + strlen(sv), sizeof(sv) - strlen(sv), " %04X", target.services[i]);
            add("%s", sv);
        }
        int top = gy + gh + 2, rows = (y + d->height() - top) / lh;
        if (detailTop > n - rows)
            detailTop = n - rows > 0 ? n - rows : 0;
        for (int i = 0; i < rows && detailTop + i < n; i++) {
            int li = detailTop + i, ly = top + i * lh;
            d->drawString(x + 2, ly, lines[li]);
            if (li == makerLine) { // only the maker name scrolls
                int lw = textW(d, lines[li]);
                drawMaker(d, x + 2 + lw, ly, d->width() - 4 - lw, m[0] ? m : "unknown", true, millis() - scrollFrom);
            }
        }
    }

    // a message in a box over the screen
    void drawBox(OLEDDisplay *d, int16_t x, int16_t y, const char *text)
    {
        int lh = fontHeight(FONT_SMALL), lines = 1;
        for (const char *c = text; *c; c++)
            lines += *c == '\n';
        int h = lines * lh + 6, bx = x + 4, bw = d->width() - 8, by = y + (d->height() - h) / 2;
        d->setColor(BLACK);
        d->fillRect(bx, by, bw, h);
        d->setColor(WHITE);
        d->drawRect(bx, by, bw, h);
        d->drawRect(bx + 1, by + 1, bw - 2, h - 2);
        char line[64];
        const char *c = text;
        for (int i = 0; i < lines; i++) {
            const char *e = strchr(c, '\n');
            int len = e ? (int)(e - c) : (int)strlen(c);
            snprintf(line, sizeof(line), "%.*s", len, c);
            fitText(d, line, bw - 6);
            drawCentered(d, x, by + 3 + i * lh, line, FONT_SMALL);
            c = e ? e + 1 : c + len;
        }
    }
};

static RadioHuntApp &radioHuntApp()
{
    static RadioHuntApp *p = psramNew<RadioHuntApp>();
    return *p;
}

void openRadioHunt(bool bluetooth)
{
    radioHuntApp().setKind(bluetooth ? SCAN_BLE : SCAN_WIFI);
    appOpen(&radioHuntApp());
}

void openEverythingHunt()
{
    radioHuntApp().setKind(SCAN_ALL);
    appOpen(&radioHuntApp());
}

void openHuntFor(bool bluetooth, const uint8_t mac[6], bool publicAddr)
{
    radioHuntApp().setKind(bluetooth ? SCAN_BLE : SCAN_WIFI);
    radioHuntApp().startOn(mac, publicAddr);
    appOpen(&radioHuntApp());
}

} // namespace badge

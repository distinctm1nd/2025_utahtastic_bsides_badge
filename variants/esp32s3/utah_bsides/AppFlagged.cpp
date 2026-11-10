// Flagged devices: every marked device the scans came across, one row each: what's near now
// (and anything following you) first, then the rest of the sightings log, newest first, plus
// devices you've trusted, nicknamed or put on the watchlist.
//   up/down pick (in details: scroll), # details (and again: hunt it), push right: the device's menu,
//   * back / done
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeDeviceMenu.h"
#include "BadgeDevices.h"
#include "BadgeFollow.h"
#include "BadgeListFilter.h"
#include "BadgeLog.h"
#include "BadgeTime.h"

#include "gps/RTC.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace badge
{

struct Flagged {
    uint8_t mac[6];
    char code[6], title[44], name[34];
    bool near, following, moved, wifiKnown, wifi;
    int8_t best;
    int8_t trend; // recent-vs-baseline RSSI, dB: >0 getting closer
    int times;        // SEEN / AGAIN entries in the log
    uint32_t lastAgo; // seconds since last seen (0 = now / unknown)
    bool agoKnown;
    uint32_t firstAgo; // seconds since first seen (the oldest sighting the badge has)
    bool firstKnown;
};

static const int MAX_ROWS = 48;
static const int LOG_LINES = 160;

static bool parseMac(const char *s, uint8_t out[6])
{
    unsigned v[6];
    if (sscanf(s, "%2x:%2x:%2x:%2x:%2x:%2x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6)
        return false;
    bool zero = true;
    for (int i = 0; i < 6; i++) {
        out[i] = (uint8_t)v[i];
        zero = zero && !v[i];
    }
    return !zero;
}

static void agoText(char *b, size_t n, uint32_t s)
{
    if (s < 60)
        snprintf(b, n, "now");
    else if (s < 3600)
        snprintf(b, n, "%um ago", (unsigned)(s / 60));
    else if (s < 86400)
        snprintf(b, n, "%uh ago", (unsigned)(s / 3600));
    else
        snprintf(b, n, "%ud ago", (unsigned)(s / 86400));
}

// "14:02 (12m ago)" when the badge knows the time, else "12m ago"
static void clockAgo(char *b, size_t n, uint32_t agoS)
{
    char a[16];
    agoText(a, sizeof(a), agoS);
    if (timeKnown() && agoS >= 60) {
        uint32_t t = getValidTime(RTCQualityFromNet, true) - agoS;
        snprintf(b, n, "%02u:%02u (%s)", (unsigned)(t % 86400 / 3600), (unsigned)(t % 3600 / 60), a);
    } else {
        snprintf(b, n, "%s", a);
    }
}

class FlaggedApp : public App
{
  public:
    const char *name() const override { return "Flagged devices"; }
    uint32_t frameMs() const override { return 2000; }
    bool numberShortcuts() const override { return !filter.editing(); }
    bool typingLights() const override { return filter.editing(); }
    void begin() override
    {
        detail = false;
        menu.open = false;
        ignoreStarRelease = false;
        filter.clear();
        build();
        sel = 0;
        if (haveStart) { // back from the nickname editor: on the same device
            haveStart = false;
            for (int i = 0; i < n; i++)
                if (!memcmp(rows[i].mac, startMac, 6))
                    sel = i;
        }
    }
    void end() override
    {
        free(lines);
        lines = nullptr;
    }
    void startOn(const uint8_t mac[6])
    {
        memcpy(startMac, mac, 6);
        haveStart = true;
    }
    void key(int k, KeyAction a) override
    {
        if (k == AK_STAR && a == KA_RELEASE && ignoreStarRelease) {
            ignoreStarRelease = false;
            return;
        }
        if (filter.editing()) {
            filter.key(k, a, millis());
            rebuildVis();
            appRedraw();
            return;
        }
        if (menu.open) {
            DeviceMenu::Act act = menu.key(k, a);
            if (act == DeviceMenu::A_NICKNAME)
                deviceNicknameEdit(menu.mac, menu.wifi, [](const uint8_t mac[6], bool) { openFlaggedAt(mac); });
            else if (act == DeviceMenu::A_EXPLORE)
                openBleExplorer(menu.mac, menu.publicAddr, devName(menu.mac, ""),
                                [](const uint8_t mac[6], bool) { openFlaggedAt(mac); });
            appRedraw();
            return;
        }
        if (k == AK_RIGHT && a == KA_PRESS && vcount) { // push right: the device's menu (as in the scanner)
            const Flagged &r = rows[vis[sel]];
            menu.show(r.mac, r.wifiKnown && r.wifi, !strcmp(r.code, "TAG"), false, devName(r.mac, r.name[0] ? r.name : r.title));
            appRedraw();
            return;
        }
        if ((a == KA_PRESS || a == KA_REPEAT) && !detail && k == AK_0) {
            filter.open();
        } else if ((a == KA_PRESS || a == KA_REPEAT) && vcount && !detail) {
            if (k == AK_UP || k == AK_2)
                sel = (sel + vcount - 1) % vcount;
            else if (k == AK_DOWN || k == AK_8)
                sel = (sel + 1) % vcount;
        } else if ((a == KA_PRESS || a == KA_REPEAT) && detail) { // scroll the details
            if ((k == AK_UP || k == AK_2) && detailTop > 0)
                detailTop--;
            else if (k == AK_DOWN || k == AK_8)
                detailTop++; // clamped when drawn
        }
        if (a != KA_RELEASE)
            return;
        if (k == AK_STAR) {
            if (detail)
                detail = false;
            else
                appClose();
        } else if ((k == AK_HASH || k == AK_5) && vcount) {
            if (!detail) {
                detail = true;
                detailTop = 0;
            } else { // hunt it: by the scan it was seen in (Bluetooth if the log doesn't say)
                const Flagged &r = rows[vis[sel]];
                openHuntFor(!(r.wifiKnown ? r.wifi : guessWifi(r.code)), r.mac, false);
            }
        }
        appRedraw();
    }
    void tick(uint32_t now) override
    {
        filter.tick(now);
        if (!detail && !menu.open && !filter.editing() && now - builtAt > 10000) // keep "near now" fresh
            build();
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        if (menu.open) {
            menu.draw(d, x, y);
            return;
        }
        if (detail && vcount)
            drawDetail(d, x, y, rows[vis[sel]]);
        else
            drawList(d, x, y);
    }

  private:
    Flagged rows[MAX_ROWS];
    int vis[MAX_ROWS];
    int n = 0, vcount = 0, sel = 0;
    ListFilter filter;
    bool detail = false, ignoreStarRelease = false, haveStart = false;
    int detailTop = 0;
    uint8_t startMac[6] = {};
    uint32_t builtAt = 0;
    LogLine *lines = nullptr;
    DeviceMenu menu;

    static bool guessWifi(const char *code) { return !strcmp(code, "TWIN") || !strcmp(code, "ATK"); }

    Flagged *row(const uint8_t mac[6])
    {
        for (int i = 0; i < n; i++)
            if (!memcmp(rows[i].mac, mac, 6))
                return &rows[i];
        if (n >= MAX_ROWS)
            return nullptr;
        Flagged &r = rows[n++];
        memset(&r, 0, sizeof(r));
        memcpy(r.mac, mac, 6);
        r.best = -128;
        return &r;
    }

    void build()
    {
        builtAt = millis();
        n = 0;
        // near now, from the watch
        static TrackedView tv[16];
        int nt = watchTrackedList(tv, 16);
        for (int i = 0; i < nt; i++) {
            Flagged *r = row(tv[i].mac);
            if (!r)
                break;
            snprintf(r->code, sizeof(r->code), "%s", tv[i].code ? tv[i].code : "");
            snprintf(r->title, sizeof(r->title), "%s", tv[i].title ? tv[i].title : "");
            r->near = tv[i].agoMs < 2 * 60 * 1000;
            r->following = tv[i].following;
            r->moved = tv[i].moved;
            r->best = tv[i].best;
            r->trend = tv[i].trend;
            r->wifiKnown = true;
            r->wifi = tv[i].wifi;
            r->lastAgo = tv[i].agoMs / 1000;
            r->agoKnown = true;
            r->firstAgo = (tv[i].agoMs + tv[i].withYouMs) / 1000;
            r->firstKnown = true;
        }
        // the sightings log, newest first
        if (!lines)
            lines = (LogLine *)malloc(sizeof(LogLine) * LOG_LINES);
        int nl = lines ? logReadRecent(lines, LOG_LINES) : 0;
        uint32_t nowEpoch = timeKnown() ? getValidTime(RTCQualityFromNet) : 0;
        for (int i = 0; i < nl; i++) {
            char buf[100], *f[11];
            snprintf(buf, sizeof(buf), "%s", lines[i].text);
            int nf = 0;
            for (char *p = buf; nf < 11; nf++) {
                f[nf] = p;
                char *c = strchr(p, ',');
                if (!c) {
                    nf++;
                    break;
                }
                *c = 0;
                p = c + 1;
            }
            uint8_t mac[6];
            if (nf < 10 || !parseMac(f[5], mac) || !strcmp(f[3], "MESH") || !strcmp(f[3], "ATTACK"))
                continue;
            Flagged *r = row(mac);
            if (!r)
                break;
            if (!r->code[0])
                snprintf(r->code, sizeof(r->code), "%s", f[4]);
            if (!r->title[0])
                snprintf(r->title, sizeof(r->title), "%s", f[9]);
            if (!r->name[0] && nf > 10 && (!strcmp(f[3], "SEEN") || !strcmp(f[3], "AGAIN")))
                snprintf(r->name, sizeof(r->name), "%s", f[10]);
            if (!strcmp(f[3], "SEEN") || !strcmp(f[3], "AGAIN"))
                r->times++;
            if (!strcmp(f[3], "FOLW"))
                r->following = r->following || r->near;
            if (!strcmp(f[3], "MOVED"))
                r->moved = true;
            int rssi = atoi(f[6]);
            if (rssi > r->best && rssi < 0)
                r->best = (int8_t)rssi;
            uint32_t epoch = strtoul(f[0], nullptr, 10);
            if (epoch && nowEpoch >= epoch) {
                if (!r->agoKnown) { // the log is newest first: the first match is the last sighting
                    r->lastAgo = nowEpoch - epoch;
                    r->agoKnown = true;
                }
                if (!r->firstKnown || nowEpoch - epoch > r->firstAgo) { // ...and the last one the first
                    r->firstAgo = nowEpoch - epoch;
                    r->firstKnown = true;
                }
            }
        }
        // devices you told the badge about that aren't in the log
        for (int i = 0; i < devCount() && n < MAX_ROWS; i++) {
            const DevMemo &d = devAt(i);
            Flagged *r = row(d.mac);
            if (r && !r->title[0])
                snprintf(r->title, sizeof(r->title), "%s", d.flags & (DEV_WATCH_HERE | DEV_WATCH_GONE) ? "(watched)" : "(saved)");
            if (r && !r->wifiKnown) {
                r->wifiKnown = true;
                r->wifi = d.flags & DEV_WIFI;
            }
        }
        // following first, then near, then the rest in log order
        for (int a = 1; a < n; a++)
            for (int b = a; b > 0; b--) {
                int ra = rows[b].following * 2 + rows[b].near, rb = rows[b - 1].following * 2 + rows[b - 1].near;
                if (ra <= rb)
                    break;
                Flagged t = rows[b];
                rows[b] = rows[b - 1];
                rows[b - 1] = t;
            }
        rebuildVis();
    }

    const char *rowHay(const Flagged &r)
    {
        static char hay[96];
        const char *nm = devName(r.mac, r.name[0] ? r.name : r.title);
        snprintf(hay, sizeof(hay), "%s %s %s %02x%02x%02x%02x%02x%02x", r.code, nm, r.title, r.mac[0], r.mac[1], r.mac[2],
                 r.mac[3], r.mac[4], r.mac[5]);
        for (char *c = hay; *c; c++)
            *c = (char)tolower(*c);
        return hay;
    }
    void rebuildVis()
    {
        vcount = 0;
        for (int i = 0; i < n; i++)
            if (filter.match(rowHay(rows[i])))
                vis[vcount++] = i;
        if (sel >= vcount)
            sel = vcount ? vcount - 1 : 0;
    }

    void drawList(OLEDDisplay *d, int16_t x, int16_t y)
    {
        char t[32];
        snprintf(t, sizeof(t), "Flagged (%d)", vcount);
        drawTitle(d, x, y, t, filter.active() ? "#info 0find" : "#info 0find *done");
        int lh = fontHeight(FONT_SMALL), W = d->width();
        if (!vcount) {
            drawCentered(d, x, y + 2 * lh, filter.active() ? "No match" : "Nothing flagged yet", FONT_SMALL);
            if (filter.active())
                filter.draw(d, x, y);
            else {
                drawCentered(d, x, y + 3 * lh + 4, "(run a WiFi / Bluetooth scan", FONT_SMALL);
                drawCentered(d, x, y + 4 * lh + 4, "or the background watch)", FONT_SMALL);
            }
            return;
        }
        int botRes = filter.active() ? lh + 2 : 0;
        int rows_ = (d->height() - lh - 2 - botRes) / lh, first = sel - rows_ + 1 > 0 ? sel - rows_ + 1 : 0;
        for (int i = 0; i < rows_ && first + i < vcount; i++) {
            const Flagged &r = rows[vis[first + i]];
            int ry = y + lh + 2 + i * lh;
            char right[16], left[64];
            if (r.following)
                snprintf(right, sizeof(right), "FOLW");
            else if (r.near)
                snprintf(right, sizeof(right), "near");
            else if (r.agoKnown)
                agoText(right, sizeof(right), r.lastAgo);
            else
                right[0] = 0;
            const char *nm = devName(r.mac, r.name[0] ? r.name : r.title);
            snprintf(left, sizeof(left), "%s%-4s %s", devTrusted(r.mac) ? "~" : "", r.code, nm);
            d->setFont(FONT_SMALL);
            int rw = right[0] ? d->getStringWidth(right) + 4 : 0, len = strlen(left);
            while (len > 1 && d->getStringWidth(left, len) > W - rw - 4)
                left[--len] = 0;
            if (first + i == sel) {
                d->fillRect(x, ry, W, lh);
                d->setColor(BLACK);
            }
            d->setTextAlignment(TEXT_ALIGN_LEFT);
            d->drawString(x + 2, ry, left);
            if (right[0]) {
                d->setTextAlignment(TEXT_ALIGN_RIGHT);
                d->drawString(x + W - 2, ry, right);
                d->setTextAlignment(TEXT_ALIGN_LEFT);
            }
            d->setColor(WHITE);
        }
        if (filter.active())
            filter.draw(d, x, y);
    }

    void drawDetail(OLEDDisplay *d, int16_t x, int16_t y, const Flagged &r)
    {
        drawTitle(d, x, y, r.code[0] ? r.code : "Device", "#hunt *back");
        int lh = fontHeight(FONT_SMALL), nl = 0;
        static char text[12][72]; // gathered first, then the part that fits, scrolled
        char b[72], ago[32];
        auto line = [&](const char *s) {
            if (nl < 12)
                snprintf(text[nl++], sizeof(text[0]), "%s", s);
        };
        const DevMemo *m = devFind(r.mac);
        line(r.title);
        snprintf(b, sizeof(b), "Name: %s", m && m->nick[0] ? m->nick : r.name[0] ? r.name : "(none)");
        line(b);
        snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", r.mac[0], r.mac[1], r.mac[2], r.mac[3], r.mac[4], r.mac[5]);
        line(b);
        snprintf(b, sizeof(b), "%s, seen %d time%s",
                 r.following ? "Following you"
                 : r.near    ? "Near now"
                             : "Not near now",
                 r.times, r.times == 1 ? "" : "s");
        line(b);
        if (r.firstKnown) {
            clockAgo(ago, sizeof(ago), r.firstAgo);
            snprintf(b, sizeof(b), "First seen %s", ago);
            line(b);
        }
        if (r.agoKnown) {
            clockAgo(ago, sizeof(ago), r.lastAgo);
            snprintf(b, sizeof(b), "Last seen %s", ago);
            line(b);
        } else if (!r.firstKnown) {
            line("Seen: time unknown (no clock then)");
        }
        const char *trendTxt = (r.near && r.trend >= 6)    ? ", getting closer"
                               : (r.near && r.trend <= -6)  ? ", moving away"
                                                            : "";
        snprintf(b, sizeof(b), "Strongest: %d dBm%s%s", r.best, r.moved ? ", moved with you" : "", trendTxt);
        line(b);
        snprintf(b, sizeof(b), "%s%s%s", m && (m->flags & DEV_TRUSTED) ? "Trusted  " : "",
                 m && (m->flags & DEV_WATCH_HERE) ? "Watch: turns up  " : "",
                 m && (m->flags & DEV_WATCH_GONE) ? "Watch: gone" : "");
        if (b[0])
            line(b);
        line("right: nickname, trust, watch, beep");
        int rows_ = (d->height() - lh - 2) / lh;
        if (detailTop > nl - rows_)
            detailTop = nl - rows_ > 0 ? nl - rows_ : 0;
        d->setFont(FONT_SMALL);
        d->setTextAlignment(TEXT_ALIGN_LEFT);
        for (int i = 0; i < rows_ && detailTop + i < nl; i++) {
            char t[72];
            snprintf(t, sizeof(t), "%s", text[detailTop + i]);
            int len = strlen(t);
            while (len > 1 && d->getStringWidth(t, len) > d->width() - 10)
                t[--len] = 0;
            d->drawString(x + 2, y + lh + 2 + i * lh, t);
        }
        int W = d->width(), H = d->height(); // arrows when there's more above / below
        if (detailTop > 0)
            d->fillTriangle(x + W - 7, y + lh + 8, x + W - 2, y + lh + 8, x + W - 5, y + lh + 4);
        if (detailTop + rows_ < nl)
            d->fillTriangle(x + W - 7, y + H - 6, x + W - 2, y + H - 6, x + W - 5, y + H - 2);
    }
};

static FlaggedApp &flaggedApp()
{
    static FlaggedApp *p = psramNew<FlaggedApp>();
    return *p;
}

void openFlagged()
{
    appOpen(&flaggedApp());
}

void openFlaggedAt(const uint8_t mac[6])
{
    flaggedApp().startOn(mac);
    appOpen(&flaggedApp());
}

} // namespace badge

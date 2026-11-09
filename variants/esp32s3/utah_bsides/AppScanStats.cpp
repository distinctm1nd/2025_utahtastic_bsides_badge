// Scan stats: a glanceable summary of what's on the air around the badge, so you don't need a
// laptop. One radio, so it shows WiFi or Bluetooth at a time (# / left-right switches). Numbers
// are "around you now" (what the scan has heard in the last ~30 s), not an all-time total.
//
//   # (or left / right) WiFi <-> Bluetooth, * done
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
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
    void begin() override
    {
        wifi = true;
        error = radioScanStart(SCAN_WIFI);
    }
    void end() override { radioScanStop(); }
    void tick(uint32_t) override { radioScanPoll(); }
    void key(int k, KeyAction a) override
    {
        if (k == AK_STAR && a == KA_RELEASE) {
            appClose();
            return;
        }
        if (a != KA_PRESS)
            return;
        if (k == AK_HASH || k == AK_5 || k == AK_LEFT || k == AK_RIGHT || k == AK_4 || k == AK_6) {
            wifi = !wifi;
            radioScanStop();
            error = radioScanStart(wifi ? SCAN_WIFI : SCAN_BLE);
        }
        appRedraw();
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        int lh = fontHeight(FONT_SMALL);
        drawTitle(d, x, y, wifi ? "Scan stats: WiFi" : "Scan stats: Bluetooth", "#swap *done");
        if (error) {
            drawCentered(d, x, y + 2 * lh, error, FONT_SMALL);
            return;
        }
        int n = radioScanSnapshot(list, MAXLIST);
        d->setFont(FONT_SMALL);
        d->setTextAlignment(TEXT_ALIGN_LEFT);
        int ry = y + lh + 2;
        char b[48];
        if (wifi)
            drawWifi(d, x, ry, lh, list, n, b, sizeof(b));
        else
            drawBle(d, x, ry, lh, list, n, b, sizeof(b));
    }

  private:
    bool wifi = true;
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
        snprintf(b, nb, "Twins %d Drones %d Karma %d", twin, drone, karma);
        d->drawString(x + 2, ry + 3 * lh, b);
        if (deauth)
            snprintf(b, nb, "! Deauths %d (ch %d, 10s)", deauth, dchan);
        else
            snprintf(b, nb, "Deauths: none");
        d->drawString(x + 2, ry + 4 * lh, b);
        snprintf(b, nb, "Flagged %d   Pwn %d", alert, radioPwnagotchiRecent());
        d->drawString(x + 2, ry + 5 * lh, b);
        char top[64];
        topVendors(v, vn, top, sizeof(top));
        snprintf(b, nb, "Top: %s", top[0] ? top : "-");
        fitText(d, b, d->width() - 4);
        d->drawString(x + 2, ry + 6 * lh, b);
    }

    void drawBle(OLEDDisplay *d, int16_t x, int ry, int lh, const ScanEntry *list, int n, char *b, size_t nb)
    {
        int pub = 0, named = 0, trackers = 0, findmy = 0, away = 0, alert = 0;
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
            if (e.flags & (SF_FINDMY | SF_FMDN))
                findmy++;
            if (e.flags & SF_AWAY)
                away++;
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
        char top[64];
        topVendors(v, vn, top, sizeof(top));
        snprintf(b, nb, "Top: %s", top[0] ? top : "-");
        fitText(d, b, d->width() - 4);
        d->drawString(x + 2, ry + 6 * lh, b);
    }
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

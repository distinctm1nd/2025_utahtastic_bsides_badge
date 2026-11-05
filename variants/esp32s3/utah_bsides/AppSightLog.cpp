// Sightings log: what the fox hunt watch wrote to /badge/sightings.csv, newest first.
//
//   joystick (or 2 / 8) scroll, # download it over Badge WiFi, hold * clear (asks first), * done
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeListFilter.h"
#include "BadgeLog.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace badge
{

static const int MAX_LINES = 80;

class SightLogApp : public App
{
  public:
    bool numberShortcuts() const override { return !filter.editing(); }
    bool typingLights() const override { return filter.editing(); }
    const char *name() const override { return "Sightings log"; }
    uint32_t frameMs() const override { return 1000; }
    void begin() override
    {
        if (!lines)
            lines = (LogLine *)malloc(MAX_LINES * sizeof(LogLine)); // big: lands in PSRAM
        count = lines ? logReadRecent(lines, MAX_LINES) : 0;
        top = 0;
        confirm = false;
        filter.clear();
        rebuildVis();
    }
    void tick(uint32_t now) override { filter.tick(now); }
    void end() override
    {
        free(lines);
        lines = nullptr;
    }
    void key(int k, KeyAction a) override
    {
        if (k == AK_STAR && a == KA_RELEASE && starHeld) { // the end of the hold that asked
            starHeld = false;
            return;
        }
        if (filter.editing()) {
            filter.key(k, a, millis());
            rebuildVis();
            appRedraw();
            return;
        }
        if (confirm) {
            if (a == KA_PRESS && (k == AK_HASH || k == AK_5)) {
                logClear();
                count = 0;
                rebuildVis();
            }
            if (a == KA_PRESS)
                confirm = false;
            appRedraw();
            return;
        }
        if (k == AK_STAR && a == KA_HOLD) { // hold *: clear it (asks first)
            confirm = true;
            starHeld = true;
            appRedraw();
            return;
        }
        if (k == AK_STAR && a == KA_RELEASE) {
            if (starHeld)
                starHeld = false; // the end of the hold that asked
            else
                appClose();
            return;
        }
        else if ((k == AK_HASH || k == AK_5) && a == KA_PRESS)
            openBadgeWifi();
        else if (k == AK_0 && a == KA_PRESS)
            filter.open();
        else if ((a == KA_PRESS || a == KA_REPEAT) && (k == AK_DOWN || k == AK_8) && top < vcount - 1)
            top++;
        else if ((a == KA_PRESS || a == KA_REPEAT) && (k == AK_UP || k == AK_2) && top > 0)
            top--;
        appRedraw();
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        int lh = fontHeight(FONT_SMALL);
        char title[32];
        snprintf(title, sizeof(title), "Sightings (%d)", vcount);
        drawTitle(d, x, y, title, filter.active() ? "#WiFi 0find" : "#WiFi 0find *done");
        if (confirm) {
            drawCentered(d, x, y + 2 * lh, "Clear the whole log?", FONT_SMALL);
            drawCentered(d, x, y + 4 * lh, "# yes   any other key no", FONT_SMALL);
            return;
        }
        if (!vcount) {
            drawCentered(d, x, y + 2 * lh, filter.active() ? "No match" : "Nothing logged yet", FONT_SMALL);
            if (filter.active())
                filter.draw(d, x, y);
            else
                drawCentered(d, x, y + 3 * lh + 4, "(marked devices go here)", FONT_SMALL);
            return;
        }
        d->setFont(FONT_SMALL);
        d->setTextAlignment(TEXT_ALIGN_LEFT);
        int botRes = filter.active() ? lh + 2 : 0;
        int ry = y + lh + 2;
        for (int vi = top; vi < vcount && ry + 2 * lh <= y + d->height() - botRes; vi++, ry += 2 * lh + 1) {
            char a[64], b[64];
            format(lines[vis[vi]].text, a, sizeof(a), b, sizeof(b));
            d->drawString(x + 2, ry, a);
            d->drawString(x + 10, ry + lh, b);
        }
        if (filter.active())
            filter.draw(d, x, y);
    }

  private:
    bool starHeld = false;
    LogLine *lines = nullptr;
    int count = 0, top = 0;
    bool confirm = false;
    ListFilter filter;
    int vis[MAX_LINES];
    int vcount = 0;

    void rebuildVis()
    {
        vcount = 0;
        for (int i = 0; i < count; i++)
            if (filter.match(lines[i].text))
                vis[vcount++] = i;
        if (top >= vcount)
            top = vcount ? vcount - 1 : 0;
    }

    // "14:05 SEEN SURV Flock plate reader" / "B4:1E:52:.. -60 extra"
    static void format(const char *csv, char *a, size_t na, char *b, size_t nb)
    {
        const char *f[11] = {};
        char buf[100];
        snprintf(buf, sizeof(buf), "%s", csv);
        int n = 0;
        for (char *p = buf; n < 11;) {
            f[n++] = p;
            char *c = strchr(p, ',');
            if (!c)
                break;
            *c = 0;
            p = c + 1;
        }
        for (int i = n; i < 11; i++)
            f[i] = "";
        char when[16];
        time_t t = (time_t)strtoul(f[0], nullptr, 10);
        if (t) {
            struct tm tm;
            localtime_r(&t, &tm);
            strftime(when, sizeof(when), "%m/%d %H:%M", &tm);
        } else {
            snprintf(when, sizeof(when), "boot %s", f[1]);
        }
        snprintf(a, na, "%s %s %s %s", when, f[3], f[4], f[9]);
        bool here = strcmp(f[7], "0") || strcmp(f[8], "0");
        snprintf(b, nb, "%s %s %s%s", f[5], f[6], f[10], here ? " @" : "");
    }
};

static SightLogApp sightLogApp;

void openSightingsLog()
{
    appOpen(&sightLogApp);
}

} // namespace badge

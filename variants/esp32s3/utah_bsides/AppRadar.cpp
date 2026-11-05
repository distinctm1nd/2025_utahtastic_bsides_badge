// Node radar: the badge in the middle, the nodes that share their position around it (north
// up), with range rings. Needs the badge's own position: shared from the phone (Meshtastic app
// -> provide phone location) or a fixed position set in the app.
//
//   joystick up / down zoom, left / right pick a node, * done
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgePositions.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>

namespace badge
{

static const float SCALES[] = {100, 250, 500, 1000, 2000, 5000, 10000, 25000, 50000, 100000, 250000};
static const int NSCALES = sizeof(SCALES) / sizeof(SCALES[0]);
static const int MAXN = 60;

static void distText(char *b, size_t n, float m)
{
    if (m < 1000)
        snprintf(b, n, "%d m", (int)m);
    else if (m < 10000)
        snprintf(b, n, "%.1f km", m / 1000);
    else
        snprintf(b, n, "%d km", (int)(m / 1000));
}

class RadarApp : public App
{
  public:
    bool numberShortcuts() const override { return true; }
    const char *name() const override { return "Node radar"; }
    uint32_t frameMs() const override { return 2000; }
    bool keepAwake() const override { return true; }
    void begin() override
    {
        refresh();
        sel = 0;
        // start zoomed to show the nearest few nodes
        scale = NSCALES - 1;
        float want = count ? list[count < 5 ? count - 1 : 4].distM : 1000;
        for (int i = 0; i < NSCALES; i++)
            if (SCALES[i] >= want) {
                scale = i;
                break;
            }
    }
    void key(int k, KeyAction a) override
    {
        if (k == AK_STAR && a == KA_RELEASE) {
            appClose();
            return;
        }
        if (a != KA_PRESS && a != KA_REPEAT)
            return;
        if ((k == AK_UP || k == AK_2) && scale > 0)
            scale--;
        else if ((k == AK_DOWN || k == AK_8) && scale < NSCALES - 1)
            scale++;
        else if ((k == AK_RIGHT || k == AK_6) && count)
            sel = (sel + 1) % count;
        else if ((k == AK_LEFT || k == AK_4) && count)
            sel = (sel + count - 1) % count;
        appRedraw();
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        refresh();
        int lh = fontHeight(FONT_SMALL);
        char b[48], t[24];
        distText(t, sizeof(t), SCALES[scale]);
        snprintf(b, sizeof(b), "Radar %s", t);
        drawTitle(d, x, y, b, "*done");
        if (!mine) {
            static const char *lines[] = {"The badge doesn't know", "where it is. In the", "Meshtastic app: share",
                                          "the phone's location,", "or set a fixed position."};
            for (int i = 0; i < 5; i++)
                drawCentered(d, x, y + lh + 6 + i * lh, lines[i], FONT_SMALL);
            return;
        }
        // the scope: rings at half and full range, north marked, the badge in the middle
        int R = 46, cx = x + 50, cy = y + lh + 4 + R;
        d->drawCircle(cx, cy, R);
        for (int a = 0; a < 360; a += 12) { // dotted half-range ring
            float r = a * M_PI / 180;
            d->setPixel(cx + (int)(R / 2 * sin(r)), cy - (int)(R / 2 * cos(r)));
        }
        d->setFont(FONT_SMALL);
        d->setTextAlignment(TEXT_ALIGN_CENTER);
        d->drawString(cx, cy - R - 2, "N");
        d->setTextAlignment(TEXT_ALIGN_LEFT);
        d->drawLine(cx - 2, cy, cx + 2, cy);
        d->drawLine(cx, cy - 2, cx, cy + 2);
        for (int i = 0; i < count; i++) {
            float f = list[i].distM / SCALES[scale];
            bool beyond = f > 1;
            if (beyond)
                f = 1;
            float r = list[i].bearing * M_PI / 180;
            int px = cx + (int)(R * f * sin(r)), py = cy - (int)(R * f * cos(r));
            if (beyond)
                d->drawCircle(px, py, 1); // off the scale: on the edge, hollow
            else
                d->fillRect(px - 1, py - 1, 3, 3);
            if (i == sel)
                d->drawRect(px - 3, py - 3, 7, 7);
        }
        // the picked node
        int tx = x + 100, ty = y + lh + 4;
        if (!count) {
            d->drawString(tx, ty, "No nodes");
            d->drawString(tx, ty + lh, "share their");
            d->drawString(tx, ty + 2 * lh, "position yet");
            return;
        }
        const NodePos &p = list[sel];
        snprintf(b, sizeof(b), "%s", p.name);
        int len = strlen(b);
        while (len > 1 && d->getStringWidth(b, len) > d->width() - tx - 1)
            b[--len] = 0;
        d->drawString(tx, ty, b);
        distText(t, sizeof(t), p.distM);
        d->drawString(tx, ty + lh, t);
        snprintf(b, sizeof(b), "%s %d\xb0", posCompass(p.bearing), (int)p.bearing);
        d->drawString(tx, ty + 2 * lh, b);
        if (p.ageS)
            snprintf(b, sizeof(b), p.ageS < 3600 ? "%u min ago" : "%u h ago", p.ageS < 3600 ? (unsigned)(p.ageS / 60) : (unsigned)(p.ageS / 3600));
        else
            snprintf(b, sizeof(b), "heard: ?");
        d->drawString(tx, ty + 3 * lh, b);
        if (p.hops >= 0) {
            snprintf(b, sizeof(b), p.hops ? "%d hop%s" : "direct", p.hops, p.hops == 1 ? "" : "s");
            d->drawString(tx, ty + 4 * lh, b);
        }
        snprintf(b, sizeof(b), "%d/%d", sel + 1, count);
        d->drawString(tx, ty + 5 * lh, b);
        drawFooter(d, x, y, "");
    }

  private:
    NodePos list[MAXN];
    int count = 0, sel = 0, scale = 3;
    bool mine = false;

    void refresh()
    {
        int32_t la, lo;
        mine = posMine(la, lo);
        uint32_t keep = count && sel < count ? list[sel].num : 0;
        count = posList(list, MAXN);
        sel = 0;
        for (int i = 0; i < count; i++)
            if (list[i].num == keep)
                sel = i;
    }
};

static RadarApp &radarApp()
{
    static RadarApp *p = psramNew<RadarApp>();
    return *p;
}

void openRadar()
{
    appOpen(&radarApp());
}

} // namespace badge

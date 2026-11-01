// Lights: pick the LED scene with a live preview, plus speed, brightness and light shows.
// A small map of the badge on the right shows which LEDs are lit.
//
// Same keys as every other list on the badge:
//   joystick up/left = previous, down/right = next, centre (or #) = select, * = back
// Selecting "Scene" opens the scene list: the LEDs change as you scroll; select keeps the
// scene, * goes back to the one you had. Selecting any other row changes it.
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeLeds.h"
#include "BadgeMesh.h"
#include "BadgeScenes.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <stdio.h>

extern graphics::Screen *screen;

namespace badge
{

class LightsApp : public App
{
    enum Row { R_SCENE, R_SPEED, R_BRIGHT, R_MOON, R_KEYFX, R_JOIN, R_LEAD, R_SHARE, R_COUNT };

  public:
    const char *name() const override { return "Lights"; }
    uint32_t frameMs() const override { return 150; } // keep the LED map moving
    void begin() override
    {
        row = R_SCENE;
        picking = false;
    }

    void key(int k, KeyAction a) override
    {
        if (a == KA_RELEASE && k == AK_STAR) {
            if (picking) { // back out of the scene list: restore the scene you had
                picking = false;
                if (ledsScene() != savedScene)
                    ledsSetScene(savedScene);
            } else {
                appClose();
            }
            return;
        }
        bool prev = k == AK_UP || k == AK_LEFT, next = k == AK_DOWN || k == AK_RIGHT;
        if ((prev || next) && (a == KA_PRESS || a == KA_REPEAT)) {
            int dir = next ? 1 : -1;
            if (picking) // live preview, not saved until you select
                ledsSetScene((ledsScene() + dir + sceneCount()) % sceneCount(), false);
            else
                row = (row + dir + R_COUNT) % R_COUNT;
            return;
        }
        if (k == AK_HASH && a == KA_PRESS) {
            if (picking) {
                picking = false;
                ledsSetScene(ledsScene()); // keep it (saves)
            } else if (row == R_SCENE) {
                picking = true;
                savedScene = ledsScene();
            } else {
                change(+1);
            }
        }
    }

    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        if (picking) {
            drawPicker(d, x, y);
            return;
        }
        drawTitle(d, x, y, "Lights", "*back");
        int lh = fontHeight(FONT_SMALL);
        int top = y + lh + 2;
        int listW = d->width() - 58;
        // show as many rows as fit, keeping the selected one visible
        int rows = (d->height() - lh - 2) / lh;
        int first = row >= rows ? row - rows + 1 : 0;
        char buf[48];
        for (int r = first; r < R_COUNT && r < first + rows; r++) {
            int ry = top + (r - first) * lh;
            label(r, buf, sizeof(buf));
            if (r == row) {
                d->fillRect(x, ry, listW, lh);
                d->setColor(BLACK);
            }
            d->drawString(x + 2, ry, buf);
            d->setColor(WHITE);
        }
        drawMap(d, x + listW + 3, top, 54, d->height() - top - 2);
    }

  private:
    int row = 0;
    bool picking = false;
    int savedScene = 0;

    // the scene list: a few names around the current one, the current one highlighted
    void drawPicker(OLEDDisplay *d, int16_t x, int16_t y)
    {
        drawTitle(d, x, y, "Scene", "select / *back");
        int lh = fontHeight(FONT_SMALL);
        int top = y + lh + 2;
        int listW = d->width() - 58;
        int rows = (d->height() - lh - 2) / lh;
        int cur = ledsScene(), n = sceneCount();
        for (int r = 0; r < rows; r++) {
            int i = cur - rows / 2 + r;
            if (i < 0 || i >= n)
                continue;
            int ry = top + r * lh;
            if (i == cur) {
                d->fillRect(x, ry, listW, lh);
                d->setColor(BLACK);
            }
            d->drawString(x + 2, ry, sceneAt(i).name);
            d->setColor(WHITE);
        }
        drawMap(d, x + listW + 3, top, 54, d->height() - top - 2);
    }

    static const char *onOff(bool b) { return b ? "on" : "off"; }

    void label(int r, char *b, size_t n)
    {
        switch (r) {
        case R_SCENE: snprintf(b, n, "Scene: %s", sceneAt(ledsScene()).name); break;
        case R_SPEED: snprintf(b, n, "Speed: %s", ledsSpeedName(ledsSpeed())); break;
        case R_BRIGHT: snprintf(b, n, "Bright: %s", ledsBrightName(ledsBright())); break;
        case R_MOON: snprintf(b, n, "Moon: %s", ledsMoonFollows() ? "scene" : "app"); break;
        case R_KEYFX: snprintf(b, n, "Key flash: %s", onOff(ledsKeyFlash())); break;
        case R_JOIN: snprintf(b, n, "Join shows: %s", onOff(meshJoinShows())); break;
        case R_LEAD: snprintf(b, n, "Lead show: %s", onOff(meshLeadingShow())); break;
        default: snprintf(b, n, "Share with badges"); break;
        }
    }

    void change(int dir)
    {
        switch (row) {
        case R_SCENE: ledsSetScene((ledsScene() + dir + sceneCount()) % sceneCount()); break;
        case R_SPEED: ledsSetSpeed((ledsSpeed() + dir + ledsSpeedCount()) % ledsSpeedCount()); break;
        case R_BRIGHT: ledsSetBright((ledsBright() + dir + ledsBrightCount()) % ledsBrightCount()); break;
        case R_MOON: ledsSetMoonFollows(!ledsMoonFollows()); break;
        case R_KEYFX: ledsSetKeyFlash(!ledsKeyFlash()); break;
        case R_JOIN: meshSetJoinShows(!meshJoinShows()); break;
        case R_LEAD: meshSetLeadingShow(!meshLeadingShow()); break;
        case R_SHARE:
            meshShareLightShow();
            if (screen)
                screen->showSimpleBanner("Light show sent", 1200);
            break;
        }
    }

    // the badge's 25 LEDs at their real positions: filled = lit, ring = dark
    void drawMap(OLEDDisplay *d, int mx, int my, int mw, int mh)
    {
        const Rgb *f = ledsFrame();
        for (int i = 0; i < NUM_LEDS; i++) {
            if (LEDS[i].group == GROUP_MOON && !ledsMoonFollows())
                continue;
            int px = mx + (int)(LEDS[i].x * (mw - 4)) + 2;
            int py = my + (int)(LEDS[i].y * (mh - 4)) + 2;
            float lum = 0.3f * f[i][0] + 0.55f * f[i][1] + 0.15f * f[i][2];
            int r = LEDS[i].group == GROUP_KEYPAD ? 3 : 2;
            if (lum > 0.25f)
                d->fillCircle(px, py, r);
            else
                d->drawCircle(px, py, r);
        }
    }
};

static LightsApp lightsApp;

void openLightsApp()
{
    appOpen(&lightsApp);
}

} // namespace badge

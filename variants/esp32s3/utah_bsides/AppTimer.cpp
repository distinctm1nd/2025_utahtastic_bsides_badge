// Stopwatch and countdown timer (a talk timer). No buzzer on the badge, so time-up flashes the
// keypad LEDs and shows a banner. Two modes; # switches between them.
//
//   Stopwatch:  5 start / stop   9 lap   0 reset   # timer mode   * done
//   Timer:      2 / 8 (up/down) set minutes, 4 / 6 set seconds, 5 start / pause, 0 reset,
//               # stopwatch mode, * done
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "mesh/Throttle.h"
#include <Arduino.h>
#include <stdio.h>

namespace badge
{

static void fmt(char *b, size_t n, uint32_t ms)
{
    uint32_t t = ms / 10; // centiseconds
    unsigned cs = t % 100, s = (t / 100) % 60, m = (t / 6000) % 60, h = t / 360000;
    if (h)
        snprintf(b, n, "%u:%02u:%02u", h, m, s);
    else
        snprintf(b, n, "%02u:%02u.%02u", m, s, cs);
}

class TimerApp : public App
{
  public:
    const char *name() const override { return "Timer"; }
    uint32_t frameMs() const override { return running() ? 60 : 500; }
    bool keepAwake() const override { return running() || ringing; }
    void begin() override
    {
        mode = SW;
        swRun = false;
        swBase = 0;
        swAcc = 0;
        nLaps = 0;
        tdRun = false;
        tdSet = 5 * 60000; // 5:00 default
        tdLeft = tdSet;
        ringing = false;
    }
    bool keypadLeds(Rgb *k, uint32_t now) override
    {
        if (!ringing)
            return false;
        float on = (now / 150) % 2 ? 1.0f : 0.05f; // fast flash at time-up
        for (int i = 0; i < KEYPAD_LEDS; i++) {
            k[i][0] = on;
            k[i][1] = on * 0.25f;
            k[i][2] = 0;
        }
        return true;
    }
    void tick(uint32_t now) override
    {
        if (mode == TD && tdRun) {
            uint32_t gone = now - tdBase;
            if (gone >= tdLeftAtStart) {
                tdLeft = 0;
                tdRun = false;
                ringing = true;
                ringAt = now;
                appRedraw();
            } else {
                tdLeft = tdLeftAtStart - gone;
            }
        }
        if (ringing && !Throttle::isWithinTimespanMs(ringAt, 10000)) // stop flashing after 10 s
            ringing = false;
    }
    void key(int k, KeyAction a) override
    {
        if (ringing && a == KA_PRESS) { // any key silences the time-up flash
            ringing = false;
            if (k == AK_STAR)
                return;
        }
        if (k == AK_STAR && a == KA_RELEASE) {
            appClose();
            return;
        }
        if (a != KA_PRESS && a != KA_REPEAT)
            return;
        if ((k == AK_HASH) && a == KA_PRESS) {
            mode = mode == SW ? TD : SW;
            appRedraw();
            return;
        }
        if (mode == SW) {
            if (k == AK_5 && a == KA_PRESS) {
                if (swRun) {
                    swAcc += millis() - swBase;
                    swRun = false;
                } else {
                    swBase = millis();
                    swRun = true;
                }
            } else if (k == AK_9 && a == KA_PRESS && swRun && nLaps < MAXLAP) {
                laps[nLaps++] = elapsed();
            } else if (k == AK_0 && a == KA_PRESS) {
                swRun = false;
                swAcc = 0;
                nLaps = 0;
            }
        } else { // countdown
            if (k == AK_5 && a == KA_PRESS) {
                if (tdRun) {
                    tdRun = false; // pause
                } else if (tdLeft > 0) {
                    tdBase = millis();
                    tdLeftAtStart = tdLeft;
                    tdRun = true;
                    ringing = false;
                }
            } else if (k == AK_0 && a == KA_PRESS) {
                tdRun = false;
                tdLeft = tdSet;
            } else if (!tdRun) { // set the duration while stopped
                if (k == AK_2 || k == AK_UP)
                    adjust(60000);
                else if (k == AK_8 || k == AK_DOWN)
                    adjust(-60000);
                else if (k == AK_6 || k == AK_RIGHT)
                    adjust(5000);
                else if (k == AK_4 || k == AK_LEFT)
                    adjust(-5000);
            }
        }
        appRedraw();
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        int lh = fontHeight(FONT_SMALL), W = d->width(), H = d->height();
        drawTitle(d, x, y, mode == SW ? "Stopwatch" : "Countdown timer", "#swap *done");
        char b[24];
        fmt(b, sizeof(b), mode == SW ? elapsed() : tdLeft);
        drawCentered(d, x, y + lh + 8, b, FONT_LARGE);
        if (mode == SW) {
            drawCentered(d, x, y + lh + 8 + fontHeight(FONT_LARGE) + 2, swRun ? "5 stop  9 lap  0 reset" : "5 start  0 reset",
                         FONT_SMALL);
            int ry = y + lh + 8 + fontHeight(FONT_LARGE) + 2 + lh + 2;
            d->setFont(FONT_SMALL);
            d->setTextAlignment(TEXT_ALIGN_LEFT);
            for (int i = nLaps - 1, shown = 0; i >= 0 && ry + lh <= y + H; i--, shown++, ry += lh) {
                char lb[32];
                fmt(lb, sizeof(lb), laps[i]);
                char line[40];
                snprintf(line, sizeof(line), "lap %d  %s", i + 1, lb);
                d->drawString(x + 4, ry, line);
            }
        } else {
            const char *hint = ringing  ? "Time! (any key)"
                               : tdRun   ? "5 pause  0 reset"
                                         : "2/8 min  4/6 sec  5 start";
            drawCentered(d, x, y + lh + 8 + fontHeight(FONT_LARGE) + 2, hint, FONT_SMALL);
        }
    }

  private:
    enum Mode { SW, TD } mode = SW;
    // stopwatch
    bool swRun = false;
    uint32_t swBase = 0, swAcc = 0;
    static const int MAXLAP = 20;
    uint32_t laps[MAXLAP];
    int nLaps = 0;
    // countdown
    bool tdRun = false, ringing = false;
    uint32_t tdSet = 300000, tdLeft = 300000, tdBase = 0, tdLeftAtStart = 0, ringAt = 0;

    bool running() const { return swRun || tdRun; }
    uint32_t elapsed() const { return swAcc + (swRun ? millis() - swBase : 0); }
    void adjust(int32_t deltaMs)
    {
        int32_t v = (int32_t)tdSet + deltaMs;
        if (v < 5000)
            v = 5000;
        if (v > 99 * 60000)
            v = 99 * 60000;
        tdSet = v;
        tdLeft = tdSet;
    }
};

static TimerApp timerApp;

void openTimer()
{
    appOpen(&timerApp);
}

} // namespace badge

// Fox hunt: pick a node (the "fox") and walk around; the screen and the keypad LEDs show
// how strong its signal is. The badge pings the fox every 20 s (any Meshtastic node answers
// a direct message with an acknowledgement), and every packet heard from it counts too.
//
//   # ping now      * done
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeMesh.h"
#include "BadgeScenes.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <Arduino.h>
#include <stdio.h>

namespace badge
{

class FoxApp : public App
{
  public:
    void setTarget(uint32_t t)
    {
        target = t;
        meshNodeName(t, foxName, sizeof(foxName));
        lastStrength = -1;
        trend = 0;
    }
    const char *name() const override { return "Fox hunt"; }
    uint32_t frameMs() const override { return 1000; }
    bool keepAwake() const override { return true; }
    void begin() override { meshFoxStart(target); }
    void end() override { meshFoxStop(); }
    void key(int k, KeyAction a) override
    {
        if (k == AK_STAR && a == KA_RELEASE)
            appClose();
        else if (k == AK_HASH && a == KA_PRESS) {
            meshFoxPing();
            pingedAt = millis();
        }
    }
    bool keypadLeds(Rgb *k, uint32_t now) override
    {
        // bar graph from the bottom row up: green, yellow, red at full strength
        static const int8_t order[12] = {9, 10, 11, 8, 7, 6, 3, 4, 5, 2, 1, 0}; // rows bottom -> top
        float s = strength();
        int lit = s < 0 ? 0 : (int)(s * 12 + 0.5f);
        for (int i = 0; i < KEYPAD_LEDS; i++)
            k[i][0] = k[i][1] = k[i][2] = 0.01f;
        for (int i = 0; i < lit && i < 12; i++) {
            Rgb &o = k[order[i]];
            float t = i / 11.0f;
            o[0] = t < 0.5f ? t * 2 : 1.0f;
            o[1] = t < 0.5f ? 1.0f : (1 - t) * 2;
            o[2] = 0;
        }
        if (s < 0) // no reading yet: slow blink of the bottom row
            for (int i = 9; i < 12; i++)
                k[i][2] = (now / 500) % 2 ? 0.3f : 0.0f;
        return true;
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        drawTitle(d, x, y, "Fox hunt", "#ping *done");
        int lh = fontHeight(FONT_SMALL);
        drawCentered(d, x, y + lh + 2, foxName, FONT_SMALL);
        FoxReading r = meshFoxReading();
        float s = strength();
        int top = y + 2 * lh + 6;
        if (s < 0) {
            drawCentered(d, x, top + 10, millis() - pingedAt < 3000 ? "Pinging..." : "No signal yet", FONT_MEDIUM);
            drawCentered(d, x, top + 40, "Walk around, or press #", FONT_SMALL);
            return;
        }
        // trend: compare with the reading before
        if (r.atMs != lastAt) {
            if (lastStrength >= 0)
                trend = s > lastStrength + 0.03f ? 1 : s < lastStrength - 0.03f ? -1 : 0;
            lastStrength = s;
            lastAt = r.atMs;
        }
        const char *word = s > 0.85f ? "RIGHT HERE!" : trend > 0 ? "Warmer" : trend < 0 ? "Colder" : s > 0.6f ? "Close" : "Searching";
        drawCentered(d, x, top, word, FONT_MEDIUM);
        int barY = top + fontHeight(FONT_MEDIUM) + 4, barW = d->width() - 20;
        d->drawRect(x + 10, barY, barW, 10);
        d->fillRect(x + 10, barY, (int)(barW * s), 10);
        char b[64];
        int age = (int)((millis() - r.atMs) / 1000);
        if (r.rssi)
            snprintf(b, sizeof(b), "RSSI %d  SNR %.1f", (int)r.rssi, r.snr);
        else
            snprintf(b, sizeof(b), "SNR %.1f dB", r.snr);
        drawCentered(d, x, barY + 14, b, FONT_SMALL);
        if (r.hops > 0)
            snprintf(b, sizeof(b), "%d hop(s) away - %d s ago", r.hops, age);
        else
            snprintf(b, sizeof(b), "heard directly - %d s ago", age);
        drawCentered(d, x, barY + 14 + lh, b, FONT_SMALL);
    }

  private:
    uint32_t target = 0, pingedAt = 0, lastAt = 0;
    char foxName[40] = "";
    float lastStrength = -1;
    int trend = 0;

    // 0..1, or -1 without a reading. Relayed readings measure the last relay, so they count less.
    float strength() const
    {
        FoxReading r = meshFoxReading();
        if (!r.valid)
            return -1;
        float s;
        if (r.rssi)
            s = (r.rssi + 120) / 80.0f; // -120 dBm .. -40 dBm
        else
            s = (r.snr + 20) / 32.0f;   // -20 dB .. +12 dB
        if (r.hops > 0)
            s *= 0.5f;
        return s < 0 ? 0 : (s > 1 ? 1 : s);
    }
};

static FoxApp foxApp;

void openFoxHunt(uint32_t target)
{
    if (!target)
        return;
    foxApp.setTarget(target);
    appOpen(&foxApp);
}

} // namespace badge

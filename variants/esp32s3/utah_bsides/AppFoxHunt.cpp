// Fox hunt: pick a node (the "fox") and walk around; the screen and the keypad LEDs show
// how strong its signal is. The badge pings the fox every 20 s (any Meshtastic node answers
// a direct message with an acknowledgement), and every packet heard from it counts too.
// RSSI and SNR each get a meter with a tick at the best reading so far.
//
//   # ping now      0 reset the best-reading ticks      * done
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeFox.h"
#include "BadgeMesh.h"
#include "BadgeScenes.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>

namespace badge
{

static const float RSSI_LO = -120, RSSI_HI = -40; // dBm
static const float SNR_LO = -20, SNR_HI = 12;     // dB

// ---- shared with the WiFi / Bluetooth hunt (BadgeFox.h) --------------------------------------
float foxScale(float v, float lo, float hi)
{
    float s = (v - lo) / (hi - lo);
    return s < 0 ? 0 : (s > 1 ? 1 : s);
}

void foxMeter(OLEDDisplay *d, int16_t x, int16_t y, const char *label, float v, float peak, const char *value)
{
    const int labelW = 30, valueW = 48, h = 9;
    int bx = x + labelW, bw = d->width() - labelW - valueW - 2;
    int by = y + (fontHeight(FONT_SMALL) - h) / 2;
    d->setFont(FONT_SMALL);
    d->setTextAlignment(TEXT_ALIGN_LEFT);
    d->drawString(x + 2, y, label);
    d->drawRect(bx, by, bw, h);
    if (v > 0)
        d->fillRect(bx, by, (int)(bw * v + 0.5f), h);
    if (peak >= 0) // sticks out above and below so it shows on a full bar too
        d->drawVerticalLine(bx + (int)((bw - 1) * peak + 0.5f), by - 2, h + 4);
    d->setTextAlignment(TEXT_ALIGN_RIGHT);
    d->drawString(x + d->width() - 2, y, value);
    d->setTextAlignment(TEXT_ALIGN_LEFT);
}

void foxKeypadBar(Rgb *k, float s, uint32_t now)
{
    // bar graph from the bottom row up: green, yellow, red at full strength
    static const int8_t order[12] = {9, 10, 11, 8, 7, 6, 3, 4, 5, 2, 1, 0}; // rows bottom -> top
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
}

const char *foxWord(float s, int trend)
{
    return s > 0.85f ? "RIGHT HERE!" : trend > 0 ? "Warmer" : trend < 0 ? "Colder" : s > 0.6f ? "Close" : "Searching";
}

void TurnSweep::begin(uint32_t now)
{
    active = true;
    done = false;
    start = now;
    for (float &v : s)
        v = -1;
}

void TurnSweep::sample(float strength, uint32_t now)
{
    if (!active)
        return;
    uint32_t t = now - start;
    if (t >= MS) {
        active = false;
        done = true;
        return;
    }
    int i = (int)((uint64_t)t * N / MS);
    if (strength > s[i])
        s[i] = strength;
}

void TurnSweep::draw(OLEDDisplay *d, int16_t x, int16_t y, uint32_t now) const
{
    int lh = fontHeight(FONT_SMALL);
    char b[48];
    if (active)
        snprintf(b, sizeof(b), "%u s", (unsigned)((MS - (now - start)) / 1000 + 1));
    drawTitle(d, x, y, active ? "Turn slowly right" : "Strongest way", active ? b : "any key");
    int r = (d->height() - 3 * lh - 4) / 2, cx = x + d->width() / 2, cy = y + lh + 3 + r;
    d->drawCircle(cx, cy, r);
    d->drawLine(cx, cy - r - 2, cx, cy - r + 3); // where you started facing
    int best = -1;
    for (int i = 0; i < N; i++) {
        if (s[i] < 0)
            continue;
        float a = i * 2 * 3.14159265f / N, len = r * (0.15f + 0.85f * s[i]);
        d->drawLine(cx, cy, cx + (int)(sinf(a) * len), cy - (int)(cosf(a) * len));
        if (best < 0 || s[i] > s[best])
            best = i;
    }
    if (active) {
        float a = (float)(now - start) * 2 * 3.14159265f / MS;
        d->fillCircle(cx + (int)(sinf(a) * r), cy - (int)(cosf(a) * r), 2);
        drawCentered(d, x, y + d->height() - 2 * lh, "Badge in front of you,", FONT_SMALL);
        drawCentered(d, x, y + d->height() - lh, "turn right, all the way round", FONT_SMALL);
        return;
    }
    if (best < 0) {
        drawCentered(d, x, y + d->height() - lh, "Heard nothing while turning", FONT_SMALL);
        return;
    }
    float a = best * 2 * 3.14159265f / N;
    d->fillCircle(cx + (int)(sinf(a) * r), cy - (int)(cosf(a) * r), 3);
    int deg = best * 360 / N;
    if (deg <= 15 || deg >= 345)
        snprintf(b, sizeof(b), "Straight ahead (where you started)");
    else if (deg <= 180)
        snprintf(b, sizeof(b), "About %d deg to your right", deg);
    else
        snprintf(b, sizeof(b), "About %d deg to your left", 360 - deg);
    drawCentered(d, x, y + d->height() - 2 * lh, b, FONT_SMALL);
    drawCentered(d, x, y + d->height() - lh, "of where you started facing", FONT_SMALL);
}

// ---- LoRa fox hunt ---------------------------------------------------------------------------
class FoxApp : public App
{
  public:
    bool numberShortcuts() const override { return true; }
    void setTarget(uint32_t t)
    {
        target = t;
        meshNodeName(t, foxName, sizeof(foxName));
        lastStrength = -1;
        trend = 0;
        resetPeaks();
    }
    const char *name() const override { return "Fox hunt"; }
    uint32_t frameMs() const override { return 1000; }
    bool keepAwake() const override { return true; }
    void begin() override { meshFoxStart(target); }
    void end() override
    {
        meshFoxStop();
        meshRestorePreset(); // if the node list sent us here on a swept preset, go back to ours
    }
    void key(int k, KeyAction a) override
    {
        if (k == AK_STAR && a == KA_RELEASE)
            appClose();
        else if (k == AK_HASH && a == KA_PRESS) {
            meshFoxPing();
            pingedAt = millis();
        } else if (k == AK_0 && a == KA_PRESS) {
            resetPeaks();
            appRedraw();
        }
    }
    bool keypadLeds(Rgb *k, uint32_t now) override
    {
        foxKeypadBar(k, strength(), now);
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
            if (r.rssi && (!peakRssi || r.rssi > peakRssi))
                peakRssi = r.rssi;
            if (r.snr > peakSnr)
                peakSnr = r.snr;
        }
        drawCentered(d, x, top, foxWord(s, trend), FONT_MEDIUM);
        int barY = top + fontHeight(FONT_MEDIUM) + 4, barW = d->width() - 20;
        d->drawRect(x + 10, barY, barW, 10);
        d->fillRect(x + 10, barY, (int)(barW * s), 10);

        char b[64];
        int row = barY + 13;
        if (r.rssi)
            snprintf(b, sizeof(b), "%d dBm", (int)r.rssi);
        else
            snprintf(b, sizeof(b), "--"); // NodeDB readings carry no RSSI
        foxMeter(d, x, row, "RSSI", r.rssi ? foxScale(r.rssi, RSSI_LO, RSSI_HI) : -1,
                 peakRssi ? foxScale(peakRssi, RSSI_LO, RSSI_HI) : -1, b);
        row += lh + 1;
        snprintf(b, sizeof(b), "%.1f dB", r.snr);
        foxMeter(d, x, row, "SNR", foxScale(r.snr, SNR_LO, SNR_HI), peakSnr > -900 ? foxScale(peakSnr, SNR_LO, SNR_HI) : -1,
                 b);
        row += lh + 1;

        int age = (int)((millis() - r.atMs) / 1000);
        if (r.hops > 0)
            snprintf(b, sizeof(b), "%d hop(s) away - %d s ago", r.hops, age);
        else
            snprintf(b, sizeof(b), "heard directly - %d s ago", age);
        drawCentered(d, x, row, b, FONT_SMALL);
    }

  private:
    uint32_t target = 0, pingedAt = 0, lastAt = 0;
    char foxName[40] = "";
    float lastStrength = -1;
    int trend = 0;
    int32_t peakRssi = 0; // 0: none yet
    float peakSnr = -999;

    void resetPeaks()
    {
        peakRssi = 0;
        peakSnr = -999;
    }

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

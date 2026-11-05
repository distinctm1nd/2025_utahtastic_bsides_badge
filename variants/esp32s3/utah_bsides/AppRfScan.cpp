// Sub-GHz scanner: borrows the LoRa radio (Meshtastic can't send or receive meanwhile) to show
// what's transmitting on the common sub-GHz bands and hunt it: 315 MHz (US car fobs / TPMS),
// 433 MHz (remotes, sensors, weather stations), 868 MHz (EU IoT / alarms / LoRaWAN EU) and
// 902-928 MHz (LoRa / Z-Wave / meters / 33 cm ham). Receive only: the badge's antenna is tuned
// for ~900 MHz, so the lower bands hear only near or strong signals, and nothing is transmitted
// off the 900 MHz band. Each frequency shows what it's most likely associated with.
//
//   warning: # start, * cancel
//   band:  < > (4 / 6: faster) move the marker, 2 / 8 (up / down) change band, # meter, * done
//   meter: 9 turn in a circle, 0 reset the best tick, * back to the band
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeFox.h"
#include "BadgeRfSniff.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "mesh/Throttle.h"
#include <Arduino.h>
#include <stdio.h>

namespace badge
{

static const int MAX_BINS = 104;
static const float DB_LO = -125, DB_HI = -60;

struct Band {
    const char *name;
    float lo, step;
    int bins;
};
static const Band BANDS[] = {
    {"315 MHz", 313.0f, 0.05f, 80},  // US car fobs, TPMS, some remotes
    {"433 MHz", 433.0f, 0.025f, 80}, // ISM 433: remotes, sensors, weather stations, TPMS (EU)
    {"868 MHz", 863.0f, 0.10f, 70},  // EU ISM: IoT, alarms, meters, LoRaWAN EU
    {"915 MHz", 902.0f, 0.25f, 104}, // US ISM: LoRa / LoRaWAN, Z-Wave, meters, 33 cm ham
};
static const int BAND_COUNT = sizeof(BANDS) / sizeof(BANDS[0]);

// what a given frequency is most likely associated with (we can't decode, so this is by band)
static const char *rfTag(float f)
{
    if (f >= 314.7f && f <= 315.3f)
        return "US car fob / TPMS";
    if (f >= 313.0f && f <= 317.0f)
        return "315 ISM: remotes";
    if (f >= 433.7f && f <= 434.1f)
        return "433.92: garage / fobs";
    if (f >= 433.0f && f <= 434.8f)
        return "433 ISM: sensors, weather";
    if (f >= 867.8f && f <= 868.8f)
        return "EU 868 ISM: alarms, IoT";
    if (f >= 863.0f && f <= 870.0f)
        return "EU 868: LoRaWAN, meters";
    if ((f >= 908.2f && f <= 908.6f) || (f >= 915.8f && f <= 916.2f))
        return "Z-Wave (US)";
    if (f >= 902.0f && f <= 928.0f)
        return "US 915 ISM: LoRa, meters";
    return "unknown";
}

class RfScanApp : public App
{
  public:
    bool numberShortcuts() const override { return true; }
    const char *name() const override { return "Sub-GHz scanner"; }
    uint32_t frameMs() const override { return 200; }
    bool keepAwake() const override { return true; }
    void begin() override
    {
        state = WARN;
        error = nullptr;
        turn.cancel();
        band = BAND_COUNT - 1; // start on 915 (where the antenna works best)
        resetBand();
    }
    void end() override { rfReturn(); }
    void tick(uint32_t now) override
    {
        if (state == SWEEP) {
            for (int k = 0; k < 6; k++) { // a few channels per tick keeps the keys responsive
                float v = rfRssiAt(freqAt(bin));
                cur[bin] = v;
                peakHold[bin] = v > peakHold[bin] ? v : peakHold[bin] - 0.05f;
                bin = (bin + 1) % bins();
            }
        } else if (state == METER && !Throttle::isWithinTimespanMs(lastRead, 50)) {
            lastRead = now;
            float v = rfRssiNow();
            smooth = haveSmooth ? 0.8f * smooth + 0.2f * v : v;
            haveSmooth = true;
            if (v > best)
                best = v;
            if (!Throttle::isWithinTimespanMs(lastTrend, 1000)) {
                trend = smooth > trendFrom + 1.5f ? 1 : smooth < trendFrom - 1.5f ? -1 : 0;
                trendFrom = smooth;
                lastTrend = now;
            }
            turn.sample(foxScale(v, DB_LO, DB_HI), now);
        }
    }
    void key(int k, KeyAction a) override
    {
        bool press = a == KA_PRESS, step = press || a == KA_REPEAT;
        if (turn.done && press) { // any key closes the result
            turn.cancel();
            return;
        }
        if (k == AK_STAR && a == KA_RELEASE) {
            if (turn.active)
                turn.cancel();
            else if (state == METER)
                state = SWEEP;
            else
                appClose();
            return;
        }
        if (state == WARN && press && (k == AK_HASH || k == AK_5)) {
            error = rfBorrow();
            if (!error)
                state = SWEEP;
        } else if (state == SWEEP && step) {
            if (k == AK_UP || k == AK_DOWN || k == AK_2 || k == AK_8) { // change band
                if (press) {
                    int d = (k == AK_UP || k == AK_2) ? 1 : BAND_COUNT - 1;
                    band = (band + d) % BAND_COUNT;
                    resetBand();
                }
            } else {
                int d = (k == AK_RIGHT) ? 1 : (k == AK_LEFT) ? -1 : (k == AK_6) ? 10 : (k == AK_4) ? -10 : 0;
                marker = (marker + d + bins()) % bins();
                if (press && (k == AK_HASH || k == AK_5)) {
                    state = METER;
                    rfRssiAt(freqAt(marker));
                    haveSmooth = false;
                    best = -200;
                    trend = 0;
                    trendFrom = -200;
                }
            }
        } else if (state == METER && press) {
            if (k == AK_9)
                turn.begin(millis());
            else if (k == AK_0)
                best = -200;
        }
        appRedraw();
    }
    bool keypadLeds(Rgb *k, uint32_t now) override
    {
        if (state != METER)
            return false;
        foxKeypadBar(k, haveSmooth ? foxScale(smooth, DB_LO, DB_HI) : -1, now);
        return true;
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        if (turn.showing())
            turn.draw(d, x, y, millis());
        else if (state == WARN)
            drawWarning(d, x, y);
        else if (state == SWEEP)
            drawBand(d, x, y);
        else
            drawMeter(d, x, y);
    }

  private:
    enum State { WARN, SWEEP, METER } state = WARN;
    const char *error = nullptr;
    int band = BAND_COUNT - 1;
    float cur[MAX_BINS], peakHold[MAX_BINS];
    int bin = 0, marker = 0;
    float smooth = 0, best = -200, trendFrom = -200;
    bool haveSmooth = false;
    int trend = 0;
    uint32_t lastRead = 0, lastTrend = 0;
    TurnSweep turn;

    int bins() const { return BANDS[band].bins; }
    float freqAt(int i) const { return BANDS[band].lo + i * BANDS[band].step; }
    void resetBand()
    {
        bin = 0;
        marker = bins() / 2;
        for (int i = 0; i < MAX_BINS; i++)
            cur[i] = peakHold[i] = DB_LO;
    }

    void drawWarning(OLEDDisplay *d, int16_t x, int16_t y)
    {
        int lh = fontHeight(FONT_SMALL);
        drawTitle(d, x, y, "Sub-GHz scanner", "#start *no");
        static const char *lines[] = {"Uses the LoRa radio:", "Meshtastic can't send or", "receive until you close",
                                      "this screen. Messages to", "you meanwhile are missed."};
        for (int i = 0; i < 5; i++)
            drawCentered(d, x, y + lh + 4 + i * lh, lines[i], FONT_SMALL);
        if (error)
            drawCentered(d, x, y + d->height() - lh, error, FONT_SMALL);
    }

    void drawBand(OLEDDisplay *d, int16_t x, int16_t y)
    {
        int lh = fontHeight(FONT_SMALL);
        char span[24];
        snprintf(span, sizeof(span), "%s  MESH OFF", BANDS[band].name);
        drawTitle(d, x, y, span, nullptr);
        int gx = x + 2, gw = d->width() - 4, gy = y + lh + 3, gh = d->height() - gy + y - 3 * lh - 3;
        d->drawRect(gx, gy, gw, gh);
        int n = bins();
        for (int i = 0; i < n; i++) {
            int px0 = gx + 1 + i * (gw - 2) / n, px1 = gx + 1 + (i + 1) * (gw - 2) / n;
            int h = (int)((gh - 2) * foxScale(cur[i], DB_LO, DB_HI));
            if (h > 0)
                d->fillRect(px0, gy + gh - 1 - h, px1 - px0 > 1 ? px1 - px0 - 1 : 1, h);
            int ph = (int)((gh - 2) * foxScale(peakHold[i], DB_LO, DB_HI));
            d->setPixel(px0, gy + gh - 1 - ph);
        }
        int mx = gx + 1 + marker * (gw - 2) / n;
        for (int yy = gy + 1; yy < gy + gh - 1; yy += 2) // dotted marker
            d->setPixel(mx, yy);
        char b[48];
        float f = freqAt(marker);
        snprintf(b, sizeof(b), "%.2f MHz  %d dBm (pk %d)", f, (int)cur[marker], (int)peakHold[marker]);
        drawCentered(d, x, y + d->height() - 3 * lh, b, FONT_SMALL);
        drawCentered(d, x, y + d->height() - 2 * lh, rfTag(f), FONT_SMALL);
        drawCentered(d, x, y + d->height() - lh, "< > move  2/8 band  # meter", FONT_SMALL);
    }

    void drawMeter(OLEDDisplay *d, int16_t x, int16_t y)
    {
        int lh = fontHeight(FONT_SMALL);
        char b[48];
        float f = freqAt(marker);
        snprintf(b, sizeof(b), "%.2f MHz", f);
        drawTitle(d, x, y, b, "MESH OFF");
        drawCentered(d, x, y + lh + 1, rfTag(f), FONT_SMALL);
        float s = haveSmooth ? foxScale(smooth, DB_LO, DB_HI) : 0;
        int wordY = y + 2 * lh + 4;
        drawCentered(d, x, wordY, foxWord(s, trend), FONT_MEDIUM);
        int row = wordY + fontHeight(FONT_MEDIUM) + 3;
        snprintf(b, sizeof(b), "%d dBm", (int)smooth);
        foxMeter(d, x, row, "Level", s, best > -199 ? foxScale(best, DB_LO, DB_HI) : -1, b);
        row += lh + 2;
        snprintf(b, sizeof(b), "best %d dBm", (int)best);
        drawCentered(d, x, row, b, FONT_SMALL);
        drawCentered(d, x, y + d->height() - 2 * lh, "9 turn in a circle  0 reset", FONT_SMALL);
        drawCentered(d, x, y + d->height() - lh, "* back to the band", FONT_SMALL);
    }
};

static RfScanApp rfScanApp;

void openRfScanner()
{
    appOpen(&rfScanApp);
}

} // namespace badge

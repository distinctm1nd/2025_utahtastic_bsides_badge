// Packet capture to pcap files for Wireshark (download from Badge WiFi). Three independent
// captures:
//   Mesh (LoRa)  -> capture.pcap  -- passive, keeps running in the background once on. Direct
//                                    messages addressed to this badge are left out (see BadgeMesh).
//   WiFi         -> wifi.pcap     -- 802.11 management frames + headers; needs this screen open.
//   Bluetooth    -> ble.pcap      -- BLE advertisements; needs this screen open.
// One 2.4 GHz radio, so WiFi and Bluetooth capture can't run at once (turning one on stops the
// other). WiFi capture uses the badge's WiFi; BLE capture shares Meshtastic's Bluetooth.
//
//   2 / 8 pick, # turn on / off, hold * clear the files (asks), * done
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeCapture.h"
#include "BadgeRadioScan.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <Arduino.h>
#include <stdio.h>

namespace badge
{

class CaptureApp : public App
{
  public:
    bool numberShortcuts() const override { return true; }
    const char *name() const override { return "Capture"; }
    uint32_t frameMs() const override { return 500; }
    bool keepAwake() const override { return true; } // WiFi/BLE capture only runs while open
    void begin() override
    {
        sel = 0;
        confirm = starHeld = warnMesh = false;
    }
    void end() override
    {
        captureWifiSet(false); // stop the radio-borrowing captures; mesh capture stays as set
        captureBleSet(false);
        radioScanStop();
    }
    void tick(uint32_t) override
    {
        if (captureWifiOn() || captureBleOn())
            radioScanPoll(); // hop channels / restart scans and drain frames to the files
    }
    void key(int k, KeyAction a) override
    {
        if (k == AK_STAR && a == KA_RELEASE && starHeld) {
            starHeld = false;
            return;
        }
        if (confirm) {
            if (a == KA_PRESS && (k == AK_HASH || k == AK_5)) {
                captureClearAll();
            }
            if (a == KA_PRESS)
                confirm = false;
            appRedraw();
            return;
        }
        if (warnMesh) { // asked to confirm turning the mesh capture on (it records plain-text messages)
            if (a == KA_PRESS) {
                if (k == AK_HASH || k == AK_5)
                    captureSet(true);
                warnMesh = false;
                appRedraw();
            }
            return;
        }
        if (k == AK_STAR && a == KA_HOLD) {
            confirm = true;
            starHeld = true;
            appRedraw();
            return;
        }
        if (k == AK_STAR && a == KA_RELEASE) {
            appClose();
            return;
        }
        if (a != KA_PRESS && a != KA_REPEAT)
            return;
        if (k == AK_UP || k == AK_2)
            sel = (sel + 2) % 3;
        else if (k == AK_DOWN || k == AK_8)
            sel = (sel + 1) % 3;
        else if ((k == AK_HASH || k == AK_5) && a == KA_PRESS)
            toggle(sel);
        appRedraw();
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        int lh = fontHeight(FONT_SMALL);
        drawTitle(d, x, y, "Packet capture", "#on/off *done");
        if (confirm) {
            drawCentered(d, x, y + 2 * lh, "Clear all capture files?", FONT_SMALL);
            drawCentered(d, x, y + 4 * lh, "# yes   any other key no", FONT_SMALL);
            return;
        }
        if (warnMesh) {
            drawCentered(d, x, y + lh, "WARNING", FONT_SMALL);
            drawCentered(d, x, y + 2 * lh + 2, "Logs channel messages,", FONT_SMALL);
            drawCentered(d, x, y + 3 * lh + 2, "positions and telemetry it", FONT_SMALL);
            drawCentered(d, x, y + 4 * lh + 2, "can decrypt as plain text.", FONT_SMALL);
            drawCentered(d, x, y + 5 * lh + 2, "Your private DMs are excluded.", FONT_SMALL);
            drawCentered(d, x, y + d->height() - lh, "# turn on   any other: no", FONT_SMALL);
            return;
        }
        drawRow(d, x, y + lh + 3, 0, "Mesh (LoRa)", captureOn(), captureCount(), captureFull());
        drawRow(d, x, y + lh + 3 + 2 * lh, 1, "WiFi (802.11)", captureWifiOn(), captureWifiCount(), captureWifiFull());
        drawRow(d, x, y + lh + 3 + 4 * lh, 2, "Bluetooth adverts", captureBleOn(), captureBleCount(), captureBleFull());
        drawCentered(d, x, y + d->height() - lh, "Badge WiFi -> download  hold * clear", FONT_SMALL);
    }

  private:
    int sel = 0;
    bool confirm = false, starHeld = false, warnMesh = false;

    void toggle(int which)
    {
        if (which == 0) {
            if (captureOn())
                captureSet(false); // turning off needs no warning
            else
                warnMesh = true; // turning on: warn it records plain-text messages first
            return;
        }
        if (which == 1) {
            bool turnOn = !captureWifiOn();
            captureBleSet(false); // one radio
            radioScanStop();
            captureWifiSet(turnOn);
            if (turnOn)
                radioScanStart(SCAN_WIFI);
        } else {
            bool turnOn = !captureBleOn();
            captureWifiSet(false);
            radioScanStop();
            captureBleSet(turnOn);
            if (turnOn)
                radioScanStart(SCAN_BLE);
        }
    }
    void drawRow(OLEDDisplay *d, int16_t x, int16_t y, int i, const char *label, bool on, uint32_t n, bool full)
    {
        int lh = fontHeight(FONT_SMALL), W = d->width();
        if (i == sel) {
            d->fillRect(x, y, W, 2 * lh);
            d->setColor(BLACK);
        }
        d->setFont(FONT_SMALL);
        d->setTextAlignment(TEXT_ALIGN_LEFT);
        d->drawString(x + 2, y, label);
        d->setTextAlignment(TEXT_ALIGN_RIGHT);
        d->drawString(x + W - 2, y, on ? "ON" : "off");
        d->setTextAlignment(TEXT_ALIGN_LEFT);
        char b[32];
        snprintf(b, sizeof(b), "%u pkts%s", (unsigned)n, full ? " (full)" : "");
        d->drawString(x + 10, y + lh, b);
        d->setColor(WHITE);
    }
};

static CaptureApp captureApp;

void openCaptureApp()
{
    appOpen(&captureApp);
}

} // namespace badge

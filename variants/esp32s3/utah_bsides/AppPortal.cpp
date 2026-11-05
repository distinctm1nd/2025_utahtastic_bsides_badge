// Badge WiFi: shows how to reach the badge web page (BadgePortal.h) and asks before
// installing a firmware update received there.
//
//   * stop (and close); when an update waits: # install, * don't
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeOta.h"
#include "BadgePortal.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <Arduino.h>
#include <stdio.h>

namespace badge
{

class PortalApp : public App
{
  public:
    bool numberShortcuts() const override { return true; }
    const char *name() const override { return "Badge WiFi"; }
    uint32_t frameMs() const override { return 500; }
    bool keepAwake() const override { return true; }
    void begin() override { error = portalStart(ssid, sizeof(ssid), pass, sizeof(pass), addr, sizeof(addr)); }
    // note: no portalStop() here. The lock screen (or opening another app) ends this view but
    // leaves the WiFi serving, so a connected phone keeps its session; portalPoll() times it out
    // when no phone has been connected for a while. Only '*' below stops it on purpose.
    void end() override {}
    // portalPoll() runs from the keypad loop (so the page stays live under the lock screen too)
    void key(int k, KeyAction a) override
    {
        if (otaState() == OTA_CONFIRM && a == KA_PRESS && (k == AK_HASH || k == AK_5 || k == AK_STAR)) {
            otaConfirm(k != AK_STAR);
            swallow = k == AK_STAR;
            appRedraw();
            return;
        }
        if (k == AK_STAR && a == KA_RELEASE) {
            if (swallow)
                swallow = false;
            else if (otaState() != OTA_REBOOTING) {
                portalStop(); // '*' is a real "stop sharing"
                appClose();
            }
        }
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        int lh = fontHeight(FONT_SMALL);
        drawTitle(d, x, y, "Badge WiFi", "*stop");
        if (error) {
            drawCentered(d, x, y + 2 * lh, error, FONT_SMALL);
            return;
        }
        char b[64];
        if (otaState() == OTA_CONFIRM || otaState() == OTA_REBOOTING) {
            drawCentered(d, x, y + lh + 6, otaState() == OTA_CONFIRM ? "Install update?" : "Installed", FONT_MEDIUM);
            snprintf(b, sizeof(b), "%s -> %s", otaRunningVersion(), otaNewVersion());
            drawCentered(d, x, y + 3 * lh + 4, b, FONT_SMALL);
            drawCentered(d, x, y + 4 * lh + 4, "Signed by the badge team", FONT_SMALL);
            drawCentered(d, x, y + 6 * lh, otaState() == OTA_CONFIRM ? "# install   * don't" : "Restarting...", FONT_SMALL);
            return;
        }
        int row = y + lh + 3;
        drawCentered(d, x, row, "Join this WiFi:", FONT_SMALL);
        drawCentered(d, x, row += lh, ssid, FONT_SMALL);
        snprintf(b, sizeof(b), "password: %s", pass);
        drawCentered(d, x, row += lh, b, FONT_SMALL);
        drawCentered(d, x, row += lh + 3, "then open in a browser:", FONT_SMALL);
        drawCentered(d, x, row += lh, addr, FONT_SMALL);
        unsigned age;
        const char *ev = portalLastEvent(&age);
        if (otaState() == OTA_RECEIVING)
            snprintf(b, sizeof(b), "Receiving update %u KB", (unsigned)(otaReceived() / 1024));
        else if (ev && age < 30)
            snprintf(b, sizeof(b), "%s", ev);
        else
            snprintf(b, sizeof(b), portalClients() ? "Phone connected" : "Waiting for a phone");
        drawCentered(d, x, row += lh + 3, b, FONT_SMALL);
    }

  private:
    const char *error = nullptr;
    bool swallow = false;
    char ssid[24], pass[12], addr[32];
};

static PortalApp portalApp;

void openBadgeWifi()
{
    appOpen(&portalApp);
}

} // namespace badge

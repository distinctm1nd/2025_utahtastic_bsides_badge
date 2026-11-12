// Badge app host: see BadgeApp.h.
#include "configuration.h"

#include "BadgeApp.h"

#include "BadgeKeypad.h"
#include "BadgeLock.h"
#include "BadgeMenu.h"
#include "BluetoothStatus.h"
#include "main.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <string.h>

namespace badge
{

static App *active = nullptr;
static uint32_t lastDraw = 0;
static uint32_t lastPaint = 0; // when the app's draw callback last actually ran
static uint32_t lastForce = 0; // when we last asked the screen to redraw our alert frame
static uint32_t lastPoke = 0;
static bool redrawSoon = false;

static void drawActive(OLEDDisplay *d, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    (void)state;
    if (!active)
        return;
    d->setColor(WHITE);
    d->setTextAlignment(TEXT_ALIGN_LEFT);
    d->setFont(FONT_SMALL);
    active->draw(d, x, y);
    lastPaint = millis();
}

uint32_t appLastPaintMs()
{
    return lastPaint;
}

void appOpen(App *app)
{
    if (!screen || !app || lockBlocks(app))
        return;
    if (active && active != app)
        active->end();
    active = app;
    app->begin();
    lastPaint = millis();
    screen->startAlert(drawActive);
    lastDraw = 0;
    redrawSoon = true;
}

void appClose()
{
    if (!active)
        return;
    App *a = active;
    active = nullptr;
    a->end();
    if (screen)
        screen->endAlert();
    menuAppClosed();
}

App *appActive()
{
    return active;
}

void appRedraw()
{
    redrawSoon = true;
}

void appKey(int k, KeyAction a)
{
    if (active) {
        active->key(k, a);
        redrawSoon = true;
    }
}

// Meshtastic is showing a Bluetooth pairing code (its own screen): leave it up until pairing is
// over, instead of taking the screen straight back - the code has to be read and typed into the phone.
static bool pairingCodeShowing()
{
    return bluetoothStatus && bluetoothStatus->getConnectionState() == meshtastic::BluetoothStatus::ConnectionState::PAIRING;
}

void appTick(uint32_t now)
{
    if (!active)
        return;
    active->tick(now);
    if (!active) // the app may have closed itself
        return;
    if (redrawSoon || now - lastDraw >= active->frameMs()) {
        redrawSoon = false;
        lastDraw = now;
        if (screen) {
            screen->forceDisplay(true); // redraw the alert frame at the next screen tick
            lastForce = now;
        }
    }
    // Meshtastic rebuilds its frameset on async events (a new node, a message) and that steals our
    // single alert frame. Then a redraw we forced never reaches our draw callback: take the screen
    // back at once so the stock page only flickers, instead of waiting ~0.6 s for homePoll. (Keying
    // off "forced but not painted" is frame-rate independent, so slow-frame apps don't false-trip.)
    // Skip while a banner/menu is up, since that legitimately draws over us.
    if (active->reclaimScreen() && screen && lastForce && (int32_t)(lastPaint - lastForce) < 0 && now - lastForce > 150 &&
        screen->isScreenOn() && !screen->isOverlayBannerShowing() && !menuBusy() && !pairingCodeShowing()) {
        screen->startAlert(drawActive);
        lastForce = now;
        redrawSoon = true;
    }
    if (active->keepAwake() && now - lastPoke > 15000) {
        lastPoke = now;
        keypadPokeAwake();
    }
}

// ---- drawing helpers ------------------------------------------------------------------------
int fontHeight(const uint8_t *font)
{
    return _fontHeight(font);
}

void drawTitle(OLEDDisplay *d, int16_t x, int16_t y, const char *title, const char *right)
{
    int h = fontHeight(FONT_SMALL);
    d->setColor(WHITE);
    d->fillRect(x, y, d->width(), h + 1);
    d->setColor(BLACK);
    d->setFont(FONT_SMALL);
    // Draw the right-hand hint first and reserve its width, then clip the title so a long title (e.g.
    // a 2+ digit device count) can't run under the hint and garble it.
    int rightW = 0;
    if (right) {
        d->setTextAlignment(TEXT_ALIGN_RIGHT);
        d->drawString(x + d->width() - 2, y, right);
        rightW = d->getStringWidth(right, strlen(right)) + 6; // hint width plus a small gap
    }
    d->setTextAlignment(TEXT_ALIGN_LEFT);
    char buf[48];
    strncpy(buf, title, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    int maxW = d->width() - 4 - rightW;
    while (buf[0] && (int)d->getStringWidth(buf, strlen(buf)) > maxW)
        buf[strlen(buf) - 1] = 0; // drop trailing chars until the title fits the space left of the hint
    d->drawString(x + 2, y, buf);
    d->setColor(WHITE);
}

void drawFooter(OLEDDisplay *d, int16_t x, int16_t y, const char *text)
{
    int h = fontHeight(FONT_SMALL);
    d->setFont(FONT_SMALL);
    d->setTextAlignment(TEXT_ALIGN_CENTER);
    d->drawString(x + d->width() / 2, y + d->height() - h, text);
    d->setTextAlignment(TEXT_ALIGN_LEFT);
}

// a little "WiFi" badge at the top (black chip so it reads over a picture); for "sharing is on"
void drawWifiChip(OLEDDisplay *d, int16_t x, int16_t y)
{
    d->setFont(FONT_SMALL);
    int w = d->getStringWidth("WiFi") + 6, cx = x + (d->width() - w) / 2;
    d->setColor(BLACK);
    d->fillRect(cx, y, w, fontHeight(FONT_SMALL) + 1);
    d->setColor(WHITE);
    d->drawRect(cx, y, w, fontHeight(FONT_SMALL) + 1);
    d->setTextAlignment(TEXT_ALIGN_CENTER);
    d->drawString(cx + w / 2, y, "WiFi");
    d->setTextAlignment(TEXT_ALIGN_LEFT);
}

void drawCentered(OLEDDisplay *d, int16_t x, int16_t y, const char *text, const uint8_t *font)
{
    d->setFont(font);
    d->setTextAlignment(TEXT_ALIGN_CENTER);
    d->drawString(x + d->width() / 2, y, text);
    d->setTextAlignment(TEXT_ALIGN_LEFT);
}

} // namespace badge

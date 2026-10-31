// Badge app host: see BadgeApp.h.
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeKeypad.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <string.h>

extern graphics::Screen *screen;

namespace badge
{

static App *active = nullptr;
static uint32_t lastDraw = 0;
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
}

void appOpen(App *app)
{
    if (!screen || !app)
        return;
    if (active && active != app)
        active->end();
    active = app;
    app->begin();
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
        if (screen)
            screen->forceDisplay(true); // redraw the alert frame at the next screen tick
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
    d->setTextAlignment(TEXT_ALIGN_LEFT);
    d->drawString(x + 2, y, title);
    if (right) {
        d->setTextAlignment(TEXT_ALIGN_RIGHT);
        d->drawString(x + d->width() - 2, y, right);
        d->setTextAlignment(TEXT_ALIGN_LEFT);
    }
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

void drawCentered(OLEDDisplay *d, int16_t x, int16_t y, const char *text, const uint8_t *font)
{
    d->setFont(font);
    d->setTextAlignment(TEXT_ALIGN_CENTER);
    d->drawString(x + d->width() / 2, y, text);
    d->setTextAlignment(TEXT_ALIGN_LEFT);
}

} // namespace badge

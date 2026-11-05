// Flashlight: every LED full white and the screen white. Any key turns it off.
// Badge menu > Flashlight, or hold 0 on Meshtastic's screens.
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeLeds.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"

namespace badge
{

class FlashlightApp : public App
{
  public:
    bool numberShortcuts() const override { return true; }
    const char *name() const override { return "Flashlight"; }
    uint32_t frameMs() const override { return 1000; }
    bool keepAwake() const override { return true; }
    void begin() override { ledsSetFlashlight(true); }
    void end() override { ledsSetFlashlight(false); }
    void key(int k, KeyAction a) override
    {
        (void)k;
        if (a == KA_RELEASE)
            appClose();
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        d->fillRect(x, y, d->width(), d->height());
        d->setColor(BLACK);
        int lh = fontHeight(FONT_SMALL);
        d->setFont(FONT_SMALL);
        d->setTextAlignment(TEXT_ALIGN_CENTER);
        d->drawString(x + d->width() / 2, y + d->height() / 2 - lh, "Flashlight");
        d->drawString(x + d->width() / 2, y + d->height() / 2, "any key: off");
        d->setTextAlignment(TEXT_ALIGN_LEFT);
        d->setColor(WHITE);
    }
};

static FlashlightApp flashlightApp;

void openFlashlight()
{
    appOpen(&flashlightApp);
}

} // namespace badge

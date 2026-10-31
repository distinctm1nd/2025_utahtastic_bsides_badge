// Full-screen badge apps (name tag, message editor, lights, games, fox hunt, ...).
//
// An open app owns the screen (drawn as a Meshtastic "alert" frame, which Meshtastic leaves
// alone until we close it) and every keypad key. Close with appClose(); most apps do that on
// a long press of '*'. The joystick centre button stays with Meshtastic.
#pragma once
#include "BadgeKeypadLogic.h"
#include "BadgeScenes.h"
#include <stdint.h>

class OLEDDisplay;

namespace badge
{

class App
{
  public:
    virtual ~App() = default;
    virtual const char *name() const = 0;
    virtual void begin() {}
    virtual void end() {}
    virtual void key(int k, KeyAction a) = 0;     // AppKey values
    virtual void tick(uint32_t now) { (void)now; } // every 10 ms while open
    virtual void draw(OLEDDisplay *d, int16_t x, int16_t y) = 0;
    virtual uint32_t frameMs() const { return 250; } // how often to redraw by itself
    virtual bool keepAwake() const { return false; }  // keep the screen from sleeping
    // Keypad LEDs: return true and fill the 13 colours to take them over (games, fox hunt)
    virtual bool keypadLeds(Rgb *out13, uint32_t now)
    {
        (void)out13;
        (void)now;
        return false;
    }
};

void appOpen(App *app);
void appClose();
App *appActive();
void appRedraw();             // draw again soon
void appKey(int k, KeyAction a);
void appTick(uint32_t now);   // from the keypad thread

// small drawing helpers shared by the apps
void drawTitle(OLEDDisplay *d, int16_t x, int16_t y, const char *title, const char *right = nullptr);
void drawFooter(OLEDDisplay *d, int16_t x, int16_t y, const char *text);
void drawCentered(OLEDDisplay *d, int16_t x, int16_t y, const char *text, const uint8_t *font);
int fontHeight(const uint8_t *font);

} // namespace badge

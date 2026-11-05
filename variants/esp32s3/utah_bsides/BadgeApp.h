// Full-screen badge apps (name tag, message editor, lights, games, fox hunt, ...).
//
// An open app owns the screen (drawn as a Meshtastic "alert" frame, which Meshtastic leaves
// alone until we close it) and every keypad key. Close with appClose(); most apps do that on
// a long press of '*'. The joystick centre button stays with Meshtastic.
#pragma once
#include <esp_heap_caps.h>
#include <new>
#include <stdlib.h>
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
    // may appTick re-assert this app's alert frame if Meshtastic steals it? off for full-screen
    // animations (name tag, lock) whose redraw is heavy enough that re-asserting it in a tight loop
    // would starve the CPU; those lean on homePoll/lockPoll's slower reclaim instead
    virtual bool reclaimScreen() const { return true; }
    virtual bool typingLights() const { return false; } // light the keys up to read them (editors)
    // holding a number key runs its shortcut here (taps still reach the app, on release); off
    // where numbers are typed or played with: editors, the PIN, games
    virtual bool numberShortcuts() const { return false; }
    // Keypad LEDs: return true and fill the 13 colours to take them over (games, fox hunt)
    virtual bool keypadLeds(Rgb *out13, uint32_t now)
    {
        (void)out13;
        (void)now;
        return false;
    }
};

// Big objects that don't need fast RAM go to PSRAM: internal RAM is what the WiFi and
// Bluetooth drivers need (WiFi won't start with under ~60 KB free). Made on first use.
template <class T> T *psramNew()
{
    void *m = heap_caps_malloc(sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!m)
        m = malloc(sizeof(T));
    return new (m) T();
}
template <class T> T *psramArray(size_t n)
{
    void *m = heap_caps_calloc(n, sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return (T *)(m ? m : calloc(n, sizeof(T)));
}

void appOpen(App *app);
void appClose();
App *appActive();
void appRedraw();             // draw again soon
void appKey(int k, KeyAction a);
void appTick(uint32_t now);   // from the keypad thread
uint32_t appLastPaintMs();    // millis() when the open app was last drawn

// small drawing helpers shared by the apps
void drawTitle(OLEDDisplay *d, int16_t x, int16_t y, const char *title, const char *right = nullptr);
void drawFooter(OLEDDisplay *d, int16_t x, int16_t y, const char *text);
void drawWifiChip(OLEDDisplay *d, int16_t x, int16_t y); // a "WiFi" badge: Badge WiFi is still sharing
void drawCentered(OLEDDisplay *d, int16_t x, int16_t y, const char *text, const uint8_t *font);
int fontHeight(const uint8_t *font);
// the Skinwalker pet as reusable pieces (AppSkinwalker.cpp), so Home can embed it
void skinPetTick(uint32_t now);
void skinPetDraw(OLEDDisplay *d, int16_t x, int16_t y);
bool skinPetKey(int k, KeyAction a);

} // namespace badge

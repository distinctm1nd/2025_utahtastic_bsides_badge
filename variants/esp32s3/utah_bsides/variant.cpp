// Utah BSides 2025 badge: board start-up hooks.
//
// Meshtastic calls earlyInitVariant() at the start of setup() and lateInitVariant() once
// the screen, input broker and radio are up (both are weak hooks in src/main.cpp).
// The rest of the badge code is in this folder:
//   BadgeKeypad*.{h,cpp}  TCA9535 keypad: debounce, multi-tap letters, holds
//   BadgeScenes.{h,cpp}   LED scenes (pure maths)
//   BadgeLeds.{h,cpp}     LED thread, settings in NVS
//   BadgeMenu.{h,cpp}     hold-* badge menu, quick-send, welcome screen
//   BadgeApp.{h,cpp}      full-screen badge apps; App*.cpp are the apps themselves
//   BadgeMesh.{h,cpp}     mesh features (alerts, badges met, light shows, fox hunt, !commands)
//   BadgeStats/BadgeHunt  achievements, high scores, scavenger hunt
//   BadgeMinibadge        minibadges on the I2C pins of the minibadge slots
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeFollow.h"
#include "BadgeHome.h"
#include "BadgeKeypad.h"
#include "BadgeLeds.h"
#include "BadgeLock.h"
#include "BadgeMenu.h"
#include "BadgeMesh.h"
#include "BadgeMeshCore.h"
#include "BadgeMinibadge.h"
#include "BadgePictures.h"
#include "BadgeStats.h"
#include "BadgeTime.h"

#include "concurrency/OSThread.h"
#include "graphics/Screen.h"
#include "graphics/draw/UIRenderer.h"
#include "main.h"
#include "mesh/Throttle.h"
#include <Arduino.h>

// Minibadge clock: MB_CLOCK_H/L toggled in antiphase once a second, as on the 2025 firmware.
static void minibadgeClockTask(void *)
{
    pinMode(MB_CLOCK_H, OUTPUT);
    pinMode(MB_CLOCK_L, OUTPUT);
    bool high = false;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        high = !high;
        digitalWrite(MB_CLOCK_H, high ? HIGH : LOW);
        digitalWrite(MB_CLOCK_L, high ? LOW : HIGH);
        vTaskDelayUntil(&last, pdMS_TO_TICKS(1000));
    }
}

// the frame buffer holds a picture, not an all-black or all-white screen
static bool pictureUp()
{
    OLEDDisplay *d = screen ? screen->getDisplayDevice() : nullptr;
    if (!d || !d->buffer)
        return false;
    size_t n = (size_t)d->getWidth() * d->getHeight() / 8, lit = 0, dark = 0;
    for (size_t k = 0; k < n; k++) {
        lit += d->buffer[k] != 0x00;
        dark += d->buffer[k] != 0xFF;
    }
    return lit > 40 && dark > 40;
}

// Lights the screen the moment the first picture (the raven) has been drawn. Its own small task,
// because that happens in the middle of Meshtastic's setup(), long before any badge thread runs.
// (Nothing wipes the screen after that: the screen colour is set before it, in earlyInitVariant.)
static void backlightTask(void *)
{
    for (int i = 0; i < 400 && !pictureUp(); i++) // 8 s at the very most
        vTaskDelay(pdMS_TO_TICKS(20));
    vTaskDelay(pdMS_TO_TICKS(60)); // the picture is drawn in memory first, then sent to the panel
    digitalWrite(BADGE_TFT_BL, HIGH);
    vTaskDelete(nullptr);
}

void earlyInitVariant()
{
    badge::menuInitEarly(); // the saved screen colour, before the raven is drawn
    xTaskCreatePinnedToCore(backlightTask, "bklight", 2048, nullptr, 1, nullptr, ARDUINO_RUNNING_CORE);
    // Backlight off until the first picture (the raven) is up: left unconfigured the pin lights
    // the screen while the panel still shows nothing sensible. backlightTask lights it.
    pinMode(BADGE_TFT_BL, OUTPUT);
    digitalWrite(BADGE_TFT_BL, LOW);
    badge::ledsEarlyInit();
    xTaskCreatePinnedToCore(minibadgeClockTask, "mbclk", 2048, nullptr, 1, nullptr, ARDUINO_RUNNING_CORE);
}

// Meshtastic shows its logo and then the badge's still raven (userPrefs.jsonc) as boot screens.
// As they end, BadgeStartup settles on the cycling lock screen (or a chosen startup picture first).
namespace graphics
{
extern uint32_t logo_timeout; // src/graphics/Screen.cpp
}
// Meshtastic's boot screens are its own logo, then the badge's raven (userPrefs.jsonc). The badge
// shows the raven for both: the linker is told (platformio.ini, -Wl,--wrap) to send Meshtastic's
// one call of UIRenderer::drawBootIconScreen() here instead, so the logo is never drawn and
// Meshtastic's source stays untouched. The name is that function's C++ linker name.
extern "C" void __wrap__ZN8graphics10UIRenderer18drawBootIconScreenEPKcP11OLEDDisplayP18OLEDDisplayUiStatess(
    const char *upperMsg, OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    graphics::UIRenderer::drawOEMIconScreen(upperMsg, display, state, x, y);
}

class BadgeStartup : public concurrency::OSThread
{
  public:
    BadgeStartup() : concurrency::OSThread("BadgeStartup") {}

  protected:
    int32_t runOnce() override
    {
        if (first) { // runs after Meshtastic's setup(), which sets the boot screen time
            first = false;
            graphics::logo_timeout = (millis() - serialSinceMsec) + RAVEN_MS; // the raven stays this long from now
        }
        switch (stage) {
        case 0: // the raven has played: take over
            if (!badge::lockBootReady())
                return 20;
            // which mesh the LoRa radio speaks this run (only in a firmware built with MeshCore)
            if (!meshAsked && badge::meshcoreBuilt()) {
                meshAsked = true;
                badge::openMeshBootChoice(afterMeshChoice);
                stage = 4;
                return 100;
            }
#if BADGE_OFFENSIVE
            // research build: a full-screen "RESEARCH BUILD" page plays HERE, after the raven and
            // before the lock/gifs. It is a badge app, so it covers Meshtastic's page; stage 3 waits
            // for it to auto-close, then runs the normal startup.
            if (researchBanner) {
                researchBanner = false;
                badge::openResearchBoot();
                stage = 3;
                return 50;
            }
#endif
            // only when the wearer has explicitly chosen a startup picture does one play here;
            // by default we go straight to the cycling lock screen (no separate raven intro)
            if (!someoneHere() && badge::picStartup()[0] && badge::nametagShowStartup()) {
                stage = 1;
                return 50;
            }
            settle();
            disable();
            return INT32_MAX;
        case 1: // a chosen startup picture is up; once it closes, settle on the cycling lock
            if (badge::appActive())
                return 50;
            settle();
            disable();
            return INT32_MAX;
#if BADGE_OFFENSIVE
        case 3: // the RESEARCH BUILD page is up; once it auto-closes, run the normal startup (pic or lock)
            if (badge::appActive())
                return 50;
            if (!someoneHere() && badge::picStartup()[0] && badge::nametagShowStartup()) {
                stage = 1;
                return 50;
            }
            settle();
            disable();
            return INT32_MAX;
#endif
        case 4: // the mesh question is up; it calls afterMeshChoice() when it closes
            return 100;
        }
        disable();
        return INT32_MAX;
    }

    // The mesh question has closed: put the next startup screen up now, in the same breath, so
    // Meshtastic's own page never shows in between.
    void resume()
    {
        if (stage != 4)
            return;
        stage = 0;
        runOnce();
    }

  private:
    static void afterMeshChoice();
    // Someone picked the mesh with a key, so they are at the badge: skip the startup picture and
    // the cycling pictures and go straight to the home menu. Not with a PIN set - then it locks.
    static bool someoneHere() { return badge::meshBootPicked() && !badge::lockEnabled(); }
    // where startup ends up: the home menu, or the cycling lock (PIN if set, else attract until a key)
    static void settle()
    {
        if (someoneHere())
            badge::homeGo();
        else
            badge::lockAttract();
    }
    static const uint32_t RAVEN_MS = 1600; // on top of the ~2.5 s it has been up while startup finished
    bool first = true;
    bool meshAsked = false;
    int stage = 0;
#if BADGE_OFFENSIVE
    bool researchBanner = true;
#endif
};
static BadgeStartup *startupThread = nullptr;
void BadgeStartup::afterMeshChoice()
{
    if (startupThread)
        startupThread->resume();
}

void lateInitVariant()
{
    badge::timeZoneDefault();
    badge::lockBoot();
    badge::homeBoot();
    badge::statsLoad();
    badge::ledsStart();
    badge::keypadStart();
    badge::meshStart();
    badge::meshcoreStart(); // MeshCore mode: takes the radio over from Meshtastic
    badge::minibadgeStart();
    badge::watchStart();
    startupThread = new BadgeStartup();
    badge::menuInit();
}

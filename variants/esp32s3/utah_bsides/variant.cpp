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
#include "BadgePictures.h"
#include "BadgeTime.h"
#include "BadgeHome.h"
#include "BadgeLock.h"
#include "BadgeApps.h"
#include "BadgeFollow.h"
#include "BadgeKeypad.h"
#include "BadgeLeds.h"
#include "BadgeMenu.h"
#include "BadgeMesh.h"
#include "BadgeMinibadge.h"
#include "BadgeStats.h"

#include "concurrency/OSThread.h"
#include "main.h"
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

void earlyInitVariant()
{
    badge::ledsEarlyInit();
    xTaskCreatePinnedToCore(minibadgeClockTask, "mbclk", 2048, nullptr, 1, nullptr, ARDUINO_RUNNING_CORE);
}

// Meshtastic shows its logo and then the badge's still raven (userPrefs.jsonc) as boot screens.
// As they end, BadgeStartup settles on the cycling lock screen (or a chosen startup picture first).
namespace graphics
{
extern uint32_t logo_timeout; // src/graphics/Screen.cpp
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
            graphics::logo_timeout = 6400; // ~3.2 s Meshtastic logo, then ~2.9 s on the still raven
        }
        switch (stage) {
        case 0: // let Meshtastic's boot screens play (logo, then the static raven), then take over
            if (!badge::lockBootReady())
                return 20;
            // only when the wearer has explicitly chosen a startup picture does one play here;
            // by default we go straight to the cycling lock screen (no separate raven intro)
            if (badge::picStartup()[0] && badge::nametagShowStartup()) {
                stage = 1;
                return 50;
            }
            badge::lockAttract(); // the cycling lock (PIN if set, else attract until a key)
            disable();
            return INT32_MAX;
        case 1: // a chosen startup picture is up; once it closes, settle on the cycling lock
            if (badge::appActive())
                return 50;
            badge::lockAttract();
            disable();
            return INT32_MAX;
        }
        disable();
        return INT32_MAX;
    }

  private:
    bool first = true;
    int stage = 0;
};
static BadgeStartup *startupThread = nullptr;

void lateInitVariant()
{
    badge::timeZoneDefault();
    badge::lockBoot();
    badge::homeBoot();
    badge::statsLoad();
    badge::ledsStart();
    badge::keypadStart();
    badge::meshStart();
    badge::minibadgeStart();
    badge::watchStart();
    startupThread = new BadgeStartup();
    badge::menuInit();

}

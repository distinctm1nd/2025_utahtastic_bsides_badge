// Utah BSides 2025 badge: board start-up hooks.
//
// Meshtastic calls earlyInitVariant() at the start of setup() and lateInitVariant() once
// the screen, input broker and radio are up (both are weak hooks in src/main.cpp).
// The rest of the badge code is in this folder:
//   BadgeKeypad*.{h,cpp}  TCA9535 keypad: debounce, multi-tap letters, holds
//   BadgeScenes.{h,cpp}   LED scenes (pure maths)
//   BadgeLeds.{h,cpp}     LED thread, settings in NVS
//   BadgeMenu.{h,cpp}     hold-* badge menu and welcome screen
#include "configuration.h"

#include "BadgeKeypad.h"
#include "BadgeLeds.h"
#include "BadgeMenu.h"

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

void lateInitVariant()
{
    badge::ledsStart();
    badge::keypadStart();
    badge::welcomeShowIfEnabled();
}

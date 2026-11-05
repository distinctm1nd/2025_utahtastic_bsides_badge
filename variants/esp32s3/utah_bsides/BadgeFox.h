// Drawing shared by the LoRa and the WiFi / Bluetooth fox hunts (AppFoxHunt.cpp).
#pragma once
#include "BadgeScenes.h"
#include <stdint.h>

class OLEDDisplay;

namespace badge
{

float foxScale(float v, float lo, float hi); // lo..hi -> 0..1, clamped
// one row: label, bar filled to v (0..1, < 0 empty), a tick at peak (< 0 none), value text
void foxMeter(OLEDDisplay *d, int16_t x, int16_t y, const char *label, float v, float peak, const char *value);
// keypad bar graph from the bottom row up, green to red; s < 0 blinks the bottom row
void foxKeypadBar(Rgb *k, float s, uint32_t now);
// the word under the name: "RIGHT HERE!", "Warmer", "Colder", "Close" or "Searching"
const char *foxWord(float s, int trend);

// "Turn in a circle": hold the badge in front of you and turn slowly right, once round.
// Your body blocks the signal from behind, so it is strongest facing the source.
struct TurnSweep {
    static const int N = 72;          // one slot per 5 degrees
    static const uint32_t MS = 20000; // a full turn
    bool active = false, done = false;
    uint32_t start = 0;
    float s[N];
    void begin(uint32_t now);
    void sample(float strength, uint32_t now); // 0..1, < 0 when there is no fresh reading
    void cancel() { active = done = false; }
    bool showing() const { return active || done; }
    void draw(OLEDDisplay *d, int16_t x, int16_t y, uint32_t now) const;
};

} // namespace badge

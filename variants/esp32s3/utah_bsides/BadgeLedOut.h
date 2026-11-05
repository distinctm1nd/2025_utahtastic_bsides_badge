// WS2812 output for the badge's LED strips, on RMT channels set up once and kept.
// The NeoPixel library re-routes the pin to a fresh RMT channel for every update and feeds it
// from a one-block buffer through a low-priority interrupt; when the radio or screen delays
// that interrupt the RMT sends stale bits, which shows as a brief white flash.
#pragma once
#include <stdint.h>

namespace badge
{

enum LedStrip { STRIP_KEYPAD, STRIP_LOGO, STRIP_MOON, STRIP_COUNT };

void ledOutBegin(LedStrip s);                            // claim the strip's channel (once)
void ledOutEnd(LedStrip s);                              // give it up (the moon, to Meshtastic)
void ledOutShow(LedStrip s, const uint8_t (*rgb)[3], int n); // send n LEDs (r, g, b)

} // namespace badge

#include "configuration.h"

#include "BadgeLedOut.h"

#include <Arduino.h>
#include <esp32-hal-rmt.h>
#include <esp_heap_caps.h>

// WS2812 output over RMT. Meshtastic 2.8 (Arduino-ESP32 3.x / ESP-IDF 5.x) switched RMT to the new
// "driver_ng"; mixing it with the old driver/rmt.h aborts the boot ("CONFLICT! driver_ng is not
// allowed to be used with the legacy driver"). So this uses the Arduino 3.x RMT API (rmtInit /
// rmtWrite), which wraps the new driver. Each strip is addressed by its data pin; the driver
// allocates an RMT TX channel per pin. The badge drives its own LEDs, so no channel reservation
// is needed (the old global rmt_reserved_channels[] is gone in 3.x anyway).

namespace badge
{

struct Strip {
    int pin, count, blocks;
    rmt_data_t *items = nullptr;
    bool on = false;
};
// blocks: RMT memory blocks reserved for the channel. The keypad (the longest strip) gets two so
// its buffer is topped up less often; the shorter strips need only one.
static Strip strips[STRIP_COUNT] = {
    {BUTTON_LED_PIN, BUTTON_LED_COUNT, 2},
    {LOGO_EYE_LED_PIN, LOGO_EYE_LED_COUNT, 1},
    {NEOPIXEL_DATA, NEOPIXEL_COUNT, 1},
};

// 40 MHz RMT clock: 25 ns ticks. WS2812: 0 = 400 ns high, 850 low; 1 = 800 high, 450 low.
// rmt_data_t fields are {duration0, level0, duration1, level1}.
static const rmt_data_t WS_ZERO = {{16, 1, 34, 0}};
static const rmt_data_t WS_ONE = {{32, 1, 18, 0}};

void ledOutBegin(LedStrip id)
{
    Strip &s = strips[id];
    if (s.on)
        return;
    if (!s.items)
        s.items = (rmt_data_t *)heap_caps_malloc(sizeof(rmt_data_t) * 24 * s.count, MALLOC_CAP_INTERNAL);
    if (!s.items)
        return;
    if (!rmtInit(s.pin, RMT_TX_MODE, (rmt_reserve_memsize_t)s.blocks, 40000000))
        return;
    s.on = true;
}

void ledOutEnd(LedStrip id)
{
    Strip &s = strips[id];
    if (!s.on)
        return;
    rmtDeinit(s.pin);
    s.on = false;
}

void ledOutShow(LedStrip id, const uint8_t (*rgb)[3], int n)
{
    Strip &s = strips[id];
    if (!s.on || !s.items)
        return;
    if (n > s.count)
        n = s.count;
    rmt_data_t *it = s.items;
    for (int i = 0; i < n; i++) {
        const uint8_t grb[3] = {rgb[i][1], rgb[i][0], rgb[i][2]}; // these LEDs take green first
        for (int b = 0; b < 3; b++)
            for (int bit = 7; bit >= 0; bit--)
                *it++ = (grb[b] >> bit) & 1 ? WS_ONE : WS_ZERO;
    }
    rmtWrite(s.pin, s.items, n * 24, RMT_WAIT_FOR_EVER); // blocks until sent
}

} // namespace badge

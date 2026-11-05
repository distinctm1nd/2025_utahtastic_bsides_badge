#include "configuration.h"

#include "BadgeLedOut.h"

#include <Arduino.h>
#include <driver/rmt.h>
#include <esp_heap_caps.h>

// the NeoPixel library's channel bookkeeping (Meshtastic's Ambient Lighting uses it for the
// moon): mark ours taken so it never picks them
extern "C" bool rmt_reserved_channels[];

namespace badge
{

struct Strip {
    rmt_channel_t ch;
    int pin, count, blocks;
    rmt_item32_t *items = nullptr;
    bool on = false;
};
// RMT memory blocks: TX channel n with 2 blocks also uses n+1's. The keypad (13 LEDs, the
// longest) gets two so its buffer is topped up less often.
static Strip strips[STRIP_COUNT] = {
    {RMT_CHANNEL_0, BUTTON_LED_PIN, BUTTON_LED_COUNT, 2},
    {RMT_CHANNEL_2, LOGO_EYE_LED_PIN, LOGO_EYE_LED_COUNT, 1},
    {RMT_CHANNEL_3, NEOPIXEL_DATA, NEOPIXEL_COUNT, 1},
};

// 40 MHz RMT clock (80 MHz APB / 2): 25 ns ticks. WS2812: 0 = 400 ns high, 850 low;
// 1 = 800 high, 450 low.
static const rmt_item32_t WS_ZERO = {{{16, 1, 34, 0}}};
static const rmt_item32_t WS_ONE = {{{32, 1, 18, 0}}};

void ledOutBegin(LedStrip id)
{
    Strip &s = strips[id];
    if (s.on)
        return;
    if (!s.items)
        s.items = (rmt_item32_t *)heap_caps_malloc(sizeof(rmt_item32_t) * 24 * s.count, MALLOC_CAP_INTERNAL);
    if (!s.items)
        return;
    rmt_config_t c = RMT_DEFAULT_CONFIG_TX((gpio_num_t)s.pin, s.ch);
    c.clk_div = 2;
    c.mem_block_num = (uint8_t)s.blocks;
    c.tx_config.idle_output_en = true;
    c.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
    if (rmt_config(&c) != ESP_OK)
        return;
    // a higher interrupt level, so the buffer top-ups aren't held up by ordinary interrupts
    if (rmt_driver_install(s.ch, 0, ESP_INTR_FLAG_LEVEL3) != ESP_OK)
        return;
    for (int i = 0; i < s.blocks; i++)
        rmt_reserved_channels[s.ch + i] = true;
    s.on = true;
}

void ledOutEnd(LedStrip id)
{
    Strip &s = strips[id];
    if (!s.on)
        return;
    rmt_wait_tx_done(s.ch, pdMS_TO_TICKS(20));
    rmt_driver_uninstall(s.ch);
    for (int i = 0; i < s.blocks; i++)
        rmt_reserved_channels[s.ch + i] = false;
    s.on = false;
}

void ledOutShow(LedStrip id, const uint8_t (*rgb)[3], int n)
{
    Strip &s = strips[id];
    if (!s.on || !s.items)
        return;
    if (n > s.count)
        n = s.count;
    rmt_item32_t *it = s.items;
    for (int i = 0; i < n; i++) {
        const uint8_t grb[3] = {rgb[i][1], rgb[i][0], rgb[i][2]}; // these LEDs take green first
        for (int b = 0; b < 3; b++)
            for (int bit = 7; bit >= 0; bit--)
                *it++ = (grb[b] >> bit) & 1 ? WS_ONE : WS_ZERO;
    }
    rmt_write_items(s.ch, s.items, n * 24, true); // waits until sent
}

} // namespace badge

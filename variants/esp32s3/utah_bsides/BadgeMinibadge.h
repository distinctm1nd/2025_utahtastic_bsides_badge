// Minibadges that talk I2C (github.com/lukejenkins/minibadge, "I2C Example Code").
// The three minibadge slots share SDA = GPIO5 and SCL = GPIO6 (MB_SDA/MB_SCL, with pull-ups
// R12/R13); this uses its own I2C bus (Wire1) for them. The minibadge CLK pin is left
// exactly as it was (1 Hz, the standard).
//
// Every 2 s each minibadge is asked for a message: a button press gives a light burst, a
// text message pops up on the screen. Minibadges that accept writes get the LED brightness
// and the latest game score.
#pragma once
#include <stdint.h>

namespace badge
{

struct Minibadge {
    uint8_t addr;
    bool canWrite;
    uint16_t presses;
    char text[64]; // last text message
};

void minibadgeStart(); // from lateInitVariant()
int minibadgeCount();
const Minibadge &minibadgeAt(int i);
void minibadgeSendScore(uint16_t score);

} // namespace badge

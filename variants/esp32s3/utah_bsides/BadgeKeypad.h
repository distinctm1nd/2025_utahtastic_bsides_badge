// Keypad driver for the Utah BSides badge (TCA9535 on I2C 0x20).
#pragma once
#include <stdint.h>

namespace badge
{

// TCA9535 bits (port 0 = bits 0..7, port 1 = bits 8..15), checked against the KiCad board
enum KeyBit : uint8_t {
    BKEY_STAR = 0,  // P00 SW1
    BKEY_0 = 1,     // P01 SW2
    BKEY_HASH = 2,  // P02 SW3
    BKEY_9 = 3,     // P03 SW4
    BKEY_8 = 4,     // P04 SW5
    BKEY_7 = 5,     // P05 SW6
    BKEY_6 = 6,     // P06 SW7
    BKEY_5 = 7,     // P07 SW8
    BKEY_4 = 8,     // P10 SW11
    BKEY_3 = 9,     // P11 SW12
    BKEY_2 = 10,    // P12 SW13
    BKEY_1 = 11,    // P13 SW14
    BKEY_JOY_A = 12, // P14..P17: joystick S1 as wired: UP, RIGHT, DOWN, LEFT (before correction)
    BKEY_JOY_B = 13,
    BKEY_JOY_C = 14,
    BKEY_JOY_D = 15,
};

void keypadStart(); // from lateInitVariant()
uint16_t keypadState(); // debounced, 1 = pressed
void keypadPokeAwake(); // count as user input so the screen doesn't time out
// type text into Meshtastic's message box as if from the keypad, then (optionally) send
void keypadInjectText(const char *text, bool send);
// joystick angle correction (see KeypadLogic::setJoystick)
void keypadSetJoystick(int steps, bool lean, bool save);
void keypadGetJoystick(int &steps, bool &lean);

} // namespace badge

// Utah BSides 2025 badge ("UTAHtastic")
// Pin map taken from hardware/2025_bsides_badge_v2.kicad_pcb in the badge repo.
// This header is included by configuration.h for every source file, so keep it to
// plain #defines (no includes, no declarations).
#pragma once

#define UTAH_BSIDES_BADGE_HW 1

#define HAS_WIFI 1
#define HAS_BLUETOOTH 1
#define HAS_RADIO 1
#define HAS_GPS 0
#define CANNED_MESSAGE_MODULE_ENABLE 1

// ---- LoRa (U4, Ai-Thinker Ra-01 style 16-pad footprint) ----------------------------
// Stock part: DreamLNK DL-LLCC68-S-915S. The LLCC68 cannot do SF11 @ 250 kHz, so it can
// not join LongFast; the badge default is MediumFast.
// Swapping in a Heltec HT-RA62 (SX1262) enables LongFast; that module has BUSY on pad 10
// instead of pad 5, so it needs one wire (see README.md). The firmware probes for an
// SX1262 first and falls back to the LLCC68, so the same build runs on both.
#define USE_SX1262
#define USE_LLCC68
// The 2025 firmware ran the DL-LLCC68 without a DIO3 TCXO, and the LLCC68 probe still does.
// TCXO_OPTIONAL makes the SX1262 probe try 1.8 V on DIO3 first and then plain XTAL, so an
// SX1262 module comes up whichever way its oscillator is powered.
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define TCXO_OPTIONAL
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_TXEN RADIOLIB_NC
#define SX126X_RXEN RADIOLIB_NC
#define SX126X_MAX_POWER 22

#define LORA_SCK 3
#define LORA_MOSI 10
#define LORA_MISO 9
#define LORA_CS 11
#define SX126X_CS LORA_CS
#define SX126X_BUSY 14
#define SX126X_DIO1 13
#define SX126X_RESET 21

// ---- Display: ST7735S 128x160 TFT on SPI3, shown landscape --------------------------
#define HAS_SCREEN 1
#define USE_TFTDISPLAY 1
#define ST7735S 1
#define ST7735_CS 38
#define ST7735_RS 35  // DC
#define ST7735_SDA 37 // MOSI
#define ST7735_SCK 36
#define ST7735_RESET 48
#define ST7735_MISO -1
#define ST7735_BUSY -1
#define TFT_BL 47
#define TFT_BACKLIGHT_ON HIGH
#define ST7735_SPI_HOST SPI3_HOST
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_HEIGHT 160
#define TFT_WIDTH 128
#define TFT_OFFSET_X 2
#define TFT_OFFSET_Y -1
#define TFT_INVERT false
#define SCREEN_ROTATE
#define SCREEN_TRANSITION_FRAMERATE 12 // fps; also the redraw rate of badge games
#define DISPLAY_FORCE_SMALL_FONTS

// ---- I2C: TCA9535 keypad expander (0x20) shares the main bus ------------------------
#define I2C_SDA 1
#define I2C_SCL 2
#define BADGE_KEYPAD_ADDR 0x20
#define BADGE_KEYPAD_INT 15 // TCA9535 /INT, 10k pull-up (open drain, active low)

// Joystick centre press is a plain GPIO; Meshtastic's normal user-button handling
// (click = next screen / next item, hold = select / open menu) runs on it.
#define BUTTON_PIN 7

// ---- LEDs -------------------------------------------------------------------------
// Moon (6 x WS2812 on GPIO16): Meshtastic "Ambient Lighting", colour set from the app.
#define HAS_NEOPIXEL
#define NEOPIXEL_COUNT 6
#define NEOPIXEL_DATA 16
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800)
#define ENABLE_AMBIENTLIGHTING

// Keypad backlights (13 x WS2812 on GPIO4) and logo/eyes (6 x WS2812 on GPIO17):
// driven by the badge LED scenes in BadgeLeds.cpp.
#define BUTTON_LED_PIN 4
#define BUTTON_LED_COUNT 13
#define LOGO_EYE_LED_PIN 17
#define LOGO_EYE_LED_COUNT 6

// ---- Misc -------------------------------------------------------------------------
#define UART_TX 43
#define UART_RX 44

// Minibadge clock: two pins driven in antiphase at 1 Hz (unchanged from the 2025 firmware)
#define MB_CLOCK_H 8
#define MB_CLOCK_L 18

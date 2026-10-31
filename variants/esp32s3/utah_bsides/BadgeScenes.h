// LED scenes for the Utah BSides badge: pure maths, no Arduino or Meshtastic headers, so
// the same file also builds on a PC (see tools/scene_preview.cpp in this folder).
//
// Every scene draws all 25 badge LEDs in one go: the 13 keypad backlights, the 2 "eye"
// and 4 logo LEDs on the sign, and the 6 moon LEDs. Whether the moon actually follows the
// scene is a menu setting (by default the moon keeps the colour set in the Meshtastic app).
#pragma once
#include <stdint.h>

namespace badge
{

enum LedGroup : uint8_t { GROUP_KEYPAD = 0, GROUP_EYES = 1, GROUP_LOGO = 2, GROUP_MOON = 3 };

// Positions from the KiCad board (front view, mm). Order = physical order on each strip.
struct LedInfo {
    float x, y;    // whole badge, 0..1 (left->right, top->bottom)
    float gx, gy;  // within the group: keypad column 0..1 / row 0..1, ring angle for the moon, etc.
    uint8_t group;
    int8_t col, row; // keypad grid (col 0..2, row 0..3), -1 elsewhere
};

constexpr int KEYPAD_LEDS = 13; // GPIO4, D1..D13
constexpr int LOGO_LEDS = 6;    // GPIO17, D20..D25 (2 eyes + 4 logo)
constexpr int MOON_LEDS = 6;    // GPIO16, D14..D19
constexpr int NUM_LEDS = KEYPAD_LEDS + LOGO_LEDS + MOON_LEDS;
constexpr int FIRST_LOGO = KEYPAD_LEDS;
constexpr int FIRST_MOON = KEYPAD_LEDS + LOGO_LEDS;

extern const LedInfo LEDS[NUM_LEDS];

// LED index (0..12) behind each keypad key, by TCA9535 bit (bit 0 = '*', ... bit 11 = '1').
// -1 for the joystick bits.
extern const int8_t KEY_BIT_TO_LED[16];

typedef float Rgb[3]; // 0..1 per channel, as you would pick it on a screen

struct SceneCtx {
    float t;          // scene time, seconds (scaled by the speed setting)
    float dt;         // time since the last frame, seconds (scaled)
    bool reset;       // true on the first frame after the scene was selected
    Rgb *out;         // NUM_LEDS colours to fill
    // key presses since the last frame (LED indices), for reactive scenes
    int8_t pressed[4];
    uint8_t npressed;
};

typedef void (*SceneFn)(SceneCtx &c);

struct Scene {
    const char *name;
    SceneFn fn;
};

int sceneCount();
const Scene &sceneAt(int i);
int sceneFind(const char *name); // -1 if unknown

// Old 2025 firmware stored a palette index 0..6 in NVS ("colorIndex"); this returns the
// matching scene so an updated badge keeps the pattern its owner picked.
int sceneFromLegacyIndex(int colorIndex);

// Colour picked in the Meshtastic app (Ambient Lighting), used by the "APP COLOUR" scenes
void sceneSetAppColour(float r, float g, float b);

// Short overlays drawn on top of any scene
void overlayKeyFlash(SceneCtx &c, const float flashLevel[KEYPAD_LEDS]);
void overlayCelebrate(SceneCtx &c, float secondsLeft);
void overlayAlert(SceneCtx &c, bool directMessage, float secondsIn); // message arrived
void overlayPulse(SceneCtx &c, float level, bool includeMoon);      // mesh traffic blink

} // namespace badge

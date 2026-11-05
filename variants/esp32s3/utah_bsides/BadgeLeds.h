// Drives the badge LEDs with the scenes in BadgeScenes.cpp and keeps the chosen scene,
// speed and brightness in flash (NVS namespace "utah").
#pragma once
#include "BadgeScenes.h"
#include <stdint.h>

namespace badge
{

void ledsEarlyInit(); // from earlyInitVariant(): LEDs on in a dim colour while booting
void ledsStart();     // from lateInitVariant(): load settings, start the 50 Hz LED thread

int ledsScene();
void ledsSetScene(int i, bool save = true);
void ledsNextScene();

// speed and brightness are small option lists, see the tables in BadgeLeds.cpp
int ledsSpeedCount();
const char *ledsSpeedName(int i);
int ledsSpeed();
void ledsSetSpeed(int i);

int ledsBrightCount();
const char *ledsBrightName(int i);
int ledsBright();
void ledsSetBright(int i);

bool ledsMoonFollows();      // true (default): moon is part of the scene; false: app's Ambient Lighting colour
void ledsSetMoonFollows(bool on);
bool ledsKeyFlash();         // keys flash white when pressed
void ledsSetKeyFlash(bool on);

void ledsKeyPressed(int keyBit); // TCA9535 bit of a key that went down
void ledsCelebrate();            // Konami code reward (6 s rainbow strobe)
void ledsFlourish();             // short rainbow burst: new badge, achievement, hunt flag
void ledsMessageAlert(bool dm);  // keys pulse: 3x for a direct message, once for a channel
void ledsSetFlashlight(bool on);  // every LED full white (the flashlight app)
void ledsMeshPulse();            // the eyes (and moon, if it follows) blink once: game feedback

// light shows: jump to a scene at a given scene time (not saved)
void ledsSetSceneSynced(int scene, int speed, uint32_t timeMs);
uint32_t ledsSceneTimeMs();

// the colours being shown (NUM_LEDS, before brightness), for the on-screen preview
const Rgb *ledsFrame();

// power: LEDs while the screen is asleep (0 keep, 1 dim, 2 off) and night mode (dim cap)
int ledsOffMode();
void ledsSetOffMode(int mode);
bool ledsNightMode();
void ledsSetNightMode(bool on);

} // namespace badge

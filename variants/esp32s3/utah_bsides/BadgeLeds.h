// Drives the badge LEDs with the scenes in BadgeScenes.cpp and keeps the chosen scene,
// speed and brightness in flash (NVS namespace "utah").
#pragma once
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

bool ledsMoonFollows();      // false: moon shows the Ambient Lighting colour from the app
void ledsSetMoonFollows(bool on);
bool ledsKeyFlash();         // keys flash white when pressed
void ledsSetKeyFlash(bool on);

void ledsKeyPressed(int keyBit); // TCA9535 bit of a key that went down
void ledsCelebrate();            // Konami code reward

} // namespace badge

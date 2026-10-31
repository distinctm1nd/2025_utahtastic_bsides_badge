// LED driver for the Utah BSides badge: renders BadgeScenes at 50 Hz on its own OSThread.
// Replaces the 2025 patch to src/AmbientLightingThread.h, so Meshtastic itself is untouched
// and the keypad/logo animations no longer stop when Ambient Lighting is turned off.
#include "configuration.h"

#include "BadgeLeds.h"
#include "BadgeScenes.h"
#include "BadgeApp.h"

#include "NodeDB.h"
#include "concurrency/OSThread.h"
#include "graphics/Screen.h"
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <math.h>

// Meshtastic's Ambient Lighting thread (owns the moon LEDs by default); defined in main.cpp
extern concurrency::OSThread *ambientLightingThread;
extern graphics::Screen *screen;

namespace badge
{

static Adafruit_NeoPixel keypadStrip(BUTTON_LED_COUNT, BUTTON_LED_PIN, NEO_GRB + NEO_KHZ800);
static Adafruit_NeoPixel logoStrip(LOGO_EYE_LED_COUNT, LOGO_EYE_LED_PIN, NEO_GRB + NEO_KHZ800);
static Adafruit_NeoPixel moonStrip(NEOPIXEL_COUNT, NEOPIXEL_DATA, NEOPIXEL_TYPE);

static_assert(BUTTON_LED_COUNT == KEYPAD_LEDS, "keypad LED count");
static_assert(LOGO_EYE_LED_COUNT == LOGO_LEDS, "logo LED count");
static_assert(NEOPIXEL_COUNT == MOON_LEDS, "moon LED count");

static const char *SPEED_NAMES[] = {"Slowest", "Slow", "Normal", "Fast", "Fastest"};
static const float SPEED_MUL[] = {0.25f, 0.5f, 1.0f, 1.6f, 2.5f};
static const char *BRIGHT_NAMES[] = {"Dim", "Low", "Medium", "High", "Max"};
static const float BRIGHT_LEVEL[] = {0.06f, 0.15f, 0.30f, 0.55f, 1.0f};
static const int NSPEED = sizeof(SPEED_MUL) / sizeof(SPEED_MUL[0]);
static const int NBRIGHT = sizeof(BRIGHT_LEVEL) / sizeof(BRIGHT_LEVEL[0]);

static int curScene = 0;
static int curSpeed = 2;
static int curBright = 2;
static bool moonFollows = false;
static bool keyFlash = true;
static bool sceneReset = true;
static float sceneTime = 0;
static float flash[KEYPAD_LEDS];
static int8_t pending[4];
static uint8_t npending = 0;
static float celebrateLeft = 0;
static Rgb frame[NUM_LEDS];
static int offMode = 1;      // LEDs while the screen is asleep: 0 keep, 1 dim, 2 off
static bool nightMode = false; // caps brightness at "Dim"
static float alertLeft = 0, alertLen = 0;
static bool alertDm = false;
static float pulseLeft = 0;
static float flourishLeft = 0;
static bool syncedScene = false; // scene came from a light show (not saved)
static uint8_t gammaTable[256];

static const char *NVS_NS = "utah";

static void buildGamma()
{
    for (int i = 0; i < 256; i++)
        gammaTable[i] = (uint8_t)lroundf(powf(i / 255.0f, 2.2f) * 255.0f);
}

static uint32_t toPixel(const Rgb &c, float level)
{
    uint8_t ch[3];
    for (int j = 0; j < 3; j++) {
        float v = c[j] * level;
        v = v < 0 ? 0 : (v > 1 ? 1 : v);
        ch[j] = gammaTable[(int)(v * 255.0f + 0.5f)];
    }
    return Adafruit_NeoPixel::Color(ch[0], ch[1], ch[2]);
}

// ---- settings --------------------------------------------------------------------------
static void saveSettings()
{
    Preferences p;
    if (!p.begin(NVS_NS, false))
        return;
    p.putString("scene", sceneAt(curScene).name);
    p.putUChar("speed", (uint8_t)curSpeed);
    p.putUChar("bright", (uint8_t)curBright);
    p.putBool("moon", moonFollows);
    p.putBool("keyfx", keyFlash);
    p.putUChar("ledoff", (uint8_t)offMode);
    p.putBool("night", nightMode);
    p.end();
}

static void loadSettings()
{
    Preferences p;
    if (!p.begin(NVS_NS, false))
        return;
    int s = -1;
    if (p.isKey("scene"))
        s = sceneFind(p.getString("scene", "").c_str());
    else if (p.isKey("colorIndex")) // pattern picked with the 2025 firmware
        s = sceneFromLegacyIndex(p.getInt("colorIndex", 0));
    curScene = s < 0 ? 0 : s;
    curSpeed = p.getUChar("speed", 2);
    curBright = p.getUChar("bright", 2);
    moonFollows = p.getBool("moon", false);
    keyFlash = p.getBool("keyfx", true);
    offMode = p.getUChar("ledoff", 1);
    nightMode = p.getBool("night", false);
    p.end();
    if (offMode > 2)
        offMode = 1;
    if (curSpeed >= NSPEED)
        curSpeed = 2;
    if (curBright >= NBRIGHT)
        curBright = 2;
}

// Hand the moon to the scenes, or back to Meshtastic's Ambient Lighting (app colour).
static void applyMoonOwner()
{
    if (!ambientLightingThread)
        return;
    if (moonFollows) {
        ambientLightingThread->disable();
    } else {
        moonStrip.clear();
        moonStrip.show();
        if (moduleConfig.ambient_lighting.led_state) {
            ambientLightingThread->enabled = true;
            ambientLightingThread->setIntervalFromNow(0); // re-applies the app colour now
        }
    }
}

// ---- thread ------------------------------------------------------------------------------
class BadgeLedThread : public concurrency::OSThread
{
  public:
    BadgeLedThread() : concurrency::OSThread("BadgeLEDs") {}

  protected:
    int32_t runOnce() override
    {
        uint32_t now = millis();
        float dt = lastMs ? (now - lastMs) / 1000.0f : 0.02f;
        if (dt > 0.2f)
            dt = 0.2f; // after a long stall, don't jump
        lastMs = now;
        float sdt = dt * SPEED_MUL[curSpeed];
        sceneTime += sdt;

        SceneCtx c;
        c.t = sceneTime;
        c.dt = sdt;
        c.reset = sceneReset;
        c.out = frame;
        c.npressed = npending;
        for (int i = 0; i < npending; i++)
            c.pressed[i] = pending[i];
        npending = 0;
        sceneReset = false;

        const meshtastic_ModuleConfig_AmbientLightingConfig &al = moduleConfig.ambient_lighting;
        sceneSetAppColour(al.red / 255.0f, al.green / 255.0f, al.blue / 255.0f);
        sceneAt(curScene).fn(c);

        // a badge app (game, fox hunt) can take over the keypad LEDs
        App *app = appActive();
        bool appLeds = app && app->keypadLeds(frame, now);
        if (keyFlash && !appLeds)
            overlayKeyFlash(c, flash);
        if (alertLeft > 0) {
            overlayAlert(c, alertDm, alertLen - alertLeft);
            alertLeft -= dt;
        }
        if (pulseLeft > 0) {
            overlayPulse(c, pulseLeft / 0.35f, moonFollows);
            pulseLeft -= dt;
        }
        if (flourishLeft > 0) {
            overlayCelebrate(c, flourishLeft * 2);
            flourishLeft -= dt;
        }
        if (celebrateLeft > 0) {
            overlayCelebrate(c, celebrateLeft);
            celebrateLeft -= dt;
        }
        for (int i = 0; i < KEYPAD_LEDS; i++)
            flash[i] = flash[i] > 0 ? flash[i] - dt * 3.0f : 0;

        float level = BRIGHT_LEVEL[nightMode ? 0 : curBright];
        if (screen && !screen->isScreenOn() && offMode)
            level = offMode == 2 ? 0 : level * 0.3f; // save the AA batteries while nobody looks
        for (int i = 0; i < KEYPAD_LEDS; i++)
            keypadStrip.setPixelColor(i, toPixel(frame[i], level));
        for (int i = 0; i < LOGO_LEDS; i++)
            logoStrip.setPixelColor(i, toPixel(frame[FIRST_LOGO + i], level));
        keypadStrip.show();
        logoStrip.show();
        if (moonFollows) {
            for (int i = 0; i < MOON_LEDS; i++)
                moonStrip.setPixelColor(i, toPixel(frame[FIRST_MOON + i], level));
            moonStrip.show();
        }
        return 20; // 50 frames a second
    }

  private:
    uint32_t lastMs = 0;
};

static BadgeLedThread *ledThread = nullptr;

// ---- public API ---------------------------------------------------------------------------
void ledsEarlyInit()
{
    buildGamma();
    keypadStrip.begin();
    logoStrip.begin();
    keypadStrip.fill(Adafruit_NeoPixel::Color(8, 3, 0)); // dim amber while booting
    logoStrip.fill(Adafruit_NeoPixel::Color(8, 3, 0));
    keypadStrip.show();
    logoStrip.show();
}

void ledsStart()
{
    loadSettings();
    moonStrip.begin();
    applyMoonOwner();
    sceneReset = true;
    if (!ledThread)
        ledThread = new BadgeLedThread();
}

int ledsScene()
{
    return curScene;
}
void ledsSetScene(int i, bool save)
{
    if (i < 0 || i >= sceneCount())
        i = 0;
    curScene = i;
    sceneTime = 0;
    sceneReset = true;
    syncedScene = false;
    if (save)
        saveSettings();
}
void ledsSetSceneSynced(int i, int speed, uint32_t timeMs)
{
    if (i < 0 || i >= sceneCount())
        return;
    if (i != curScene)
        sceneReset = true;
    curScene = i;
    if (speed >= 0 && speed < NSPEED)
        curSpeed = speed;
    sceneTime = timeMs / 1000.0f;
    syncedScene = true; // not saved: after a reboot the badge goes back to its own scene
}
uint32_t ledsSceneTimeMs()
{
    return (uint32_t)(sceneTime * 1000.0f);
}
const Rgb *ledsFrame()
{
    return frame;
}
void ledsMessageAlert(bool dm)
{
    alertDm = dm;
    alertLen = alertLeft = dm ? 1.8f : 0.8f;
}
void ledsMeshPulse()
{
    if (pulseLeft <= 0)
        pulseLeft = 0.35f;
}
void ledsFlourish()
{
    flourishLeft = 1.5f;
}
int ledsOffMode()
{
    return offMode;
}
void ledsSetOffMode(int m)
{
    if (m >= 0 && m <= 2) {
        offMode = m;
        saveSettings();
    }
}
bool ledsNightMode()
{
    return nightMode;
}
void ledsSetNightMode(bool on)
{
    nightMode = on;
    saveSettings();
}
void ledsNextScene()
{
    ledsSetScene((curScene + 1) % sceneCount());
}

int ledsSpeedCount()
{
    return NSPEED;
}
const char *ledsSpeedName(int i)
{
    return SPEED_NAMES[(i >= 0 && i < NSPEED) ? i : 2];
}
int ledsSpeed()
{
    return curSpeed;
}
void ledsSetSpeed(int i)
{
    if (i >= 0 && i < NSPEED) {
        curSpeed = i;
        saveSettings();
    }
}

int ledsBrightCount()
{
    return NBRIGHT;
}
const char *ledsBrightName(int i)
{
    return BRIGHT_NAMES[(i >= 0 && i < NBRIGHT) ? i : 2];
}
int ledsBright()
{
    return curBright;
}
void ledsSetBright(int i)
{
    if (i >= 0 && i < NBRIGHT) {
        curBright = i;
        saveSettings();
    }
}

bool ledsMoonFollows()
{
    return moonFollows;
}
void ledsSetMoonFollows(bool on)
{
    moonFollows = on;
    applyMoonOwner();
    saveSettings();
}
bool ledsKeyFlash()
{
    return keyFlash;
}
void ledsSetKeyFlash(bool on)
{
    keyFlash = on;
    saveSettings();
}

void ledsKeyPressed(int keyBit)
{
    if (keyBit < 0 || keyBit > 15)
        return;
    int led = KEY_BIT_TO_LED[keyBit];
    if (led < 0)
        return;
    flash[led] = 1.0f;
    if (npending < sizeof(pending))
        pending[npending++] = (int8_t)led;
}

void ledsCelebrate()
{
    celebrateLeft = 6.0f;
}

} // namespace badge

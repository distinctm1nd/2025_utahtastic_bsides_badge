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
#include <string.h>

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
// light output (after gamma) for each setting; about what the 2025 levels gave, except "Dim",
// which used to round down to nearly off
static const float BRIGHT_LEVEL[] = {0.02f, 0.04f, 0.08f, 0.25f, 1.0f};
static const int NSPEED = sizeof(SPEED_MUL) / sizeof(SPEED_MUL[0]);
static const int NBRIGHT = sizeof(BRIGHT_LEVEL) / sizeof(BRIGHT_LEVEL[0]);

static int curScene = 0;
static int curSpeed = 2;
static int curBright = 2;
static bool moonFollows = true; // the moon is part of the scenes unless set to the app colour
static bool keyFlash = true;
static bool sceneReset = true;
static float sceneTime = 0;
static float flash[KEYPAD_LEDS];
static int8_t pending[4];
static uint8_t npending = 0;
static float celebrateLeft = 0;
static Rgb frame[NUM_LEDS];
static int offMode = 0;      // LEDs while the screen is asleep: 0 stay on (default), 1 dim, 2 off
static bool nightMode = false; // caps brightness at "Dim"
static float alertLeft = 0, alertLen = 0;
static bool alertDm = false;
static float pulseLeft = 0;
static float flourishLeft = 0;
static bool syncedScene = false; // scene came from a light show (not saved)
static Rgb sceneBuf[NUM_LEDS];  // what the scene drew this frame
static Rgb base[NUM_LEDS];      // the scene after the crossfade, before overlays
static Rgb fadeFrom[NUM_LEDS];  // the last frame of the previous scene
static float fadeLeft = 0;
static bool fadeStart = false;
static const float FADE_S = 0.6f; // crossfade between scenes, seconds
static bool forceShow = true;     // resend everything (start, moon handed back)

// Output stage. Gamma is applied before brightness, in floating point, so dim settings still
// get smooth steps (the old 8-bit table turned everything at "Dim" into on/off 0/1 values
// that blinked as the animation crossed them). Each channel only moves when the target is
// clearly a new value (hysteresis), so a slow fade never jitters between two neighbours.
static uint8_t outVal[NUM_LEDS][3];
static float powerScale = 1;
// 3 AA cells: keep the LED current modest so the supply doesn't sag (a sag makes the LEDs
// misread their data and flash random colours). Units: one channel fully on = 1.
static const float POWER_BUDGET = 22.0f;

static const char *NVS_NS = "utah";

static inline float gamma22(float v)
{
    v = v < 0 ? 0 : (v > 1 ? 1 : v);
    return powf(v, 2.2f);
}

static uint8_t settle(uint8_t prev, float target)
{
    if (target < 0.35f)
        return 0;
    if (target > 254.5f)
        return 255;
    if (fabsf(target - prev) < 0.75f)
        return prev;
    return (uint8_t)lroundf(target);
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
    p.putBool("moonfx", moonFollows);
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
    moonFollows = p.getBool("moonfx", true);
    keyFlash = p.getBool("keyfx", true);
    offMode = p.getUChar("ledoff", 0);
    nightMode = p.getBool("night", false);
    p.end();
    if (offMode > 2)
        offMode = 0;
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
        forceShow = true;
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
        c.out = sceneBuf;
        c.npressed = npending;
        for (int i = 0; i < npending; i++)
            c.pressed[i] = pending[i];
        npending = 0;
        sceneReset = false;

        const meshtastic_ModuleConfig_AmbientLightingConfig &al = moduleConfig.ambient_lighting;
        sceneSetAppColour(al.red / 255.0f, al.green / 255.0f, al.blue / 255.0f);
        sceneRender(curScene, c);

        // crossfade from the old scene so a change never jumps
        if (fadeStart) {
            fadeStart = false;
            memcpy(fadeFrom, base, sizeof(base));
            fadeLeft = FADE_S;
        }
        float k = fadeLeft > 0 ? 1 - fadeLeft / FADE_S : 1;
        k = k * k * (3 - 2 * k);
        for (int i = 0; i < NUM_LEDS; i++)
            for (int j = 0; j < 3; j++)
                base[i][j] = fadeFrom[i][j] + (sceneBuf[i][j] - fadeFrom[i][j]) * k;
        if (fadeLeft > 0)
            fadeLeft -= dt;

        // overlays go on a copy, so they never leak into the scene
        memcpy(frame, base, sizeof(frame));
        c.out = frame;

        // a badge app (game, fox hunt) can take over the keypad LEDs
        App *app = appActive();
        bool appLeds = app && app->keypadLeds(frame, now);
        if (keyFlash && !appLeds) {
            Rgb accent;
            sceneAccent(curScene, accent);
            overlayKeyFlash(c, flash, accent);
        }
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

        // linear light for each channel, then the power cap
        static float lin[NUM_LEDS][3];
        float total = 0;
        int lastLed = moonFollows ? NUM_LEDS : FIRST_MOON;
        for (int i = 0; i < lastLed; i++)
            for (int j = 0; j < 3; j++)
                total += (lin[i][j] = gamma22(frame[i][j]) * level);
        float want = total > POWER_BUDGET ? POWER_BUDGET / total : 1;
        // drop at once, recover slowly, so the cap itself never pumps
        float rate = dt * 2 > 1 ? 1 : dt * 2;
        powerScale = want < powerScale ? want : powerScale + (want - powerScale) * rate;

        bool changed[3] = {false, false, false}; // keypad, logo, moon strip
        for (int i = 0; i < lastLed; i++) {
            int strip = i < FIRST_LOGO ? 0 : (i < FIRST_MOON ? 1 : 2);
            for (int j = 0; j < 3; j++) {
                uint8_t v = settle(outVal[i][j], lin[i][j] * powerScale * 255.0f);
                if (v != outVal[i][j]) {
                    outVal[i][j] = v;
                    changed[strip] = true;
                }
            }
        }
        if (forceShow) {
            forceShow = false;
            changed[0] = changed[1] = changed[2] = true;
        }
        // only send data when something changed: fewer chances for a glitch on the LED line
        if (changed[0]) {
            for (int i = 0; i < KEYPAD_LEDS; i++)
                keypadStrip.setPixelColor(i, outVal[i][0], outVal[i][1], outVal[i][2]);
            keypadStrip.show();
        }
        if (changed[1]) {
            for (int i = 0; i < LOGO_LEDS; i++) {
                const uint8_t *v = outVal[FIRST_LOGO + i];
                logoStrip.setPixelColor(i, v[0], v[1], v[2]);
            }
            logoStrip.show();
        }
        if (moonFollows && changed[2]) {
            for (int i = 0; i < MOON_LEDS; i++) {
                const uint8_t *v = outVal[FIRST_MOON + i];
                moonStrip.setPixelColor(i, v[0], v[1], v[2]);
            }
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
    if (i != curScene)
        fadeStart = true;
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
    if (i != curScene) {
        sceneReset = true;
        fadeStart = true;
    }
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

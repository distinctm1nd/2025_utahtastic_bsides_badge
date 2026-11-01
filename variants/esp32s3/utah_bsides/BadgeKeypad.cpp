// Meshtastic glue for the badge keypad: polls the TCA9535 and feeds BadgeKeypadLogic.
#include "configuration.h"

#include "BadgeKeypad.h"
#include "BadgeKeypadLogic.h"
#include "BadgeLeds.h"
#include "BadgeMenu.h"
#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeStats.h"

#include "concurrency/OSThread.h"
#include "graphics/Screen.h"
#include "input/InputBroker.h"
#include "modules/CannedMessageModule.h"
#include <Wire.h>

#include <Preferences.h>

// The joystick is mounted at an angle; its correction is set on the badge (badge menu >
// Settings > Joystick) and kept in NVS ("utah": "joy", "joylean").

extern graphics::Screen *screen;

// The joystick sits about 45 degrees round on every badge: straight up closes the UP and RIGHT
// switches, straight down DOWN and LEFT. Angle 8 in Joystick setup (7 steps) reads those as up
// and down without any setup; the setup is only needed if a badge differs.
static const int JOY_DEFAULT_STEPS = 7;

// Joystick setup's four learned pushes, kept in NVS "joymap": 4 bits each, bit 16 = valid
static uint32_t packMap(const uint8_t m[4])
{
    return 0x10000u | (m[0] & 0xF) | ((m[1] & 0xF) << 4) | ((m[2] & 0xF) << 8) | ((uint32_t)(m[3] & 0xF) << 12);
}
static bool unpackMap(uint32_t v, uint8_t m[4])
{
    if (!(v & 0x10000u))
        return false;
    for (int i = 0; i < 4; i++)
        m[i] = (v >> (4 * i)) & 0xF;
    return m[0] && m[1] && m[2] && m[3];
}

namespace badge
{

static_assert((int)KP_SELECT == (int)INPUT_BROKER_SELECT, "input event ids");
static_assert((int)KP_UP == (int)INPUT_BROKER_UP, "input event ids");
static_assert((int)KP_DOWN == (int)INPUT_BROKER_DOWN, "input event ids");
static_assert((int)KP_LEFT == (int)INPUT_BROKER_LEFT, "input event ids");
static_assert((int)KP_RIGHT == (int)INPUT_BROKER_RIGHT, "input event ids");
static_assert((int)KP_CANCEL == (int)INPUT_BROKER_CANCEL, "input event ids");
static_assert((int)KP_BACK == (int)INPUT_BROKER_BACK, "input event ids");
static_assert((int)KP_ANYKEY == (int)INPUT_BROKER_ANYKEY, "input event ids");

static const uint8_t TCA9535_INPUT0 = 0x00;
static const uint8_t TCA9535_POLARITY0 = 0x04;
static const uint8_t TCA9535_CONFIG0 = 0x06;

static bool writeReg2(uint8_t reg, uint8_t a, uint8_t b)
{
    Wire.beginTransmission(BADGE_KEYPAD_ADDR);
    Wire.write(reg);
    Wire.write(a);
    Wire.write(b);
    return Wire.endTransmission() == 0;
}

// Returns false on an I2C error (state left unchanged). Bit 16 is the joystick centre press.
static bool readKeys(uint32_t &pressed)
{
    Wire.beginTransmission(BADGE_KEYPAD_ADDR);
    Wire.write(TCA9535_INPUT0);
    if (Wire.endTransmission(false) != 0)
        return false;
    if (Wire.requestFrom((uint8_t)BADGE_KEYPAD_ADDR, (uint8_t)2) != 2)
        return false;
    uint8_t port0 = Wire.read(); // read one byte at a time: the order matters
    uint8_t port1 = Wire.read();
    pressed = (uint16_t)~(port0 | (port1 << 8)); // keys pull the pins low
    if (digitalRead(BADGE_CENTER_PIN) == LOW)      // 100k pull-up, press = low
        pressed |= 1u << CENTER_BIT;
    return true;
}

class MeshtasticHooks : public KeypadHooks
{
  public:
    explicit MeshtasticHooks(Observable<const InputEvent *> *src) : source(src) {}

    void emit(KpEvent ev, uint8_t ch) override
    {
        InputEvent e = {};
        e.source = "BSIDES-KEYPAD";
        e.inputEvent = (input_broker_event)ev;
        e.kbchar = ch;
        source->notifyObservers(&e);
    }
    bool inFreeText() override
    {
        // true while composing a message (or typing a node-name search), i.e. when a letter
        // we send ends up in a text box (getRunState() is only public on RAK14014 builds)
        return cannedMessageModule && cannedMessageModule->isCharInputAllowed();
    }
    bool bannerShowing() override { return screen && screen->isOverlayBannerShowing(); }
    bool screenOff() override { return screen && !screen->isScreenOn(); }
    bool welcomeShowing() override { return welcomeActive(); }
    void welcomeDismiss() override { welcomeClose(); }
    void openBadgeMenu() override { menuOpen(); }
    void nextLedScene() override
    {
        ledsNextScene();
        menuShowSceneName();
    }
    void keyDown(int bit) override { ledsKeyPressed(bit); }
    void konami() override
    {
        ledsCelebrate();
        if (!appActive() && screen)
            screen->showSimpleBanner("Konami code!\nYou win.", 4000);
        statsAchieve(ACH_KONAMI);
    }
    bool appOpen() override { return appActive() != nullptr; }
    void appKey(int k, KeyAction a) override { badge::appKey(k, a); }
    void openCompose(int firstKey) override { badge::openCompose(firstKey); }
    void quickSend(int digit) override { menuQuickSend(digit); }

  private:
    Observable<const InputEvent *> *source;
};

class BadgeKeypad : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    BadgeKeypad() : concurrency::OSThread("BadgeKeypad"), hooks(this), logic(hooks)
    {
        Preferences p;
        if (p.begin("utah", true)) {
            logic.setJoystick(p.getInt("joy", JOY_DEFAULT_STEPS), p.getBool("joylean", false));
            uint8_t masks[4];
            if (unpackMap(p.getUInt("joymap", 0), masks))
                logic.setJoystickMap(masks, p.getBool("joylean", false));
            p.end();
        }
        if (inputBroker)
            inputBroker->registerSource(this);
        // all 16 pins inputs, normal polarity (both are the power-on defaults; set anyway)
        writeReg2(TCA9535_CONFIG0, 0xFF, 0xFF);
        pinMode(BADGE_CENTER_PIN, INPUT_PULLUP);
        writeReg2(TCA9535_POLARITY0, 0x00, 0x00);
    }
    uint32_t state() const { return logic.state(); }

  protected:
    int32_t runOnce() override
    {
        uint32_t keys;
        uint32_t now = millis();
        if (readKeys(keys))
            logic.update(keys, now);
        // a fresh time: key handling above stamps letters with millis(), and an older 'now'
        // here made the editor think a tapped letter was already a second old (U came out TT)
        appTick(millis()); // the open badge app, if any
        menuPoll();   // opens badge sub-menus queued by menu callbacks
        tttPoll();    // tic-tac-toe invitations
        return 10;
    }

  public:
    void emitNow(KpEvent ev, uint8_t ch) { hooks.emit(ev, ch); }
    void setJoystick(int steps, bool lean) { logic.setJoystick(steps, lean); }
    void setJoystickMap(const uint8_t masks[4], bool lean) { logic.setJoystickMap(masks, lean); }

  private:
    MeshtasticHooks hooks;
    KeypadLogic logic;
};

static BadgeKeypad *keypad = nullptr;

void keypadStart()
{
    if (!keypad)
        keypad = new BadgeKeypad();
}

void keypadPokeAwake()
{
    if (keypad && inputBroker)
        keypad->emitNow(KP_NONE, 0); // counts as input, so the screen stays on
}

void keypadInjectText(const char *text, bool send)
{
    if (!keypad || !text)
        return;
    // type it into Meshtastic's text box (opens it), then Enter
    for (const char *c = text; *c; c++)
        if (*c >= 32 && *c <= 126)
            keypad->emitNow(KP_ANYKEY, (uint8_t)*c);
    if (send)
        keypad->emitNow(KP_SELECT, 0);
}

void keypadSetJoystick(int steps, bool lean, bool save)
{
    if (keypad)
        keypad->setJoystick(steps, lean);
    if (save) {
        Preferences p;
        if (p.begin("utah", false)) {
            p.putInt("joy", steps);
            p.putBool("joylean", lean);
            p.putUInt("joymap", 0); // an angle replaces any learned pushes
            p.end();
        }
    }
}

void keypadSetJoystickMap(const uint8_t masks[4], bool lean, bool save)
{
    if (keypad)
        keypad->setJoystickMap(masks, lean);
    if (save) {
        Preferences p;
        if (p.begin("utah", false)) {
            p.putUInt("joymap", packMap(masks));
            p.putBool("joylean", lean);
            p.end();
        }
    }
}

bool keypadGetJoystickMap(uint8_t masks[4], bool &lean)
{
    bool ok = false;
    lean = false;
    Preferences p;
    if (p.begin("utah", true)) {
        ok = unpackMap(p.getUInt("joymap", 0), masks);
        lean = p.getBool("joylean", false);
        p.end();
    }
    return ok;
}

void keypadGetJoystick(int &steps, bool &lean)
{
    steps = JOY_DEFAULT_STEPS;
    lean = false;
    Preferences p;
    if (p.begin("utah", true)) {
        steps = p.getInt("joy", JOY_DEFAULT_STEPS);
        lean = p.getBool("joylean", false);
        p.end();
    }
}

uint32_t keypadState()
{
    return keypad ? keypad->state() : 0;
}

} // namespace badge

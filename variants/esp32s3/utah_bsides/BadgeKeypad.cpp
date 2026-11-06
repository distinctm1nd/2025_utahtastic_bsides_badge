// Meshtastic glue for the badge keypad: polls the TCA9535 and feeds BadgeKeypadLogic.
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeHome.h"
#include "BadgeKeypad.h"
#include "BadgeKeypadLogic.h"
#include "BadgeLeds.h"
#include "BadgeLock.h"
#include "BadgeMenu.h"
#include "BadgePortal.h"
#include "BadgeStats.h"

#include "concurrency/OSThread.h"
#include "graphics/Screen.h"
#include "input/InputBroker.h"
#include "modules/CannedMessageModule.h"
#include <Wire.h>

#include <Preferences.h>

// The joystick is mounted at an angle; its correction is set on the badge (badge menu >
// Settings > Joystick) and kept in NVS ("utah": "joy", "joylean").

// The joystick sits about 45 degrees round on every badge: straight up closes the UP and RIGHT
// switches, straight down DOWN and LEFT. Angle 8 in Joystick setup (7 steps) reads those as up
// and down without any setup; the setup is only needed if a badge differs. A push that catches
// one switch leans clockwise: straight down often closes only LEFT (measured: U12 R23 D4 L14).
static const int JOY_DEFAULT_STEPS = 7;
static const bool JOY_DEFAULT_LEAN = true;

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
    if (digitalRead(BADGE_CENTER_PIN) == LOW)    // 100k pull-up, press = low
        pressed |= 1u << CENTER_BIT;
    return true;
}

class MeshtasticHooks : public KeypadHooks
{
  public:
    explicit MeshtasticHooks(Observable<const InputEvent *> *src) : source(src) {}

    void emit(KpEvent ev, uint8_t ch) override
    {
        // Meshtastic scrolls its quick-message list with left/right too, trapping the user on
        // that page: close the list so left/right turn the page like everywhere else
        if ((ev == KP_LEFT || ev == KP_RIGHT) && cannedMessageModule && cannedMessageModule->shouldDraw() &&
            !cannedMessageModule->isFreeTextActive() && !(screen && screen->isOverlayBannerShowing()))
            send(KP_CANCEL, 0);
        else if ((ev == KP_LEFT || ev == KP_RIGHT) && messagesTakePageTurn(ev == KP_RIGHT ? 1 : -1))
            return; // the badge's Messages screen opened in place of Meshtastic's page
        send(ev, ch);
    }
    void send(KpEvent ev, uint8_t ch)
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
        return cannedMessageModule && cannedMessageModule->isFreeTextActive();
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
    // while an overlay menu (banner) is up, input drives the menu, not the app underneath it -
    // except an app that holdsKeys() (a compose/editor) keeps focus, so a keypress can't leak to
    // Meshtastic and open its on-screen keyboard over ours.
    bool appOpen() override
    {
        App *a = appActive();
        if (!a)
            return false;
        if (a->holdsKeys())
            return true;
        return !(screen && screen->isOverlayBannerShowing());
    }
    void appKey(int k, KeyAction a) override { badge::appKey(k, a); }
    void openCompose(int firstKey) override { badge::openCompose(firstKey); }
    void quickSend(int digit) override { menuShortcut(digit); } // hold a number: its shortcut
    bool appShortcuts() override
    {
        App *a = appActive();
        return a && a->numberShortcuts();
    }
    void menuBack() override { badge::menuBack(); }
    void lockNow() override
    {
        if (lockEnabled())
            badge::lockNow();
        else if (screen)
            screen->showSimpleBanner("No PIN set yet:\nSettings > Screen lock", 2500);
    }

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
            logic.setJoystick(p.getInt("joy", JOY_DEFAULT_STEPS), p.getBool("joylean", JOY_DEFAULT_LEAN));
            uint8_t masks[4];
            if (unpackMap(p.getUInt("joymap", 0), masks))
                logic.setJoystickMap(masks, p.getBool("joylean", JOY_DEFAULT_LEAN));
            p.end();
        }
        if (inputBroker)
            inputBroker->registerSource(this);
        // all 16 pins inputs, normal polarity (both are the power-on defaults; set anyway)
        writeReg2(TCA9535_CONFIG0, 0xFF, 0xFF);
        pinMode(BADGE_CENTER_PIN, INPUT_PULLUP);
        writeReg2(TCA9535_POLARITY0, 0x00, 0x00);
        // TFT backlight (active HIGH): Meshtastic's TFTDisplay no longer owns this pin (see
        // variant.h BADGE_TFT_BL), so drive it on ourselves. The badge display stays on.
        pinMode(BADGE_TFT_BL, OUTPUT);
        digitalWrite(BADGE_TFT_BL, HIGH);
    }
    uint32_t state() const { return logic.state(); }

  protected:
    int32_t runOnce() override
    {
        // Hold back key handling until the badge has taken the screen after the boot screens,
        // so a press while the boot images are still up can't leak to a Meshtastic page or the
        // menu. Once the first badge screen (intro / lock / home) is up, input is live for good.
        static bool armed = false;
        if (!armed && lockBootReady() && (appActive() || lockLocked()))
            armed = true;
        uint32_t keys;
        uint32_t now = millis();
        if (readKeys(keys)) {
            lockPoll(keys, now);
            if (armed && (!lockLocked() || lockShowing())) // not yet armed, or locked with no PIN screen: keys go nowhere
                logic.update(keys, now);
        }
        // a fresh time: key handling above stamps letters with millis(), and an older 'now'
        // here made the editor think a tapped letter was already a second old (U came out TT)
        appTick(millis()); // the open badge app, if any
        messagesPoll(millis());
        portalPoll();       // Badge WiFi keeps serving even under the lock screen (it owns its idle time-out)
        homePoll(millis()); // the pet-home is the default screen
        menuPoll();         // opens badge sub-menus queued by menu callbacks
        pagesPoll();        // carries out a Meshtastic-page jump picked in the Pages overlay
        tttPoll();          // tic-tac-toe invitations
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
    lean = JOY_DEFAULT_LEAN;
    Preferences p;
    if (p.begin("utah", true)) {
        ok = unpackMap(p.getUInt("joymap", 0), masks);
        lean = p.getBool("joylean", JOY_DEFAULT_LEAN);
        p.end();
    }
    return ok;
}

void keypadGetJoystick(int &steps, bool &lean)
{
    steps = JOY_DEFAULT_STEPS;
    lean = JOY_DEFAULT_LEAN;
    Preferences p;
    if (p.begin("utah", true)) {
        steps = p.getInt("joy", JOY_DEFAULT_STEPS);
        lean = p.getBool("joylean", JOY_DEFAULT_LEAN);
        p.end();
    }
}

bool keypadTyping()
{
    App *a = appActive();
    if (a)
        return a->typingLights();
    return cannedMessageModule && cannedMessageModule->isFreeTextActive();
}

uint32_t keypadState()
{
    return keypad ? keypad->state() : 0;
}

} // namespace badge

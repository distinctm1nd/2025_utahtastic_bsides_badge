// Meshtastic glue for the badge keypad: polls the TCA9535 and feeds BadgeKeypadLogic.
#include "configuration.h"

#include "BadgeKeypad.h"
#include "BadgeKeypadLogic.h"
#include "BadgeLeds.h"
#include "BadgeMenu.h"

#include "concurrency/OSThread.h"
#include "graphics/Screen.h"
#include "input/InputBroker.h"
#include "modules/CannedMessageModule.h"
#include <Wire.h>

// The joystick is mounted turned with the screen. If its directions feel wrong, change
// this to 1, 2 or 3 (quarter turns clockwise).
#ifndef BADGE_JOYSTICK_ROTATION
#define BADGE_JOYSTICK_ROTATION 0
#endif

extern graphics::Screen *screen;

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

// Returns false on an I2C error (state left unchanged).
static bool readKeys(uint16_t &pressed)
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
        if (screen)
            screen->showSimpleBanner("Konami code!\nYou win.", 4000);
    }

  private:
    Observable<const InputEvent *> *source;
};

class BadgeKeypad : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    BadgeKeypad() : concurrency::OSThread("BadgeKeypad"), hooks(this), logic(hooks, BADGE_JOYSTICK_ROTATION)
    {
        if (inputBroker)
            inputBroker->registerSource(this);
        // all 16 pins inputs, normal polarity (both are the power-on defaults; set anyway)
        writeReg2(TCA9535_CONFIG0, 0xFF, 0xFF);
        writeReg2(TCA9535_POLARITY0, 0x00, 0x00);
    }
    uint16_t state() const { return logic.state(); }

  protected:
    int32_t runOnce() override
    {
        uint16_t keys;
        if (readKeys(keys))
            logic.update(keys, millis());
        menuPoll(); // opens badge sub-menus queued by menu callbacks
        return 10;
    }

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

uint16_t keypadState()
{
    return keypad ? keypad->state() : 0;
}

} // namespace badge

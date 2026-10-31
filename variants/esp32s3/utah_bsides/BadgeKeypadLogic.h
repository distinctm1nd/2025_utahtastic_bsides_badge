// Keypad behaviour for the Utah BSides badge, kept free of Arduino/Meshtastic headers so it
// can be tested on a PC (tools/keypad_test.cpp). BadgeKeypad.cpp feeds it the raw TCA9535
// state every 10 ms and connects the hooks to Meshtastic.
//
// What the 2025 firmware got wrong, and what this does instead:
//  * No debounce: one press of a bouncy key read as two or three presses 20 ms apart,
//    which multi-tap turned into the next letter ('a' became 'b' or 'c'). Now each key
//    must hold a new state for DEBOUNCE_MS before it counts.
//  * Multi-tap erased with BACK even when the letter never reached the text box (screen
//    waking up, a menu open), deleting or leaving the wrong thing. Now it only replaces a
//    letter it knows is in the draft.
//  * UP sent the message and DOWN was backspace, which fought with the 2.7 menus. Now the
//    joystick only moves, # is Enter/Send and * is Backspace/Back.
//  * The Konami code never restarted on UP after a wrong key. Fixed.
#pragma once
#include <stdint.h>

namespace badge
{

// Values of input_broker_event in src/input/InputBroker.h (checked by static_assert there)
enum KpEvent : int {
    KP_NONE = 0,
    KP_SELECT = 10,
    KP_UP = 17,
    KP_DOWN = 18,
    KP_LEFT = 19,
    KP_RIGHT = 20,
    KP_CANCEL = 24,
    KP_BACK = 27,
    KP_ANYKEY = 0xff,
};

struct KeypadHooks {
    virtual ~KeypadHooks() = default;
    virtual void emit(KpEvent ev, uint8_t ch) = 0; // to Meshtastic's input broker
    virtual bool inFreeText() = 0;                 // a message draft is open
    virtual bool bannerShowing() = 0;              // a Meshtastic menu/picker is open
    virtual bool screenOff() = 0;
    virtual bool welcomeShowing() = 0;
    virtual void welcomeDismiss() = 0;
    virtual void openBadgeMenu() = 0;
    virtual void nextLedScene() = 0;
    virtual void keyDown(int bit) = 0; // LED feedback
    virtual void konami() = 0;
};

class KeypadLogic
{
  public:
    static const uint32_t DEBOUNCE_MS = 30;
    static const uint32_t MULTITAP_MS = 1000; // tap the same key again within this to cycle letters
    static const uint32_t HOLD_MS = 700;      // hold: * = badge menu, # = next LED scene, digit = the digit
    static const uint32_t REPEAT_DELAY_MS = 450;
    static const uint32_t REPEAT_MS = 130;

    // joystick bits 12..15 in the order UP, RIGHT, DOWN, LEFT before rotation
    explicit KeypadLogic(KeypadHooks &h, int joystickQuarterTurns = 0) : hooks(h), joyRot(joystickQuarterTurns & 3) {}

    uint16_t state() const { return stable; }

    void update(uint16_t raw, uint32_t now)
    {
        // debounce each key separately
        for (int b = 0; b < 16; b++) {
            uint16_t m = (uint16_t)(1u << b);
            bool r = raw & m, s = stable & m;
            if (r == s) {
                since[b] = 0;
                continue;
            }
            if (!since[b]) {
                since[b] = now ? now : 1;
                continue;
            }
            if (now - since[b] >= DEBOUNCE_MS) {
                since[b] = 0;
                stable ^= m;
                if (r)
                    onPress(b, now);
                else
                    onRelease(b, now);
            }
        }
        // holds and auto-repeat
        for (int b = 0; b < 16; b++) {
            if (!(stable & (1u << b)) || (swallow & (1u << b)))
                continue;
            uint32_t held = now - downAt[b];
            if (isJoystick(b)) {
                if (held >= REPEAT_DELAY_MS && now - lastRepeat[b] >= REPEAT_MS) {
                    lastRepeat[b] = now;
                    hooks.emit(joyEvent(b), 0);
                }
            } else if (!holdDone[b] && held >= HOLD_MS) {
                holdDone[b] = true;
                onHold(b);
            }
        }
    }

    static const char *lettersFor(int bit)
    {
        switch (bit) {
        case 11: return ".,?!'-@:1"; // 1
        case 10: return "abc2";
        case 9: return "def3";
        case 8: return "ghi4";
        case 7: return "jkl5";
        case 6: return "mno6";
        case 5: return "pqrs7";
        case 4: return "tuv8";
        case 3: return "wxyz9";
        case 1: return " 0";
        default: return nullptr;
        }
    }
    static char digitFor(int bit)
    {
        const char *l = lettersFor(bit);
        if (!l)
            return 0;
        while (l[1])
            l++;
        return *l;
    }

  private:
    KeypadHooks &hooks;
    int joyRot;
    uint16_t stable = 0;
    uint16_t swallow = 0; // keys whose press was used up (woke the screen, closed the welcome)
    uint32_t since[16] = {};
    uint32_t downAt[16] = {};
    uint32_t lastRepeat[16] = {};
    bool holdDone[16] = {};

    int tapBit = -1;      // key of the letter last typed into the draft
    int tapIdx = 0;       // which letter of that key
    uint32_t tapAt = 0;
    int konamiPos = 0;

    static bool isJoystick(int b) { return b >= 12; }
    static bool isDigit(int b) { return lettersFor(b) != nullptr; }

    KpEvent joyEvent(int b) const
    {
        static const KpEvent dirs[4] = {KP_UP, KP_RIGHT, KP_DOWN, KP_LEFT};
        return dirs[(b - 12 + joyRot) & 3];
    }

    void konamiStep(int b)
    {
        // up up down down left right left right B A ("2abc" is both B and A)
        static const KpEvent seq[] = {KP_UP, KP_UP, KP_DOWN, KP_DOWN, KP_LEFT, KP_RIGHT, KP_LEFT, KP_RIGHT};
        const int n = sizeof(seq) / sizeof(seq[0]);
        bool match;
        if (konamiPos < n)
            match = isJoystick(b) && joyEvent(b) == seq[konamiPos];
        else
            match = (b == 10); // the "2abc" key, twice
        if (match) {
            if (++konamiPos == n + 2) {
                konamiPos = 0;
                hooks.konami();
            }
        } else {
            // restart, letting this key begin a new attempt ("up up up down ..." still counts)
            bool up = isJoystick(b) && joyEvent(b) == KP_UP;
            konamiPos = up ? (konamiPos == 2 ? 2 : 1) : 0;
        }
    }

    void onPress(int b, uint32_t now)
    {
        downAt[b] = now;
        lastRepeat[b] = now;
        holdDone[b] = false;
        swallow &= (uint16_t)~(1u << b);
        hooks.keyDown(b);

        if (hooks.screenOff()) {
            swallow |= (uint16_t)(1u << b);
            hooks.emit(KP_NONE, 0); // wakes the screen; the broker drops the key itself
            return;
        }
        if (hooks.welcomeShowing()) {
            swallow |= (uint16_t)(1u << b);
            hooks.welcomeDismiss();
            return;
        }
        konamiStep(b);

        if (isJoystick(b)) {
            tapBit = -1;
            hooks.emit(joyEvent(b), 0);
            return;
        }
        if (b == KEY_STAR_BIT || b == KEY_HASH_BIT)
            return; // act on release (short) or hold

        if (hooks.bannerShowing()) {
            swallow |= (uint16_t)(1u << b); // digits do nothing in Meshtastic's menus
            return;
        }

        // multi-tap letters
        const char *letters = lettersFor(b);
        if (b == tapBit && now - tapAt < MULTITAP_MS && hooks.inFreeText()) {
            tapIdx = letters[tapIdx + 1] ? tapIdx + 1 : 0;
            hooks.emit(KP_BACK, 0x08); // replace the letter we typed a moment ago
        } else {
            tapIdx = 0;
        }
        hooks.emit(KP_ANYKEY, (uint8_t)letters[tapIdx]);
        tapAt = now;
        tapBit = hooks.inFreeText() ? b : -1; // only cycle a letter that reached the draft
    }

    void onHold(int b)
    {
        if (b == KEY_STAR_BIT) {
            tapBit = -1;
            if (!hooks.bannerShowing())
                hooks.openBadgeMenu();
            return;
        }
        if (b == KEY_HASH_BIT) {
            tapBit = -1;
            if (!hooks.bannerShowing())
                hooks.nextLedScene();
            return;
        }
        if (isDigit(b) && tapBit == b && hooks.inFreeText()) {
            // held a key: type its digit instead of the letter
            hooks.emit(KP_BACK, 0x08);
            hooks.emit(KP_ANYKEY, (uint8_t)digitFor(b));
            tapBit = -1;
        }
    }

    void onRelease(int b, uint32_t now)
    {
        (void)now;
        bool used = (swallow & (1u << b)) || holdDone[b];
        swallow &= (uint16_t)~(1u << b);
        if (used)
            return;
        if (b == KEY_STAR_BIT) {
            tapBit = -1;
            // in a Meshtastic menu * closes it; otherwise backspace (or back when nothing typed)
            hooks.emit(hooks.bannerShowing() ? KP_CANCEL : KP_BACK, 0x08);
        } else if (b == KEY_HASH_BIT) {
            tapBit = -1;
            hooks.emit(KP_SELECT, 0); // Enter: send the draft / choose in a menu
        }
    }

    static const int KEY_STAR_BIT = 0;
    static const int KEY_HASH_BIT = 2;
};

} // namespace badge

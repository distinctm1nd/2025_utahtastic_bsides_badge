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
//
// Three modes:
//  * a badge app is open (game, name tag, compose...): every key goes to the app as
//    press/release/hold/repeat (hooks.appKey), nothing reaches Meshtastic
//  * on Meshtastic's own screens: tapping a digit opens the badge's message editor, holding
//    a digit offers quick-send of canned message N
//  * in Meshtastic's own text box (replying from the message screen, node search): plain
//    multi-tap typed straight into it
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

// Keys as badge apps see them. Digits/'*'/'#' keep their TCA9535 bit numbers; the joystick
// is reported by direction after BADGE_JOYSTICK_ROTATION is applied.
enum AppKey : int {
    AK_STAR = 0,
    AK_0 = 1,
    AK_HASH = 2,
    AK_9 = 3,
    AK_8 = 4,
    AK_7 = 5,
    AK_6 = 6,
    AK_5 = 7,
    AK_4 = 8,
    AK_3 = 9,
    AK_2 = 10,
    AK_1 = 11,
    AK_UP = 12,
    AK_RIGHT = 13,
    AK_DOWN = 14,
    AK_LEFT = 15,
};
enum KeyAction : int { KA_PRESS, KA_RELEASE, KA_HOLD, KA_REPEAT };

// digit 0..9 for a digit key, -1 otherwise
inline int appKeyDigit(int k)
{
    static const int8_t d[12] = {-1, 0, -1, 9, 8, 7, 6, 5, 4, 3, 2, 1};
    return (k >= 0 && k < 12) ? d[k] : -1;
}

struct KeypadHooks {
    virtual ~KeypadHooks() = default;
    virtual void emit(KpEvent ev, uint8_t ch) = 0; // to Meshtastic's input broker
    virtual bool inFreeText() = 0;                 // Meshtastic's own text box is open
    virtual bool bannerShowing() = 0;              // a Meshtastic menu/picker is open
    virtual bool screenOff() = 0;
    virtual bool welcomeShowing() = 0;
    virtual void welcomeDismiss() = 0;
    virtual void openBadgeMenu() = 0;
    virtual void nextLedScene() = 0;
    virtual void keyDown(int bit) = 0; // LED feedback
    virtual void konami() = 0;
    virtual bool appOpen() = 0;                         // a badge app has the keys
    virtual void appKey(int key, KeyAction action) = 0; // key for the open app
    virtual void openCompose(int firstKey) = 0;         // tap a digit on a Meshtastic screen
    virtual void quickSend(int digit) = 0;              // hold a digit on a Meshtastic screen
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
                    if (appMode & (1u << b))
                        hooks.appKey(appKeyOf(b), KA_REPEAT);
                    else
                        hooks.emit(joyEvent(b), 0);
                }
            } else if (!holdDone[b] && held >= HOLD_MS) {
                holdDone[b] = true;
                if (appMode & (1u << b))
                    hooks.appKey(b, KA_HOLD);
                else
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
    uint16_t swallow = 0;  // keys whose press was used up (woke the screen, closed the welcome)
    uint16_t appMode = 0;  // keys whose press went to a badge app (their release/hold go there too)
    uint16_t homeTap = 0;  // digit pressed on a Meshtastic screen: decide on release (compose) or hold
    uint32_t since[16] = {};
    uint32_t downAt[16] = {};
    uint32_t lastRepeat[16] = {};
    bool holdDone[16] = {};

    int tapBit = -1; // key of the letter last typed into Meshtastic's text box
    int tapIdx = 0;  // which letter of that key
    uint32_t tapAt = 0;
    int konamiPos = 0;

    static const int KEY_STAR_BIT = 0;
    static const int KEY_HASH_BIT = 2;

    static bool isJoystick(int b) { return b >= 12; }
    static bool isDigit(int b) { return lettersFor(b) != nullptr; }

    int dirIndex(int b) const { return (b - 12 + joyRot) & 3; } // 0 up, 1 right, 2 down, 3 left
    KpEvent joyEvent(int b) const
    {
        static const KpEvent dirs[4] = {KP_UP, KP_RIGHT, KP_DOWN, KP_LEFT};
        return dirs[dirIndex(b)];
    }
    int appKeyOf(int b) const { return isJoystick(b) ? AK_UP + dirIndex(b) : b; }

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
        uint16_t m = (uint16_t)(1u << b);
        downAt[b] = now;
        lastRepeat[b] = now;
        holdDone[b] = false;
        swallow &= (uint16_t)~m;
        appMode &= (uint16_t)~m;
        homeTap &= (uint16_t)~m;
        hooks.keyDown(b);

        if (hooks.screenOff()) {
            swallow |= m;
            hooks.emit(KP_NONE, 0); // wakes the screen; the broker drops the key itself
            return;
        }
        if (hooks.welcomeShowing()) {
            swallow |= m;
            hooks.welcomeDismiss();
            return;
        }
        konamiStep(b);

        if (hooks.appOpen()) {
            appMode |= m;
            tapBit = -1;
            hooks.appKey(appKeyOf(b), KA_PRESS);
            return;
        }
        if (isJoystick(b)) {
            tapBit = -1;
            hooks.emit(joyEvent(b), 0);
            return;
        }
        if (b == KEY_STAR_BIT || b == KEY_HASH_BIT)
            return; // act on release (short) or hold

        if (hooks.bannerShowing()) {
            swallow |= m; // digits do nothing in Meshtastic's menus
            return;
        }
        if (!hooks.inFreeText()) {
            homeTap |= m; // tap = open the badge editor, hold = quick-send
            tapBit = -1;
            return;
        }

        // multi-tap letters straight into Meshtastic's text box
        const char *letters = lettersFor(b);
        if (b == tapBit && now - tapAt < MULTITAP_MS) {
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
        uint16_t m = (uint16_t)(1u << b);
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
        if (homeTap & m) {
            homeTap &= (uint16_t)~m;
            hooks.quickSend(digitFor(b) - '0');
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
        uint16_t m = (uint16_t)(1u << b);
        if (appMode & m) {
            appMode &= (uint16_t)~m;
            hooks.appKey(appKeyOf(b), KA_RELEASE);
            return;
        }
        bool used = (swallow & m) || holdDone[b];
        swallow &= (uint16_t)~m;
        if (homeTap & m) {
            homeTap &= (uint16_t)~m;
            if (!used)
                hooks.openCompose(b);
            return;
        }
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
};

} // namespace badge

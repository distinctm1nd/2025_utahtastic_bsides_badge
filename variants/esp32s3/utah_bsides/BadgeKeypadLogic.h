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
//    joystick only moves, the joystick centre press (and #) is Select/Send, * is Back.
//
// One rule on every screen: joystick up/left = previous, down/right = next, centre = select
// (hold centre = badge menu), * = back.
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
// is reported by direction after the angle correction (Settings > Joystick) is applied.
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
// The joystick centre press (GPIO7) is fed in as bit 16. Badge apps receive it as AK_HASH,
// so "select" behaves the same in every app.
static const int CENTER_BIT = 16;
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
    static const uint32_t CHORD_MS = 60; // wait this long for a second joystick switch (angled pushes)

    // Joystick correction (it is mounted at an angle):
    //   steps  turn the directions clockwise by steps x 45 degrees (0..7)
    //   lean   a push that closes only one switch lands on a diagonal after an odd number of
    //          steps; lean picks which neighbour it becomes
    // A push that closes two neighbouring switches at once is the direction between them.
    explicit KeypadLogic(KeypadHooks &h, int joySteps = 0, bool joyLean = false) : hooks(h)
    {
        setJoystick(joySteps, joyLean);
    }
    void setJoystick(int steps, bool lean)
    {
        joySteps = ((steps % 8) + 8) % 8;
        joyLean = lean;
        joyMapSet = false;
    }
    // learned from Joystick setup: the switches each push closed (up, right, down, left).
    // Works for any mounting, rotated or mirrored.
    void setJoystickMap(const uint8_t masks[4], bool lean)
    {
        buildJoyMap(masks, lean, joyMap);
        joyMapSet = true;
    }

    // switch mask -> direction from the four learned pushes. A push that closes exactly what a
    // learned push closed is that direction; otherwise the direction sharing the most switches
    // (a tie between two neighbours goes to the first, or the second with lean); -1 if unclear.
    static void buildJoyMap(const uint8_t masks[4], bool lean, int8_t out[16])
    {
        out[0] = -1;
        for (int m = 1; m < 16; m++) {
            int score[4], best = -99;
            for (int d = 0; d < 4; d++) {
                uint8_t k = masks[d] & 0xF;
                if (!k) {
                    score[d] = -99;
                    continue;
                }
                score[d] = 2 * __builtin_popcount(m & k) - __builtin_popcount(m & ~k) + (m == k ? 10 : 0);
                if (score[d] > best)
                    best = score[d];
            }
            int cand[4], n = 0;
            for (int d = 0; d < 4; d++)
                if (score[d] == best)
                    cand[n++] = d;
            if (best <= 0 || n > 2)
                out[m] = -1;
            else if (n == 1)
                out[m] = (int8_t)cand[0];
            else // two neighbours: 0 and 3 are neighbours too (up / left)
                out[m] = (int8_t)((cand[0] == 0 && cand[1] == 3) ? (lean ? 0 : 3) : (lean ? cand[1] : cand[0]));
        }
    }

    uint32_t state() const { return stable; }

    // joystick switches (bits 12..15 = UP, RIGHT, DOWN, LEFT as wired) -> direction 0 up,
    // 1 right, 2 down, 3 left after correction; -1 if no direction (nothing, or opposite switches)
    static int resolveJoystick(uint8_t mask, int steps, bool lean)
    {
        int vx = ((mask & 2) ? 1 : 0) - ((mask & 8) ? 1 : 0);
        int vy = ((mask & 1) ? 1 : 0) - ((mask & 4) ? 1 : 0);
        if (!vx && !vy)
            return -1;
        // angle in 45 degree units, counter-clockwise from right: 0 right, 2 up, 4 left, 6 down
        static const int8_t oct[3][3] = {{5, 6, 7}, {4, -1, 0}, {3, 2, 1}}; // [vy+1][vx+1]
        int a = oct[vy + 1][vx + 1];
        int p = ((a - steps) % 8 + 8) % 8;
        if (p & 1)
            p = lean ? (p + 1) % 8 : (p + 7) % 8;
        static const int8_t toDir[8] = {1, -1, 0, -1, 3, -1, 2, -1};
        return toDir[p];
    }

    // raw: TCA9535 bits 0..15 plus the centre press in bit 16, 1 = pressed
    void update(uint32_t raw, uint32_t now)
    {
        // debounce each key separately
        for (int b = 0; b < NKEYS; b++) {
            uint32_t m = 1u << b;
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
                if (isJoystick(b))
                    continue; // handled together below
                if (r)
                    onPress(b, now);
                else
                    onRelease(b, now);
            }
        }
        joyUpdate(now);
        // holds
        for (int b = 0; b < NKEYS; b++) {
            if (isJoystick(b))
                continue;
            if (!(stable & (1u << b)) || (swallow & (1u << b)))
                continue;
            uint32_t held = now - downAt[b];
            if (!holdDone[b] && held >= HOLD_MS) {
                holdDone[b] = true;
                if (appMode & (1u << b))
                    hooks.appKey(appKeyOf(b), KA_HOLD);
                else
                    onHold(b);
            }
        }
    }

    static const char *lettersFor(int bit)
    {
        switch (bit) {
        case 11: return ".,?!'-@:+/=1"; // 1 (+ / = for channel keys)
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
    int joySteps = 0;
    bool joyLean = false;
    bool joyMapSet = false;
    int8_t joyMap[16] = {};
    int joyDir = -1;          // direction being held, -1 none
    uint8_t joyTarget = 0;    // where it went: 0 Meshtastic, 1 badge app, 2 used up
    uint32_t joyCand = 0, joyDownAt = 0, joyRepeatAt = 0;
    static const int NKEYS = 17;
    uint32_t stable = 0;
    uint32_t swallow = 0;  // keys whose press was used up (woke the screen, closed the welcome)
    uint32_t appMode = 0;  // keys whose press went to a badge app (their release/hold go there too)
    uint32_t homeTap = 0;  // digit pressed on a Meshtastic screen: decide on release (compose) or hold
    uint32_t since[NKEYS] = {};
    uint32_t downAt[NKEYS] = {};
    uint32_t lastRepeat[NKEYS] = {};
    bool holdDone[NKEYS] = {};

    int tapBit = -1; // key of the letter last typed into Meshtastic's text box
    int tapIdx = 0;  // which letter of that key
    uint32_t tapAt = 0;
    int konamiPos = 0;

    static const int KEY_STAR_BIT = 0;
    static const int KEY_HASH_BIT = 2;

    static bool isJoystick(int b) { return b >= 12 && b <= 15; }
    static int appKeyOf(int b) { return b == CENTER_BIT ? AK_HASH : b; }
    static bool isDigit(int b) { return lettersFor(b) != nullptr; }

    static KpEvent dirEvent(int d)
    {
        static const KpEvent dirs[4] = {KP_UP, KP_RIGHT, KP_DOWN, KP_LEFT};
        return dirs[d & 3];
    }

    // code: a key bit 0..11, or 100 + joystick direction
    void konamiStep(int code)
    {
        // up up down down left right left right B A ("2abc" is both B and A)
        static const int seq[] = {100, 100, 102, 102, 103, 101, 103, 101};
        const int n = sizeof(seq) / sizeof(seq[0]);
        bool match = konamiPos < n ? code == seq[konamiPos] : code == 10; // the "2abc" key, twice
        if (match) {
            if (++konamiPos == n + 2) {
                konamiPos = 0;
                hooks.konami();
            }
        } else {
            // restart, letting this key begin a new attempt ("up up up down ..." still counts)
            konamiPos = code == 100 ? (konamiPos == 2 ? 2 : 1) : 0;
        }
    }

    // the joystick: its switches are read together so an angled push (two switches) is one
    // direction
    void joyUpdate(uint32_t now)
    {
        uint8_t m = (uint8_t)((stable >> 12) & 0xF);
        if (joyDir < 0) {
            if (!m) {
                joyCand = 0;
                return;
            }
            if (!joyCand) {
                joyCand = now ? now : 1;
                return;
            }
            if (now - joyCand < CHORD_MS)
                return;
            int d = joyMapSet ? joyMap[m & 0xF] : resolveJoystick(m, joySteps, joyLean);
            if (d < 0)
                return;
            joyDir = d;
            joyCand = 0;
            joyDownAt = joyRepeatAt = now;
            joyPress(d);
            return;
        }
        if (!m) {
            if (joyTarget == 1)
                hooks.appKey(AK_UP + joyDir, KA_RELEASE);
            joyDir = -1;
            return;
        }
        if (joyTarget != 2 && now - joyDownAt >= REPEAT_DELAY_MS && now - joyRepeatAt >= REPEAT_MS) {
            joyRepeatAt = now;
            if (joyTarget == 1)
                hooks.appKey(AK_UP + joyDir, KA_REPEAT);
            else
                hooks.emit(dirEvent(joyDir), 0);
        }
    }

    void joyPress(int d)
    {
        if (hooks.screenOff()) {
            joyTarget = 2;
            hooks.emit(KP_NONE, 0);
            return;
        }
        if (hooks.welcomeShowing()) {
            joyTarget = 2;
            hooks.welcomeDismiss();
            return;
        }
        konamiStep(100 + d);
        tapBit = -1;
        if (hooks.appOpen()) {
            joyTarget = 1;
            hooks.appKey(AK_UP + d, KA_PRESS);
        } else {
            joyTarget = 0;
            hooks.emit(dirEvent(d), 0);
        }
    }

    void onPress(int b, uint32_t now)
    {
        uint32_t m = 1u << b;
        downAt[b] = now;
        lastRepeat[b] = now;
        holdDone[b] = false;
        swallow &= ~m;
        appMode &= ~m;
        homeTap &= ~m;
        if (b != CENTER_BIT)
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
        if (b != CENTER_BIT)
            konamiStep(b);

        if (hooks.appOpen()) {
            appMode |= m;
            tapBit = -1;
            hooks.appKey(appKeyOf(b), KA_PRESS);
            return;
        }
        if (b == KEY_STAR_BIT || b == KEY_HASH_BIT || b == CENTER_BIT)
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
        tapBit = b; // the next tap of this key within a second replaces the letter
    }

    void onHold(int b)
    {
        uint32_t m = 1u << b;
        if (b == KEY_STAR_BIT || b == CENTER_BIT) {
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
            homeTap &= ~m;
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
        uint32_t m = 1u << b;
        if (appMode & m) {
            appMode &= ~m;
            hooks.appKey(appKeyOf(b), KA_RELEASE);
            return;
        }
        bool used = (swallow & m) || holdDone[b];
        swallow &= ~m;
        if (homeTap & m) {
            homeTap &= ~m;
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
        } else if (b == KEY_HASH_BIT || b == CENTER_BIT) {
            tapBit = -1;
            hooks.emit(KP_SELECT, 0); // select: choose in a menu / send the draft
        }
    }
};

} // namespace badge

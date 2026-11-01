// PC test for BadgeKeypadLogic.h: fake key bounce, multi-tap, holds and menus.
//   g++ -std=c++17 -I.. keypad_test.cpp -o keypad_test && ./keypad_test
#include "../BadgeKeypadLogic.h"
#include "../BadgeTextEntry.h"
#include "../BadgeBase64.h"
#include <cstdio>
#include <string>
#include <cstring>

using namespace badge;

struct FakeBadge : KeypadHooks {
    std::string draft; // what the Meshtastic text box would contain
    bool freetext = false, banner = false, screenIsOff = false, welcome = false;
    int menus = 0, scenes = 0, konamis = 0, selects = 0, cancels = 0, ups = 0, downs = 0;
    void emit(KpEvent ev, uint8_t ch) override
    {
        if (screenIsOff) { // broker drops it and wakes the screen
            screenIsOff = false;
            return;
        }
        if (banner) {
            if (ev == KP_CANCEL) { banner = false; cancels++; }
            if (ev == KP_SELECT) { banner = false; selects++; }
            if (ev == KP_UP) ups++;
            if (ev == KP_DOWN) downs++;
            return;
        }
        switch (ev) {
        case KP_ANYKEY:
            if (ch >= 32 && ch <= 126) { freetext = true; draft += (char)ch; }
            break;
        case KP_BACK:
            if (freetext) {
                if (draft.empty()) freetext = false;
                else draft.pop_back();
            }
            break;
        case KP_SELECT: selects++; if (freetext) { freetext = false; draft = "<sent>"; } break;
        case KP_UP: ups++; break;
        case KP_DOWN: downs++; break;
        default: break;
        }
    }
    bool inFreeText() override { return freetext; }
    bool bannerShowing() override { return banner; }
    bool screenOff() override { return screenIsOff; }
    bool welcomeShowing() override { return welcome; }
    void welcomeDismiss() override { welcome = false; }
    void openBadgeMenu() override { menus++; banner = true; }
    void nextLedScene() override { scenes++; }
    void keyDown(int) override {}
    void konami() override { konamis++; }
    // badge apps / home screen
    bool app = false;
    std::string appLog;
    int composeKey = -1, quick = -1;
    bool appOpen() override { return app; }
    void appKey(int k, KeyAction a) override
    {
        static const char *A = "PRHT";
        appLog += std::to_string(k) + A[a] + " ";
    }
    void openCompose(int k) override { composeKey = k; }
    void quickSend(int d) override { quick = d; }
};

static uint32_t now = 1000;
static uint32_t raw = 0;

static void run(KeypadLogic &k, uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 10) {
        now += 10;
        k.update(raw, now);
    }
}
// a press with contact bounce on the way down and up
static void press(KeypadLogic &k, int bit, uint32_t holdMs = 120, bool bouncy = true)
{
    uint32_t m = 1u << bit;
    if (bouncy)
        for (int i = 0; i < 3; i++) {
            raw |= m; run(k, 10);
            raw &= ~m; run(k, 10);
        }
    raw |= m;
    run(k, holdMs);
    if (bouncy)
        for (int i = 0; i < 2; i++) {
            raw &= ~m; run(k, 10);
            raw |= m; run(k, 10);
        }
    raw &= ~m;
    run(k, 60);
}

static int fails = 0;
static void expect(bool ok, const char *what)
{
    printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        fails++;
}

int main()
{
    {
        FakeBadge b; KeypadLogic k(b);
        b.freetext = true; // replying in Meshtastic's own text box
        press(k, 10); // one bouncy press of 2abc
        expect(b.draft == "a", "bouncy single press types one letter");
        press(k, 10); press(k, 10);
        expect(b.draft == "c", "three taps of 2 -> c");
        run(k, 1100);
        press(k, 10);
        expect(b.draft == "ca", "pause, then 2 again starts a new letter");
        press(k, 7); press(k, 7); press(k, 7);
        expect(b.draft == "cal", "5 x3 -> l");
        press(k, 1);
        expect(b.draft == "cal ", "0 -> space");
        press(k, 0);
        expect(b.draft == "cal", "* -> backspace");
        press(k, 3, 900);
        expect(b.draft == "cal9", "hold 9 -> digit 9");
        press(k, 2);
        expect(b.draft == "<sent>" && !b.freetext, "# sends");
    }
    {
        FakeBadge b; KeypadLogic k(b);
        press(k, 0, 900);
        expect(b.menus == 1 && b.draft.empty(), "hold * opens badge menu, types nothing");
        press(k, 14); press(k, 14);
        expect(b.downs == 2, "joystick moves in menu");
        press(k, 10);
        expect(b.draft.empty(), "digits ignored while a menu is open");
        press(k, 0);
        expect(b.cancels == 1 && !b.banner, "* closes the menu");
        press(k, 2, 900);
        expect(b.scenes == 1 && b.selects == 0, "hold # -> next LED scene, no Enter");
    }
    {
        FakeBadge b; KeypadLogic k(b);
        b.screenIsOff = true;
        b.freetext = true;
        press(k, 10);
        expect(b.draft.empty(), "first press only wakes the screen");
        press(k, 10);
        expect(b.draft == "a", "next press types");
        b.welcome = true;
        press(k, 2);
        expect(!b.welcome && b.selects == 0, "a key closes the welcome screen and is not used");
    }
    {
        FakeBadge b; KeypadLogic k(b);
        int seq[] = {12, 12, 12, 14, 14, 15, 13, 15, 13, 10, 10};
        for (int s : seq) press(k, s, 100, false), run(k, 1100);
        expect(b.konamis == 1, "Konami code (with an extra UP at the start)");
    }
    {
        FakeBadge b; KeypadLogic k(b);
        raw = 1u << 14; run(k, 1000); raw = 0; run(k, 100);
        expect(b.downs >= 4, "holding joystick DOWN auto-repeats");
    }
    {
        FakeBadge b; KeypadLogic k(b);
        press(k, 10);
        expect(b.composeKey == 10 && b.draft.empty(), "tap 2 on a Meshtastic screen opens the badge editor");
        press(k, 9, 900);
        expect(b.quick == 3 && b.composeKey == 10, "hold 3 on a Meshtastic screen -> quick send 3");
    }
    {
        FakeBadge b; KeypadLogic k(b);
        b.app = true;
        press(k, 7, 60, false);
        press(k, 2, 900, false);
        raw = 1u << 12; run(k, 700); raw = 0; run(k, 60);
        expect(b.appLog.rfind("7P 7R 2P 2H 2R 12P 12T", 0) == 0 && b.draft.empty(), "app gets press/hold/repeat/release, Meshtastic gets nothing");
        KeypadLogic k2(b, 2);
        b.appLog.clear();
        press(k2, 12, 100, false);
        expect(b.appLog == "13P 13R ", "joystick turned 90 degrees: UP switch reads as RIGHT");
        // angled joystick: two switches at once = the direction between them
        KeypadLogic k3(b, 7);
        b.appLog.clear();
        raw = (1u << 12) | (1u << 13); run(k3, 120); raw = 0; run(k3, 60);
        expect(b.appLog == "12P 12R ", "angled: UP+RIGHT switches together read as UP");
        b.appLog.clear();
        raw = (1u << 13); run(k3, 15); raw |= (1u << 14); run(k3, 120); raw = 0; run(k3, 60);
        expect(b.appLog == "13P 13R ", "angled: switches closing 15 ms apart still count as one push");
        KeypadLogic k4(b, 7, false), k5(b, 7, true);
        b.appLog.clear();
        press(k4, 12, 100, false); press(k5, 12, 100, false);
        expect(b.appLog == "12P 12R 15P 15R ", "angled: a single switch leans one way or the other");
    }
    {
        TextEntry t; t.reset();
        uint32_t ms = 0;
        auto tap = [&](int k) { t.key(k, KA_PRESS, ms); ms += 150; t.key(k, KA_RELEASE, ms); ms += 150; };
        auto wait = [&]() { ms += 1100; t.tick(ms); };
        tap(AK_4); tap(AK_4); wait(); tap(AK_4); tap(AK_4); tap(AK_4); // h i
        expect(strcmp(t.text(), "Hi") == 0, "editor: sentence case, multi-tap");
        tap(AK_1); tap(AK_0); wait(); tap(AK_2);
        expect(strcmp(t.text(), "Hi. A") == 0, "editor: capital after a full stop");
        tap(AK_STAR);
        expect(strcmp(t.text(), "Hi. ") == 0, "editor: * deletes");
        t.key(AK_9, KA_PRESS, ms); t.key(AK_9, KA_HOLD, ms + 800); t.key(AK_9, KA_RELEASE, ms + 900); ms += 1000;
        expect(strcmp(t.text(), "Hi. 9") == 0, "editor: hold a key for its digit");
        t.key(AK_LEFT, KA_PRESS, ms); t.key(AK_LEFT, KA_RELEASE, ms);
        wait(); tap(AK_8);
        expect(strcmp(t.text(), "Hi. T9") == 0, "editor: cursor moves, insert in the middle");
        t.key(AK_HASH, KA_PRESS, ms);
        expect(t.key(AK_HASH, KA_RELEASE, ms + 100) == TextEntry::TE_DONE, "editor: # finishes");
        t.key(AK_STAR, KA_PRESS, ms);
        expect(t.key(AK_STAR, KA_HOLD, ms + 800) == TextEntry::TE_CANCEL, "editor: hold * cancels");
    }
    {
        FakeBadge b; KeypadLogic k(b);
        press(k, 16);
        expect(b.selects == 1, "centre click = select on Meshtastic screens");
        press(k, 16, 900);
        expect(b.menus == 1 && b.selects == 1, "hold centre = badge menu");
        press(k, 16);
        expect(b.selects == 2 && b.cancels == 0 && !b.banner, "centre click chooses in a menu");
        b.app = true; b.appLog.clear();
        press(k, 16, 100, false);
        expect(b.appLog == "2P 2R ", "badge apps get the centre press as #");
    }
    {
        // the badge ticks the editor with a time a little older than the key's stamp
        // (it used to commit the letter at once, so 8 8 typed "Tt" instead of "U")
        TextEntry t; t.reset("", 200, TextEntry::CASE_LOWER);
        uint32_t ms = 5000;
        t.key(AK_8, KA_PRESS, ms + 1); t.key(AK_8, KA_RELEASE, ms + 80);
        t.tick(ms);
        t.tick(ms + 300);
        t.key(AK_8, KA_PRESS, ms + 400); t.key(AK_8, KA_RELEASE, ms + 480);
        t.tick(ms + 399);
        t.key(AK_8, KA_PRESS, ms + 700);
        expect(strcmp(t.text(), "v") == 0, "editor: 8 x3 = v even with a slightly older tick time");
        t.tick(ms + 1800);
        t.key(AK_8, KA_PRESS, ms + 1900);
        expect(strcmp(t.text(), "vt") == 0, "editor: a pause starts a new letter");
    }
    {
        // the badge's default: straight up closes UP+RIGHT, straight down DOWN+LEFT
        expect(KeypadLogic::resolveJoystick(1 | 2, 7, false) == 0, "default angle: U+R = up");
        expect(KeypadLogic::resolveJoystick(2 | 4, 7, false) == 1, "default angle: R+D = right");
        expect(KeypadLogic::resolveJoystick(4 | 8, 7, false) == 2, "default angle: D+L = down (was moving up)");
        expect(KeypadLogic::resolveJoystick(8 | 1, 7, false) == 3, "default angle: L+U = left");
    }
    {
        // Joystick setup's learned pushes (switch bits as wired: 1, 2, 4, 8)
        int8_t map[16];
        const uint8_t rot45[4] = {1 | 8, 1 | 2, 2 | 4, 4 | 8}; // up closes 1+4, ... (rotated)
        KeypadLogic::buildJoyMap(rot45, false, map);
        expect(map[1 | 8] == 0 && map[1 | 2] == 1 && map[2 | 4] == 2 && map[4 | 8] == 3, "learned: rotated stick, all four pushes");
        expect(map[1] >= 0 && map[1] != 2, "learned: one switch goes to a neighbouring direction");
        expect(map[1 | 4] == -1, "learned: opposite switches do nothing");
        const uint8_t mirror[4] = {1, 8, 4, 2}; // left and right wired the other way round
        KeypadLogic::buildJoyMap(mirror, false, map);
        expect(map[1] == 0 && map[8] == 1 && map[4] == 2 && map[2] == 3, "learned: mirrored stick");
        const uint8_t straight[4] = {1, 2, 4, 8};
        int8_t a[16], b[16];
        KeypadLogic::buildJoyMap(straight, false, a);
        KeypadLogic::buildJoyMap(straight, true, b);
        expect(a[3] != b[3] && (a[3] == 0 || a[3] == 1) && (b[3] == 0 || b[3] == 1), "learned: lean picks the other neighbour");
        FakeBadge fb; KeypadLogic k(fb);
        k.setJoystickMap(rot45, false);
        fb.downs = 0;
        uint32_t t = 1000;
        k.update(1u << 12 | 1u << 15, t); for (int i = 1; i < 12; i++) k.update(1u << 12 | 1u << 15, t + i * 10);
        k.update(0, t + 200); k.update(0, t + 300);
        expect(fb.ups == 1, "learned map is used for menu moves");
    }
    {
        // channel keys
        uint8_t k[32];
        char b[48];
        expect(base64Decode("AQ==", k, 32) == 1 && k[0] == 1, "base64: the default key AQ==");
        const uint8_t key16[16] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59, 0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};
        base64Encode(key16, 16, b);
        expect(strcmp(b, "1PG7OiApB1nwvP+rz05pAQ==") == 0, "base64: encode a 16-byte key");
        expect(base64Decode(b, k, 32) == 16 && memcmp(k, key16, 16) == 0, "base64: round trip");
        expect(base64Decode("1PG7OiApB1nwvP+rz05pAQ", k, 32) == 16, "base64: padding optional");
        expect(base64Decode("not a key!", k, 32) < 0, "base64: rejects junk");
        uint8_t k32[32];
        for (int i = 0; i < 32; i++) k32[i] = (uint8_t)(i * 7 + 3);
        base64Encode(k32, 32, b);
        expect(strlen(b) == 44 && base64Decode(b, k, 32) == 32 && memcmp(k, k32, 32) == 0, "base64: 32-byte key (44 chars)");
    }
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}

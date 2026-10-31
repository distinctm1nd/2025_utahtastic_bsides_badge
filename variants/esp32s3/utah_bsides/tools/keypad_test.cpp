// PC test for BadgeKeypadLogic.h: fake key bounce, multi-tap, holds and menus.
//   g++ -std=c++17 -I.. keypad_test.cpp -o keypad_test && ./keypad_test
#include "../BadgeKeypadLogic.h"
#include "../BadgeTextEntry.h"
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
static uint16_t raw = 0;

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
    uint16_t m = 1u << bit;
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
        for (int s : seq) press(k, s, 60, false), run(k, 1100);
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
        KeypadLogic k2(b, 1);
        b.appLog.clear();
        press(k2, 12, 60, false);
        expect(b.appLog == "13P 13R ", "joystick rotation applies to app keys");
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
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}

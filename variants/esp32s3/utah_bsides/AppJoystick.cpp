// Joystick setup: the joystick is mounted at an angle, so a push may close one switch or
// two, and not the one its label suggests. Push straight up, right, down and left (as you
// hold the badge); the badge learns which switches each push closes, so any mounting works,
// rotated or mirrored. Then try it (the arrow shows what each push now means) and save.
//
//   learning: push each direction the screen asks for, let go
//   testing:  push anywhere to see the result; 0 flip the lean (in-between pushes),
//             9 start over, centre/# save, * cancel
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeKeypad.h"
#include "BadgeKeypadLogic.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <Arduino.h>
#include <stdio.h>

namespace badge
{

class JoystickApp : public App
{
    enum State { JS_LEARN, JS_TEST };

  public:
    const char *name() const override { return "Joystick"; }
    uint32_t frameMs() const override { return 100; }
    void begin() override
    {
        state = JS_LEARN;
        step = 0;
        pushMask = 0;
        heldSince = 0;
        problem = nullptr;
        saved = false;
        lean = false;
        uint8_t m[4];
        bool l;
        if (keypadGetJoystickMap(m, l))
            lean = l;
    }
    void end() override {}
    void key(int k, KeyAction a) override
    {
        if (a != KA_PRESS)
            return;
        if (k == AK_STAR) {
            appClose();
            return;
        }
        if (state != JS_TEST)
            return;
        if (k == AK_HASH) {
            keypadSetJoystickMap(masks, lean, true);
            saved = true;
            appClose();
            if (screen)
                screen->showSimpleBanner("Joystick saved", 1500);
            return;
        }
        int dg = appKeyDigit(k);
        if (dg == 0) {
            lean = !lean;
            KeypadLogic::buildJoyMap(masks, lean, map);
            appRedraw();
        } else if (dg == 9) {
            begin();
            appRedraw();
        }
    }
    void tick(uint32_t now) override
    {
        uint8_t m = (uint8_t)((keypadState() >> 12) & 0xF);
        if (m) {
            pushMask |= m; // every switch the push touched
            heldSince = heldSince ? heldSince : now;
            if (state == JS_TEST && map[m] >= 0)
                shown = map[m];
            return;
        }
        if (!pushMask)
            return;
        uint8_t mask = pushMask;
        pushMask = 0;
        bool longEnough = now - heldSince > 60;
        heldSince = 0;
        if (!longEnough || state != JS_LEARN)
            return;
        problem = nullptr;
        for (int i = 0; i < step; i++)
            if (masks[i] == mask)
                problem = "Same as an earlier push";
        if (__builtin_popcount(mask) > 2)
            problem = "Too many switches";
        if (problem) {
            appRedraw();
            return; // ask for this direction again
        }
        masks[step++] = mask;
        if (step == 4) {
            KeypadLogic::buildJoyMap(masks, lean, map);
            // every learned push must read back as itself
            for (int d = 0; d < 4; d++)
                if (map[masks[d]] != d)
                    problem = "Pushes overlap";
            if (problem) {
                step = 0;
            } else {
                state = JS_TEST;
                shown = -1;
            }
        }
        appRedraw();
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        static const char *DIRS[] = {"UP", "RIGHT", "DOWN", "LEFT"};
        drawTitle(d, x, y, "Joystick setup", state == JS_TEST ? "#save" : "*cancel");
        int lh = fontHeight(FONT_SMALL);
        int top = y + lh + 4;
        // the four switches as wired, lit while closed
        uint8_t m = (uint8_t)((keypadState() >> 12) & 0xF);
        int cx = x + 26, cy = top + 28;
        drawSwitch(d, cx, cy - 18, m & 1, "1");
        drawSwitch(d, cx + 18, cy, m & 2, "2");
        drawSwitch(d, cx, cy + 18, m & 4, "3");
        drawSwitch(d, cx - 18, cy, m & 8, "4");
        int tx = x + 60;
        if (state == JS_LEARN) {
            d->drawString(tx, top, "Push straight");
            d->drawString(tx, top + lh, DIRS[step]);
            d->drawString(tx, top + 2 * lh, "then let go.");
            char b[24];
            snprintf(b, sizeof(b), "(%d of 4)", step + 1);
            d->drawString(tx, top + 3 * lh, b);
            if (problem)
                d->drawString(x + 2, y + d->height() - 2 * lh, problem);
            d->drawString(x + 2, y + d->height() - lh, "as you hold the badge");
            return;
        }
        d->drawString(tx, top, "Push = ");
        drawArrow(d, tx + 56, top + 6, shown);
        d->drawString(tx, top + lh, shown >= 0 ? DIRS[shown] : "(try it)");
        // what was learned, e.g. "U12 R23 D34 L41", so it can be reported
        char b[40];
        snprintf(b, sizeof(b), "U%s R%s D%s L%s", sw(masks[0]).s, sw(masks[1]).s, sw(masks[2]).s, sw(masks[3]).s);
        d->drawString(tx, top + 2 * lh, b);
        d->drawString(x + 2, y + d->height() - 2 * lh, "0 lean  9 redo");
        d->drawString(x + 2, y + d->height() - lh, "centre save  * cancel");
    }

  private:
    State state = JS_LEARN;
    int step = 0, shown = -1;
    bool lean = false, saved = false;
    uint8_t pushMask = 0, masks[4] = {};
    int8_t map[16] = {};
    uint32_t heldSince = 0;
    const char *problem = nullptr;

    struct Sw {
        char s[6];
    };
    static Sw sw(uint8_t m) // switch numbers 1..4 that a push closed
    {
        Sw r = {};
        int n = 0;
        for (int i = 0; i < 4; i++)
            if (m & (1 << i))
                r.s[n++] = (char)('1' + i);
        return r;
    }
    static void drawSwitch(OLEDDisplay *d, int x, int y, bool on, const char *label)
    {
        if (on)
            d->fillCircle(x, y, 7);
        else
            d->drawCircle(x, y, 7);
        d->setColor(on ? BLACK : WHITE);
        d->setTextAlignment(TEXT_ALIGN_CENTER);
        d->drawString(x, y - fontHeight(FONT_SMALL) / 2, label);
        d->setTextAlignment(TEXT_ALIGN_LEFT);
        d->setColor(WHITE);
    }
    static void drawArrow(OLEDDisplay *d, int x, int y, int dir)
    {
        if (dir < 0)
            return;
        static const int8_t DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
        int ex = x + DX[dir] * 8, ey = y + DY[dir] * 8, sx = x - DX[dir] * 8, sy = y - DY[dir] * 8;
        d->drawLine(sx, sy, ex, ey);
        d->drawLine(ex, ey, ex - DX[dir] * 4 + DY[dir] * 4, ey - DY[dir] * 4 + DX[dir] * 4);
        d->drawLine(ex, ey, ex - DX[dir] * 4 - DY[dir] * 4, ey - DY[dir] * 4 - DX[dir] * 4);
    }
};

static JoystickApp joystickApp;

void openJoystickSetup()
{
    appOpen(&joystickApp);
}

} // namespace badge

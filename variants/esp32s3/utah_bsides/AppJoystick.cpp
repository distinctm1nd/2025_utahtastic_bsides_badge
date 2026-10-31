// Joystick setup: the joystick is mounted at an angle, so "up" on the badge may close the
// UP switch, the RIGHT switch, or both. Push straight up, then straight right, and the badge
// works out the correction; then try it (the arrow shows what each push now means) and save.
//
//   calibrating: push straight up, let go; push straight right, let go
//   testing:     push anywhere to see the result
//                1-8 try other angles, 0 flip the lean, 9 calibrate again
//                # save, * cancel
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeKeypad.h"
#include "BadgeKeypadLogic.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <Arduino.h>
#include <stdio.h>

extern graphics::Screen *screen;

namespace badge
{

class JoystickApp : public App
{
    enum State { CAL_UP, CAL_RIGHT, TEST };

  public:
    const char *name() const override { return "Joystick"; }
    uint32_t frameMs() const override { return 100; }
    void begin() override
    {
        keypadGetJoystick(steps, lean);
        oldSteps = steps;
        oldLean = lean;
        keypadSetJoystick(0, false, false); // read the switches as wired while calibrating
        state = CAL_UP;
        pushMask = 0;
    }
    void end() override
    {
        if (!saved)
            keypadSetJoystick(oldSteps, oldLean, false);
        saved = false;
    }
    void key(int k, KeyAction a) override
    {
        if (a != KA_PRESS)
            return;
        if (k == AK_STAR) {
            appClose();
            return;
        }
        if (k == AK_HASH && state == TEST) {
            keypadSetJoystick(steps, lean, true);
            saved = true;
            appClose();
            if (screen)
                screen->showSimpleBanner("Joystick saved", 1500);
            return;
        }
        int dg = appKeyDigit(k);
        if (state != TEST || dg < 0)
            return;
        if (dg >= 1 && dg <= 8)
            steps = dg - 1;
        else if (dg == 0)
            lean = !lean;
        else if (dg == 9) {
            state = CAL_UP;
            keypadSetJoystick(0, false, false);
        }
    }
    void tick(uint32_t now) override
    {
        uint8_t m = (uint8_t)((keypadState() >> 12) & 0xF);
        if (m) {
            pushMask |= m; // every switch the push touched
            lastRaw = m;
            heldSince = heldSince ? heldSince : now;
            if (state == TEST) {
                int d = KeypadLogic::resolveJoystick(m, steps, lean);
                if (d >= 0)
                    shown = d;
            }
            return;
        }
        if (!pushMask)
            return;
        uint8_t mask = pushMask;
        pushMask = 0;
        bool longEnough = now - heldSince > 60;
        heldSince = 0;
        if (!longEnough)
            return;
        if (state == CAL_UP) {
            upMask = mask;
            state = CAL_RIGHT;
        } else if (state == CAL_RIGHT) {
            rightMask = mask;
            solve();
            state = TEST;
            shown = -1;
        }
        appRedraw();
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        drawTitle(d, x, y, "Joystick setup", state == TEST ? "#save" : "*cancel");
        int lh = fontHeight(FONT_SMALL);
        int top = y + lh + 4;
        // the four switches as wired, lit while closed
        uint8_t m = (uint8_t)((keypadState() >> 12) & 0xF);
        int cx = x + 28, cy = top + 30;
        drawSwitch(d, cx, cy - 18, m & 1, "U");
        drawSwitch(d, cx + 18, cy, m & 2, "R");
        drawSwitch(d, cx, cy + 18, m & 4, "D");
        drawSwitch(d, cx - 18, cy, m & 8, "L");
        int tx = x + 64;
        if (state != TEST) {
            d->drawString(tx, top, "Push the stick");
            d->drawString(tx, top + lh, state == CAL_UP ? "straight UP" : "straight RIGHT");
            d->drawString(tx, top + 2 * lh, "then let go.");
            d->drawString(tx, top + 4 * lh, state == CAL_UP ? "(step 1 of 2)" : "(step 2 of 2)");
            return;
        }
        static const char *DIRS[] = {"UP", "RIGHT", "DOWN", "LEFT"};
        d->drawString(tx, top, "Push = ");
        drawArrow(d, tx + 70, top + 6, shown);
        d->drawString(tx, top + lh, shown >= 0 ? DIRS[shown] : "(try it)");
        char b[32];
        snprintf(b, sizeof(b), "Angle %d, lean %s", steps + 1, lean ? "B" : "A");
        d->drawString(tx, top + 3 * lh, b);
        d->drawString(x + 2, y + d->height() - 2 * lh, "1-8 angle  0 lean  9 redo");
        d->drawString(x + 2, y + d->height() - lh, "# save   * cancel");
    }

  private:
    State state = CAL_UP;
    int steps = 0, oldSteps = 0, shown = -1;
    bool lean = false, oldLean = false, saved = false;
    uint8_t pushMask = 0, lastRaw = 0, upMask = 0, rightMask = 0;
    uint32_t heldSince = 0;

    // the first setting that reads both test pushes correctly (two-switch pushes give the
    // most information, so odd angles are tried first when one was seen)
    void solve()
    {
        bool pair = __builtin_popcount(upMask) == 2 || __builtin_popcount(rightMask) == 2;
        for (int pass = 0; pass < 2; pass++)
            for (int s = 0; s < 8; s++) {
                if (pass == 0 && pair != (s & 1))
                    continue;
                for (int l = 0; l < 2; l++)
                    if (KeypadLogic::resolveJoystick(upMask, s, l) == 0 && KeypadLogic::resolveJoystick(rightMask, s, l) == 1) {
                        steps = s;
                        lean = l;
                        return;
                    }
            }
        steps = 0;
        lean = false;
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
        // head
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

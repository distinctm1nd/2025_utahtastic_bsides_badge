// Skinwalker Mine: the Doom-style shooter (engine in BadgeMine.h).
//
//   joystick up/down   walk           joystick left/right   turn
//   4 / 6              step sideways  2 / 8                 walk (while turning with the stick)
//   centre / # / 5     shoot          0                     reload
//   hold *             quit
// The keypad shows your health; it flashes when you shoot and goes red when you're hit.
// The skull's heartbeat speeds up as a skinwalker closes in.
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeLeds.h"
#include "BadgeMine.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>

namespace badge
{

class MineApp : public App
{
  public:
    const char *name() const override { return "Skinwalker Mine"; }
    uint32_t frameMs() const override { return 66; } // about 15 frames a second
    bool keepAwake() const override { return true; }
    void begin() override
    {
        game.reset(millis() | 1);
        memset(&held, 0, sizeof(held));
        lastMs = millis();
        started = false;
    }
    void key(int k, KeyAction a) override
    {
        if (k == AK_STAR) {
            if (a == KA_HOLD)
                appClose();
            return;
        }
        bool down = a == KA_PRESS || a == KA_REPEAT || a == KA_HOLD;
        if (a != KA_PRESS && a != KA_RELEASE)
            return;
        int dg = appKeyDigit(k);
        if (k == AK_HASH || dg == 5) {
            if (a == KA_PRESS) {
                if (!started)
                    started = true;
                else if (game.dead || game.won)
                    begin(), started = true;
                else
                    fireQueued = true;
            }
            return;
        }
        if (k == AK_UP)
            held.fwd = down;
        else if (k == AK_DOWN)
            held.back = down;
        else if (k == AK_LEFT)
            held.left = down;
        else if (k == AK_RIGHT)
            held.right = down;
        else if (dg == 4)
            held.strafeL = down;
        else if (dg == 6)
            held.strafeR = down;
        else if (dg == 2)
            held.fwd = down;
        else if (dg == 8)
            held.back = down;
        else if (dg == 0 && a == KA_PRESS)
            reloadQueued = true;
    }
    void tick(uint32_t now) override
    {
        float dt = (now - lastMs) / 1000.0f;
        lastMs = now;
        if (!started)
            return;
        Mine::Input in = held;
        in.fire = fireQueued;
        in.reload = reloadQueued;
        fireQueued = reloadQueued = false;
        bool wasDead = game.dead, wasWon = game.won;
        if (game.update(dt, in))
            shotAt = now;
        if (game.won && !wasWon)
            ledsFlourish();
        (void)wasDead;
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        if (!started) {
            drawTitle(d, x, y, "Skinwalker Mine", "*hold quit");
            int lh = fontHeight(FONT_SMALL);
            const char *lines[] = {"Something is in the", "old silver mine.", "Clear it out.", "",
                                   "stick: walk / turn", "4 6: sidestep  0: reload", "centre: shoot / start"};
            for (int i = 0; i < 7; i++)
                d->drawString(x + 4, y + lh + 4 + i * (lh - 2), lines[i]);
            return;
        }
        game.render(fb);
        d->drawXbm(x, y, Mine::W, Mine::VIEW_H, fb);
        drawHud(d, x, y + Mine::VIEW_H);
        if (game.message && game.messageLeft > 0) {
            d->setFont(FONT_SMALL);
            int w = d->getStringWidth(game.message, (uint16_t)strlen(game.message));
            int lh = fontHeight(FONT_SMALL);
            d->setColor(BLACK);
            d->fillRect(x + (Mine::W - w) / 2 - 3, y + 2, w + 6, lh);
            d->setColor(WHITE);
            drawCentered(d, x, y + 2, game.message, FONT_SMALL);
        }
        if (game.dead || game.won) {
            char b[40];
            snprintf(b, sizeof(b), "%d/%d  %ds  centre: again", game.kills, game.total, (int)game.timeAlive);
            int lh = fontHeight(FONT_SMALL);
            d->setColor(BLACK);
            d->fillRect(x, y + 40, Mine::W, lh + 2);
            d->setColor(WHITE);
            drawCentered(d, x, y + 41, b, FONT_SMALL);
        }
    }
    bool keypadLeds(Rgb *out, uint32_t now) override
    {
        if (!started)
            return false;
        // keys: health, green to red, filling from the bottom row
        static const int8_t ORDER[12] = {9, 10, 11, 8, 7, 6, 3, 4, 5, 2, 1, 0}; // LED indices, bottom first
        int lit = (game.health * 12 + 99) / 100;
        float h = game.health / 100.0f;
        bool shot = now - shotAt < 90, hurt = game.hurtLeft > 0;
        for (int i = 0; i < 12; i++) {
            int led = ORDER[i];
            Rgb &c = out[led];
            if (hurt)
                c[0] = 1, c[1] = 0, c[2] = 0;
            else if (shot)
                c[0] = 1, c[1] = 0.6f, c[2] = 0.15f; // muzzle flash
            else if (i < lit)
                c[0] = 1 - h, c[1] = h * 0.8f, c[2] = 0;
            else
                c[0] = c[1] = c[2] = 0.02f;
        }
        // the skull: a heartbeat, faster as one gets close
        float near = game.nearestAwake();
        float rate = near < 2 ? 3.0f : near < 5 ? 1.8f : 0.9f;
        float p = fmodf(now / 1000.0f * rate, 1.0f);
        float beat = expf(-p * 18) + 0.6f * expf(-fabsf(p - 0.25f) * 22);
        out[SKULL_LED][0] = 0.1f + 0.9f * beat;
        out[SKULL_LED][1] = 0;
        out[SKULL_LED][2] = 0;
        return true;
    }

  private:
    Mine game;
    uint8_t fb[Mine::FB_BYTES];
    Mine::Input held;
    bool fireQueued = false, reloadQueued = false, started = false;
    uint32_t lastMs = 0, shotAt = 0;

    void drawHud(OLEDDisplay *d, int16_t x, int16_t y)
    {
        int H = d->height() - Mine::VIEW_H;
        d->drawHorizontalLine(x, y, Mine::W);
        // health bar
        d->drawRect(x + 2, y + 4, 42, H - 8);
        d->fillRect(x + 3, y + 5, 40 * game.health / 100, H - 10);
        // the cylinder: six chambers, full or empty
        for (int i = 0; i < 6; i++) {
            int cx = x + 56 + i * 8, cy = y + H / 2;
            if (game.reloadLeft > 0 && ((millis() / 150) & 1))
                continue;
            if (i < game.cylinder)
                d->fillCircle(cx, cy, 3);
            else
                d->drawCircle(cx, cy, 3);
        }
        char b[16];
        snprintf(b, sizeof(b), "+%d", game.ammo);
        d->setFont(FONT_SMALL);
        d->drawString(x + 104, y + (H - fontHeight(FONT_SMALL)) / 2, b);
        snprintf(b, sizeof(b), "%d/%d", game.kills, game.total);
        d->setTextAlignment(TEXT_ALIGN_RIGHT);
        d->drawString(x + Mine::W - 2, y + (H - fontHeight(FONT_SMALL)) / 2, b);
        d->setTextAlignment(TEXT_ALIGN_LEFT);
    }
};

static MineApp mineApp;

void openMine()
{
    appOpen(&mineApp);
}

} // namespace badge

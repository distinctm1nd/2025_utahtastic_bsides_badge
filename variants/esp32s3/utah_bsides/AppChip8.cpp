// CHIP-8 arcade: public-domain CHIP-8 games (chip8/, CC0) on the badge. The 64x32 screen is
// drawn at twice the size in the middle of ours.
//
//   list: joystick move, # (or centre) play, * done
//   game: joystick = CHIP-8 keys 5 / 7 / 8 / 9 (Octo's W A S D), centre or # = 6 (Octo's E),
//         digits = the same CHIP-8 keys, * = A, hold * quit
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeChip8.h"
#include "generated/BadgeChip8Roms.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <Arduino.h>
#include <new>
#include <stdio.h>

namespace badge
{

class Chip8App : public App
{
  public:
    const char *name() const override { return "CHIP-8 arcade"; }
    uint32_t frameMs() const override { return playing ? 33 : 500; }
    bool keepAwake() const override { return playing; }
    void begin() override
    {
        playing = false;
        quitHeld = false;
    }
    void end() override
    {
        delete vm;
        vm = nullptr;
        playing = false;
    }
    void tick(uint32_t now) override
    {
        if (!playing || !vm)
            return;
        // 60 frames a second, catching up a little if the badge was busy
        for (int k = 0; k < 3 && (int32_t)(now - nextFrame) >= 0; k++) {
            vm->frame(CHIP8_GAMES[game].tickrate);
            nextFrame += 1000 / 60;
        }
        if ((int32_t)(now - nextFrame) > 100)
            nextFrame = now;
        if (vm->dirty) {
            vm->dirty = false;
            appRedraw();
        }
    }
    void key(int k, KeyAction a) override
    {
        if (!playing) {
            if (k == AK_STAR && a == KA_RELEASE)
                appClose();
            else if ((a == KA_PRESS || a == KA_REPEAT) && (k == AK_UP || k == AK_LEFT) && sel > 0)
                sel--;
            else if ((a == KA_PRESS || a == KA_REPEAT) && (k == AK_DOWN || k == AK_RIGHT) && sel < CHIP8_GAME_COUNT - 1)
                sel++;
            else if (a == KA_PRESS && (k == AK_HASH || k == AK_5))
                start();
            appRedraw();
            return;
        }
        if (k == AK_STAR && a == KA_HOLD) { // quit to the list
            quitHeld = true;
            playing = false;
            delete vm;
            vm = nullptr;
            appRedraw();
            return;
        }
        if (k == AK_STAR && quitHeld) {
            if (a == KA_RELEASE)
                quitHeld = false;
            return;
        }
        int c = chipKey(k);
        if (c >= 0 && vm && (a == KA_PRESS || a == KA_RELEASE))
            vm->keys[c] = a == KA_PRESS;
    }
    bool keypadLeds(Rgb *out, uint32_t now) override
    {
        (void)now;
        if (!playing || !vm || !vm->beeping())
            return false;
        for (int i = 0; i < KEYPAD_LEDS; i++) // the badge has no speaker: the keys flash for the beep
            out[i][0] = 0.8f, out[i][1] = 0.2f, out[i][2] = 0;
        return true;
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        int lh = fontHeight(FONT_SMALL);
        if (!playing) {
            drawTitle(d, x, y, "CHIP-8 arcade", "#play *done");
            int rows = (d->height() - lh - 2) / lh;
            if (sel < top)
                top = sel;
            if (sel >= top + rows)
                top = sel - rows + 1;
            d->setFont(FONT_SMALL);
            for (int r = 0; r < rows && top + r < CHIP8_GAME_COUNT; r++) {
                int i = top + r, ry = y + lh + 2 + r * lh;
                if (i == sel) {
                    d->fillRect(x, ry, d->width(), lh);
                    d->setColor(BLACK);
                }
                d->drawString(x + 4, ry, CHIP8_GAMES[i].title);
                d->setColor(WHITE);
            }
            return;
        }
        drawTitle(d, x, y, CHIP8_GAMES[game].title, "hold * quit");
        int ox = x + (d->width() - 128) / 2, oy = y + lh + 6;
        d->drawRect(ox - 2, oy - 2, 132, 68);
        if (vm)
            for (int py = 0; py < Chip8::H; py++)
                for (int px = 0; px < Chip8::W; px++)
                    if (vm->pixel(px, py))
                        d->fillRect(ox + px * 2, oy + py * 2, 2, 2);
        drawFooter(d, x, y, vm && vm->crashed ? "The game stopped (hold * to quit)" : "keys 1-9 0 play  hold * quit");
    }

  private:
    Chip8 *vm = nullptr;
    bool playing = false, quitHeld = false;
    int sel = 0, top = 0, game = 0;
    uint32_t nextFrame = 0;

    void start()
    {
        delete vm;
        vm = new (std::nothrow) Chip8();
        if (!vm)
            return;
        game = sel;
        vm->load(CHIP8_GAMES[game].rom, CHIP8_GAMES[game].len, CHIP8_GAMES[game].quirks);
        playing = true;
        nextFrame = millis();
    }

    // badge key -> CHIP-8 key (hex keypad 1 2 3 C / 4 5 6 D / 7 8 9 E / A 0 B F)
    static int chipKey(int k)
    {
        switch (k) {
        case AK_UP: return 0x5;
        case AK_LEFT: return 0x7;
        case AK_DOWN: return 0x8;
        case AK_RIGHT: return 0x9;
        case AK_HASH: return 0x6; // also the joystick centre
        case AK_STAR: return 0xA;
        default: {
            int dg = appKeyDigit(k);
            return dg >= 0 ? dg : -1;
        }
        }
    }
};

static Chip8App chip8App;

void openChip8()
{
    appOpen(&chip8App);
}

} // namespace badge

// Falling blocks.
//   joystick left/right move, up rotate, down drop faster      # drop / start      * quit
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeLeds.h"
#include "BadgeStats.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace badge
{

class TetrisApp : public App
{
    static const int BW = 10, BH = 20, CELL = 5;

  public:
    const char *name() const override { return "Blocks"; }
    uint32_t frameMs() const override { return 1000; }
    void begin() override { state = TB_READY; }
    void key(int k, KeyAction a) override
    {
        if (k == AK_STAR && a == KA_RELEASE) {
            appClose();
            return;
        }
        if (a != KA_PRESS && a != KA_REPEAT)
            return;
        if (state != TB_PLAYING) {
            if (k == AK_HASH && a == KA_PRESS)
                start();
            return;
        }
        switch (k) {
        case AK_LEFT: tryMove(px - 1, py, rot); break;
        case AK_RIGHT: tryMove(px + 1, py, rot); break;
        case AK_UP:
            if (a == KA_PRESS && !tryMove(px, py, (rot + 1) % 4))
                if (!tryMove(px - 1, py, (rot + 1) % 4)) // wall kicks
                    tryMove(px + 1, py, (rot + 1) % 4);
            break;
        case AK_DOWN:
            if (!tryMove(px, py + 1, rot))
                lock();
            break;
        case AK_HASH:
            if (a == KA_PRESS) {
                while (tryMove(px, py + 1, rot)) {
                }
                lock();
            }
            break;
        default: break;
        }
        appRedraw();
    }
    void tick(uint32_t now) override
    {
        if (state != TB_PLAYING || now - lastFall < fallMs())
            return;
        lastFall = now;
        if (!tryMove(px, py + 1, rot))
            lock();
        appRedraw();
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        int top = y + 2, left = x + 4;
        d->drawRect(left - 1, top - 1, BW * CELL + 2, BH * CELL + 2);
        for (int r = 0; r < BH; r++)
            for (int c = 0; c < BW; c++)
                if (board[r][c])
                    d->fillRect(left + c * CELL, top + r * CELL, CELL - 1, CELL - 1);
        if (state == TB_PLAYING)
            for (int i = 0; i < 4; i++) {
                int c, r;
                cell(piece, rot, i, c, r);
                d->fillRect(left + (px + c) * CELL, top + (py + r) * CELL, CELL - 1, CELL - 1);
            }
        // side panel
        int sx = left + BW * CELL + 8;
        char b[24];
        d->setFont(FONT_SMALL);
        d->drawString(sx, top, "BLOCKS");
        snprintf(b, sizeof(b), "Lines %d", lines);
        d->drawString(sx, top + 16, b);
        snprintf(b, sizeof(b), "Best %u", statsHigh(GAME_TETRIS));
        d->drawString(sx, top + 30, b);
        d->drawString(sx, top + 48, "Next:");
        for (int i = 0; i < 4; i++) {
            int c, r;
            cell(next, 0, i, c, r);
            d->fillRect(sx + 4 + c * CELL, top + 64 + r * CELL, CELL - 1, CELL - 1);
        }
        if (state != TB_PLAYING) {
            d->drawString(sx, top + 88, state == TB_READY ? "# start" : "Game over");
            if (state == TB_OVER)
                d->drawString(sx, top + 102, "# again");
        }
    }

  private:
    enum State { TB_READY, TB_PLAYING, TB_OVER } state = TB_READY;
    uint8_t board[BH][BW];
    int piece = 0, next = 0, rot = 0, px = 0, py = 0, lines = 0;
    uint32_t lastFall = 0;

    // 7 pieces x 4 rotations, each 4 cells packed as (col,row) nibbles in a 4x4 box
    static void cell(int p, int r, int i, int &c, int &row)
    {
        static const uint16_t SHAPES[7][4] = {
            {0x0F00, 0x2222, 0x00F0, 0x4444}, // I
            {0x8E00, 0x6440, 0x0E20, 0x44C0}, // J
            {0x2E00, 0x4460, 0x0E80, 0xC440}, // L
            {0x6600, 0x6600, 0x6600, 0x6600}, // O
            {0x6C00, 0x4620, 0x06C0, 0x8C40}, // S
            {0x4E00, 0x4640, 0x0E40, 0x4C40}, // T
            {0xC600, 0x2640, 0x0C60, 0x4C80}, // Z
        };
        uint16_t m = SHAPES[p][r];
        int n = 0;
        for (int b = 0; b < 16; b++) {
            if (m & (0x8000 >> b)) {
                if (n == i) {
                    c = b % 4;
                    row = b / 4;
                    return;
                }
                n++;
            }
        }
        c = row = 0;
    }
    bool fits(int x, int y, int r) const
    {
        for (int i = 0; i < 4; i++) {
            int c, row;
            cell(piece, r, i, c, row);
            int bx = x + c, by = y + row;
            if (bx < 0 || bx >= BW || by >= BH)
                return false;
            if (by >= 0 && board[by][bx])
                return false;
        }
        return true;
    }
    bool tryMove(int x, int y, int r)
    {
        if (!fits(x, y, r))
            return false;
        px = x;
        py = y;
        rot = r;
        return true;
    }
    uint32_t fallMs() const
    {
        int level = lines / 5;
        int ms = 700 - level * 60;
        return ms < 120 ? 120 : ms;
    }
    void spawn()
    {
        piece = next;
        next = random(7);
        rot = 0;
        px = 3;
        py = -1;
        if (!fits(px, py, rot)) {
            state = TB_OVER;
            if (statsReportScore(GAME_TETRIS, (uint16_t)lines))
                ledsFlourish();
        }
    }
    void start()
    {
        memset(board, 0, sizeof(board));
        lines = 0;
        next = random(7);
        state = TB_PLAYING;
        spawn();
        lastFall = millis();
    }
    void lock()
    {
        for (int i = 0; i < 4; i++) {
            int c, r;
            cell(piece, rot, i, c, r);
            if (py + r >= 0)
                board[py + r][px + c] = 1;
        }
        int cleared = 0;
        for (int r = BH - 1; r >= 0; r--) {
            bool full = true;
            for (int c = 0; c < BW; c++)
                full = full && board[r][c];
            if (full) {
                memmove(&board[1][0], &board[0][0], (size_t)r * BW);
                memset(board[0], 0, BW);
                cleared++;
                r++; // check this row again
            }
        }
        if (cleared) {
            lines += cleared;
            ledsMeshPulse();
        }
        spawn();
    }
};

static TetrisApp tetris;

void openTetris()
{
    appOpen(&tetris);
}

} // namespace badge

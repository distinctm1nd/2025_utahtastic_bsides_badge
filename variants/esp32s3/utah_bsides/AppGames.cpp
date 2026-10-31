// Keypad games: Simon and Whack-a-mole use the lit keys 1-9; Snake uses the screen.
// In every game: # starts / restarts, * (or hold *) quits. High scores are kept.
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeLeds.h"
#include "BadgeScenes.h"
#include "BadgeStats.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace badge
{

// keys 1..9 -> keypad LED index, and a colour for each
static int ledForDigit(int digit)
{
    static const int8_t bitOf[10] = {1, 11, 10, 9, 8, 7, 6, 5, 4, 3}; // TCA9535 bit of digits 0..9
    return KEY_BIT_TO_LED[bitOf[digit]];
}
static void digitColour(int digit, Rgb &out)
{
    static const uint32_t C[10] = {0xFFFFFF, 0xFF0000, 0xFF8000, 0xFFFF00, 0x00FF00, 0x00FFFF,
                                   0x0060FF, 0x8000FF, 0xFF00FF, 0xFFFFFF};
    uint32_t c = C[digit % 10];
    out[0] = ((c >> 16) & 255) / 255.0f;
    out[1] = ((c >> 8) & 255) / 255.0f;
    out[2] = (c & 255) / 255.0f;
}
static void clearKeys(Rgb *k, float level = 0.02f)
{
    for (int i = 0; i < KEYPAD_LEDS; i++)
        k[i][0] = k[i][1] = k[i][2] = level;
}
static void bigLine(OLEDDisplay *d, int16_t x, int y, const char *s)
{
    drawCentered(d, x, y, s, FONT_MEDIUM);
}

// =============================================================================================
// Simon: watch the keys light up, then repeat the sequence
// =============================================================================================
class SimonApp : public App
{
    enum State { IDLE, SHOW, INPUT, OVER };

  public:
    const char *name() const override { return "Simon"; }
    uint32_t frameMs() const override { return 100; }
    void begin() override { state = IDLE; }
    void key(int k, KeyAction a) override
    {
        if (k == AK_STAR && a == KA_RELEASE) {
            appClose();
            return;
        }
        if (a != KA_PRESS)
            return;
        if (k == AK_HASH && (state == IDLE || state == OVER)) {
            len = 0;
            addStep();
            return;
        }
        int dg = appKeyDigit(k);
        if (state != INPUT || dg < 1)
            return;
        lit = dg;
        litUntil = millis() + 250;
        if (dg == seq[pos]) {
            if (++pos == len) {
                if (len >= (int)sizeof(seq))
                    gameOver();
                else
                    addStep();
            }
        } else {
            gameOver();
        }
    }
    void tick(uint32_t now) override
    {
        if (state == SHOW && now >= nextAt) {
            int step = showIdx / 2;
            if (step >= len) {
                state = INPUT;
                pos = 0;
                lit = 0;
                appRedraw();
            } else if (showIdx % 2 == 0) {
                lit = seq[step];
                nextAt = now + onMs();
            } else {
                lit = 0;
                nextAt = now + 150;
            }
            showIdx++;
        }
        if (state == INPUT && lit && now > litUntil)
            lit = 0;
    }
    bool keypadLeds(Rgb *k, uint32_t now) override
    {
        clearKeys(k);
        if (state == OVER) {
            float b = (now / 250) % 2 ? 0.6f : 0.1f;
            for (int dg = 1; dg <= 9; dg++)
                k[ledForDigit(dg)][0] = b;
            return true;
        }
        for (int dg = 1; dg <= 9; dg++) { // dim hint of each key's colour
            Rgb c;
            digitColour(dg, c);
            for (int j = 0; j < 3; j++)
                k[ledForDigit(dg)][j] = c[j] * 0.06f;
        }
        if (lit) {
            Rgb c;
            digitColour(lit, c);
            for (int j = 0; j < 3; j++)
                k[ledForDigit(lit)][j] = c[j];
        }
        return true;
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        char r[24];
        snprintf(r, sizeof(r), "best %u", statsHigh(GAME_SIMON));
        drawTitle(d, x, y, "Simon", r);
        char b[32];
        int my = y + 30;
        switch (state) {
        case IDLE:
            bigLine(d, x, my, "Simon says");
            drawFooter(d, x, y, "# start   * quit");
            break;
        case SHOW:
            snprintf(b, sizeof(b), "Level %d", len);
            bigLine(d, x, my, b);
            drawCentered(d, x, my + 30, "Watch the keys...", FONT_SMALL);
            break;
        case INPUT:
            snprintf(b, sizeof(b), "Level %d", len);
            bigLine(d, x, my, b);
            snprintf(b, sizeof(b), "Your turn: %d / %d", pos, len);
            drawCentered(d, x, my + 30, b, FONT_SMALL);
            break;
        case OVER:
            bigLine(d, x, my, "Game over");
            snprintf(b, sizeof(b), "You reached level %d%s", len - 1, newBest ? " - best!" : "");
            drawCentered(d, x, my + 30, b, FONT_SMALL);
            drawFooter(d, x, y, "# again   * quit");
            break;
        }
    }

  private:
    State state = IDLE;
    uint8_t seq[64];
    int len = 0, pos = 0, showIdx = 0, lit = 0;
    uint32_t nextAt = 0, litUntil = 0;
    bool newBest = false;

    uint32_t onMs() const { return len < 5 ? 500 : len < 10 ? 380 : len < 15 ? 300 : 230; }
    void addStep()
    {
        seq[len++] = (uint8_t)(1 + random(9));
        state = SHOW;
        showIdx = 0;
        lit = 0;
        nextAt = millis() + 700;
    }
    void gameOver()
    {
        state = OVER;
        lit = 0;
        newBest = statsReportScore(GAME_SIMON, (uint16_t)(len - 1));
        if (newBest)
            ledsFlourish();
    }
};

// =============================================================================================
// Whack-a-mole: hit the lit key before it goes out. 30 seconds.
// =============================================================================================
class WhackApp : public App
{
  public:
    const char *name() const override { return "Whack-a-mole"; }
    uint32_t frameMs() const override { return 200; }
    void begin() override { running = false; }
    void key(int k, KeyAction a) override
    {
        if (k == AK_STAR && a == KA_RELEASE) {
            appClose();
            return;
        }
        if (a != KA_PRESS)
            return;
        if (k == AK_HASH && !running) {
            running = true;
            score = misses = 0;
            endAt = millis() + 30000;
            newMole(millis());
            return;
        }
        int dg = appKeyDigit(k);
        if (!running || dg < 1)
            return;
        if (dg == mole) {
            score++;
            hitFlash = dg;
            flashGood = true;
            newMole(millis());
        } else {
            misses++;
            hitFlash = dg;
            flashGood = false;
        }
        flashUntil = millis() + 200;
    }
    void tick(uint32_t now) override
    {
        if (!running)
            return;
        if (now >= endAt) {
            running = false;
            mole = 0;
            newBest = statsReportScore(GAME_WHACK, (uint16_t)score);
            if (newBest)
                ledsFlourish();
            appRedraw();
            return;
        }
        if (now >= moleUntil)
            newMole(now);
    }
    bool keypadLeds(Rgb *k, uint32_t now) override
    {
        clearKeys(k, 0.01f);
        if (mole)
            k[ledForDigit(mole)][0] = 0.6f, k[ledForDigit(mole)][1] = 0.3f, k[ledForDigit(mole)][2] = 0.0f;
        if (hitFlash && now < flashUntil) {
            Rgb &c = k[ledForDigit(hitFlash)];
            c[0] = flashGood ? 0.0f : 1.0f;
            c[1] = flashGood ? 1.0f : 0.0f;
            c[2] = 0;
        }
        return true;
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        char r[24], b[32];
        snprintf(r, sizeof(r), "best %u", statsHigh(GAME_WHACK));
        drawTitle(d, x, y, "Whack-a-mole", r);
        if (running) {
            snprintf(b, sizeof(b), "%d", score);
            drawCentered(d, x, y + 24, b, FONT_LARGE);
            int left = (int)((endAt - millis()) / 1000) + 1;
            snprintf(b, sizeof(b), "%d s left   misses %d", left, misses);
            drawCentered(d, x, y + 70, b, FONT_SMALL);
            int barW = (int)((d->width() - 20) * (endAt - millis()) / 30000);
            d->fillRect(x + 10, y + 90, barW, 6);
        } else {
            bigLine(d, x, y + 28, score ? "Time!" : "Whack-a-mole");
            if (score) {
                snprintf(b, sizeof(b), "Score %d%s", score, newBest ? " - best!" : "");
                drawCentered(d, x, y + 60, b, FONT_SMALL);
            } else {
                drawCentered(d, x, y + 60, "Hit the lit key, fast", FONT_SMALL);
            }
            drawFooter(d, x, y, "# start   * quit");
        }
    }

  private:
    bool running = false, newBest = false, flashGood = false;
    int score = 0, misses = 0, mole = 0, hitFlash = 0;
    uint32_t endAt = 0, moleUntil = 0, flashUntil = 0;

    void newMole(uint32_t now)
    {
        int prev = mole;
        do
            mole = 1 + random(9);
        while (mole == prev);
        uint32_t window = 950 - (uint32_t)score * 15;
        if (window < 420)
            window = 420;
        moleUntil = now + window;
    }
};

// =============================================================================================
// Snake on the screen, steered with the joystick
// =============================================================================================
class SnakeApp : public App
{
    static const int CELL = 5;
    static const int MAXLEN = 400;

  public:
    const char *name() const override { return "Snake"; }
    uint32_t frameMs() const override { return 1000; }
    void begin() override
    {
        state = READY;
        reset();
    }
    void key(int k, KeyAction a) override
    {
        if (k == AK_STAR && a == KA_RELEASE) {
            appClose();
            return;
        }
        if (a != KA_PRESS)
            return;
        if (k == AK_HASH) {
            if (state == PLAYING)
                state = PAUSED;
            else if (state == PAUSED)
                state = PLAYING;
            else {
                reset();
                state = PLAYING;
            }
            return;
        }
        // no reversing into yourself
        if (k == AK_UP && dy != 1)
            ndx = 0, ndy = -1;
        else if (k == AK_DOWN && dy != -1)
            ndx = 0, ndy = 1;
        else if (k == AK_LEFT && dx != 1)
            ndx = -1, ndy = 0;
        else if (k == AK_RIGHT && dx != -1)
            ndx = 1, ndy = 0;
    }
    void tick(uint32_t now) override
    {
        if (state != PLAYING || now - lastStep < stepMs())
            return;
        lastStep = now;
        dx = ndx;
        dy = ndy;
        int hx = bx[0] + dx, hy = by[0] + dy;
        if (hx < 0 || hy < 0 || hx >= cols || hy >= rows || hitsBody(hx, hy)) {
            state = OVER;
            newBest = statsReportScore(GAME_SNAKE, (uint16_t)score);
            if (newBest)
                ledsFlourish();
            appRedraw();
            return;
        }
        bool eat = hx == fx && hy == fy;
        if (eat && len < MAXLEN)
            len++;
        for (int i = len - 1; i > 0; i--) {
            bx[i] = bx[i - 1];
            by[i] = by[i - 1];
        }
        bx[0] = (int8_t)hx;
        by[0] = (int8_t)hy;
        if (eat) {
            score++;
            ledsMeshPulse();
            placeFood();
        }
        appRedraw();
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        char r[24];
        snprintf(r, sizeof(r), "%d  best %u", score, statsHigh(GAME_SNAKE));
        drawTitle(d, x, y, "Snake", r);
        int top = y + fontHeight(FONT_SMALL) + 2;
        cols = d->width() / CELL;
        rows = (d->height() - (top - y)) / CELL;
        d->drawRect(x, top, cols * CELL, rows * CELL);
        for (int i = 0; i < len; i++)
            d->fillRect(x + bx[i] * CELL + 1, top + by[i] * CELL + 1, CELL - 1, CELL - 1);
        d->drawCircle(x + fx * CELL + CELL / 2, top + fy * CELL + CELL / 2, 2);
        if (state != PLAYING) {
            const char *msg = state == READY ? "# to start" : state == PAUSED ? "Paused" : "Game over - #";
            d->setColor(BLACK);
            d->fillRect(x + 20, top + 30, d->width() - 40, 20);
            d->setColor(WHITE);
            d->drawRect(x + 20, top + 30, d->width() - 40, 20);
            drawCentered(d, x, top + 33, msg, FONT_SMALL);
        }
    }

  private:
    enum State { READY, PLAYING, PAUSED, OVER } state = READY;
    int8_t bx[MAXLEN], by[MAXLEN];
    int len = 3, dx = 1, dy = 0, ndx = 1, ndy = 0, fx = 0, fy = 0, score = 0;
    int cols = 32, rows = 22;
    uint32_t lastStep = 0;
    bool newBest = false;

    uint32_t stepMs() const { return score < 5 ? 180 : score < 15 ? 140 : score < 30 ? 110 : 85; }
    bool hitsBody(int x, int y) const
    {
        for (int i = 0; i < len - 1; i++)
            if (bx[i] == x && by[i] == y)
                return true;
        return false;
    }
    void reset()
    {
        len = 3;
        score = 0;
        dx = ndx = 1;
        dy = ndy = 0;
        for (int i = 0; i < len; i++) {
            bx[i] = (int8_t)(cols / 2 - i);
            by[i] = (int8_t)(rows / 2);
        }
        placeFood();
    }
    void placeFood()
    {
        do {
            fx = random(cols);
            fy = random(rows);
        } while (hitsBody(fx, fy) || (fx == bx[0] && fy == by[0]));
    }
};

static SimonApp simon;
static WhackApp whack;
static SnakeApp snake;

void openSimon()
{
    appOpen(&simon);
}
void openWhack()
{
    appOpen(&whack);
}
void openSnake()
{
    appOpen(&snake);
}

} // namespace badge

// Mahjong solitaire on the keypad. Tiles are laid in two layers; a tile is "free" when nothing
// sits on top of it and one side is open. Match two free tiles of the same face to clear them.
// The deal is built by removing solvable pairs in reverse, so every board can be cleared.
//   4/2 previous free tile, 6/8 next free tile, 5/# select / match, * deselect (hold * quits),
//   # reshuffles when no moves are left.
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <Arduino.h>
#include <esp_random.h>
#include <string.h>

namespace badge
{

static const int LAYERS = 3, COLS = 10, ROWS = 6, TYPES = 16;
static const int NP = LAYERS * COLS * ROWS;
static const int TW = 14, TH = 15, LO = 3;              // tile width/height and the 3-D layer offset
static const int BX = (160 - (COLS * TW + 2 * LO)) / 2; // centred on the 160x128 screen
static const int BY = 14;

class MahjongApp : public App
{
  public:
    const char *name() const override { return "Mahjong"; }
    uint32_t frameMs() const override { return 500; }
    void begin() override { deal(); }

    void key(int k, KeyAction a) override
    {
        if (k == AK_STAR && a == KA_HOLD) {
            appClose();
            return;
        }
        if (a != KA_PRESS && a != KA_REPEAT)
            return;
        if (showHelp) { // any key closes the help card
            showHelp = false;
            appRedraw();
            return;
        }
        if (k == AK_0) {
            showHelp = true;
            appRedraw();
            return;
        }
        if (won) {
            if (k == AK_HASH || k == AK_5)
                deal();
            else if (k == AK_STAR)
                appClose();
            appRedraw();
            return;
        }
        if (!anyMove() && (k == AK_HASH || k == AK_5)) {
            deal();
            appRedraw();
            return;
        }
        if (k == AK_LEFT || k == AK_4)
            move(0);
        else if (k == AK_RIGHT || k == AK_6)
            move(1);
        else if (k == AK_UP || k == AK_2)
            move(2);
        else if (k == AK_DOWN || k == AK_8)
            move(3);
        else if (k == AK_HASH || k == AK_5)
            select();
        else if (k == AK_STAR)
            sel = -1;
        appRedraw();
    }

    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        d->setColor(WHITE);
        d->setFont(FONT_SMALL);
        if (showHelp) {
            const char *L[] = {"How to play",
                               "Match two FREE tiles that",
                               "show the same picture.",
                               "A tile is free when it has",
                               "no tile on top and an open",
                               "left or right side.",
                               "4 2 prev   6 8 next tile",
                               "5/# pick / match   * drop",
                               "hold * quit        0 close"};
            for (int i = 0; i < 9; i++)
                d->drawString(x + 2, y + 1 + i * 14, L[i]);
            return;
        }
        if (won) {
            drawCentered(d, x, y + 40, "Board cleared!", FONT_MEDIUM);
            drawCentered(d, x, y + 70, "# deal again   * quit", FONT_SMALL);
            return;
        }
        for (int l = 0; l < LAYERS; l++) // lower layer first, upper drawn on top for a 3-D stack
            for (int r = 0; r < ROWS; r++)
                for (int c = 0; c < COLS; c++) {
                    int p = idx(l, c, r);
                    if (!exists[p] || !occ[p])
                        continue;
                    int px, py;
                    tileXY(p, px, py);
                    tile(d, px, py, face[p], p == sel, p == curPos && isFree(p));
                }
        if (!anyMove()) {
            d->setColor(BLACK);
            d->fillRect(x, y + d->height() - 13, d->width(), 13);
            d->setColor(WHITE);
            drawCentered(d, x, y + d->height() - 13, "No moves - # reshuffle", FONT_SMALL);
        } else {
            d->drawString(x + 2, y + d->height() - 13, "0 help"); // in the free space below the board
        }
    }

  private:
    bool exists[LAYERS * COLS * ROWS] = {false};
    bool occ[LAYERS * COLS * ROWS] = {false};
    uint8_t face[LAYERS * COLS * ROWS] = {0};
    int sel = -1, curPos = 0;
    bool won = false;
    bool showHelp = false;

    static int idx(int l, int c, int r) { return (l * ROWS + r) * COLS + c; }

    // the top-left pixel of a tile, and its centre (upper layer shifts up-right for a 3-D look)
    static void tileXY(int p, int &px, int &py)
    {
        int l = p / (COLS * ROWS), r = (p / COLS) % ROWS, c = p % COLS;
        px = BX + c * TW + l * LO;
        py = BY + r * TH - l * LO;
    }
    static void center(int p, int &cx, int &cy)
    {
        tileXY(p, cx, cy);
        cx += TW / 2;
        cy += TH / 2;
    }

    void layout()
    {
        memset(exists, 0, sizeof(exists));
        for (int r = 0; r < ROWS; r++) // layer 0: the full 10x6 floor (60)
            for (int c = 0; c < COLS; c++)
                exists[idx(0, c, r)] = true;
        for (int r = 1; r <= 4; r++) // layer 1: 8x4 cap (32)
            for (int c = 1; c <= 8; c++)
                exists[idx(1, c, r)] = true;
        for (int r = 2; r <= 3; r++) // layer 2: 6x2 peak (12)  -> 104 tiles, 52 pairs
            for (int c = 2; c <= 7; c++)
                exists[idx(2, c, r)] = true;
    }

    bool covered(int l, int c, int r) const // any tile on a higher layer sitting on top
    {
        for (int L = l + 1; L < LAYERS; L++)
            if (exists[idx(L, c, r)] && occ[idx(L, c, r)])
                return true;
        return false;
    }
    bool sideOpen(int l, int c, int r) const
    {
        bool left = c > 0 && exists[idx(l, c - 1, r)] && occ[idx(l, c - 1, r)];
        bool right = c < COLS - 1 && exists[idx(l, c + 1, r)] && occ[idx(l, c + 1, r)];
        return !left || !right;
    }
    bool isFree(int p) const
    {
        if (!occ[p])
            return false;
        int l = p / (COLS * ROWS), r = (p / COLS) % ROWS, c = p % COLS;
        return !covered(l, c, r) && sideOpen(l, c, r);
    }
    int freeCount() const
    {
        int n = 0;
        for (int p = 0; p < LAYERS * COLS * ROWS; p++)
            if (isFree(p))
                n++;
        return n;
    }
    int freeAt(int k) const // the k-th free tile, in position order
    {
        for (int p = 0; p < LAYERS * COLS * ROWS; p++)
            if (isFree(p)) {
                if (k == 0)
                    return p;
                k--;
            }
        return -1;
    }
    bool anyMove() const
    {
        for (int p = 0; p < LAYERS * COLS * ROWS; p++) {
            if (!isFree(p))
                continue;
            for (int q = p + 1; q < LAYERS * COLS * ROWS; q++)
                if (isFree(q) && face[q] == face[p])
                    return true;
        }
        return false;
    }

    // move the cursor to the nearest free tile in a direction (0 left, 1 right, 2 up, 3 down)
    void move(int dir)
    {
        if (curPos < 0 || !isFree(curPos))
            validCursor();
        if (curPos < 0)
            return;
        int cx, cy;
        center(curPos, cx, cy);
        int best = -1;
        long bestScore = 0;
        for (int p = 0; p < NP; p++) {
            if (p == curPos || !isFree(p))
                continue;
            int px, py;
            center(p, px, py);
            int dx = px - cx, dy = py - cy, ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
            bool ok;
            long score;
            if (dir == 1)
                ok = dx > 2, score = dx + (long)ay * 3;
            else if (dir == 0)
                ok = dx < -2, score = -dx + (long)ay * 3;
            else if (dir == 3)
                ok = dy > 2, score = dy + (long)ax * 3;
            else
                ok = dy < -2, score = -dy + (long)ax * 3;
            if (ok && (best < 0 || score < bestScore)) {
                best = p;
                bestScore = score;
            }
        }
        if (best >= 0)
            curPos = best;
    }
    void validCursor() // keep the cursor on a free tile, nearest to where it was
    {
        if (curPos >= 0 && isFree(curPos))
            return;
        int cx = BX, cy = BY;
        if (curPos >= 0)
            center(curPos, cx, cy);
        int best = -1;
        long bestD = 0;
        for (int p = 0; p < NP; p++) {
            if (!isFree(p))
                continue;
            int px, py;
            center(p, px, py);
            long dd = (long)(px - cx) * (px - cx) + (long)(py - cy) * (py - cy);
            if (best < 0 || dd < bestD) {
                best = p;
                bestD = dd;
            }
        }
        curPos = best;
    }

    void deal()
    {
        layout();
        int pairs = 0;
        for (int p = 0; p < NP; p++)
            if (exists[p])
                pairs++;
        pairs /= 2;
        uint8_t pf[LAYERS * COLS * ROWS / 2];
        for (int i = 0; i < pairs; i++)
            pf[i] = i % TYPES; // every type twice (four tiles of each, as in a real set)
        // Build the deal by peeling off free pairs in reverse; replaying that order back is a
        // guaranteed solution. A random peel can strand tiles, so retry until it clears fully
        // (it succeeds ~93% per try, so a few tries always suffice).
        bool ok = false;
        for (int attempt = 0; attempt < 80 && !ok; attempt++) {
            for (int p = 0; p < NP; p++)
                occ[p] = exists[p];
            for (int i = pairs - 1; i > 0; i--) { // shuffle the pair faces
                int j = esp_random() % (i + 1);
                uint8_t t = pf[i];
                pf[i] = pf[j];
                pf[j] = t;
            }
            ok = true;
            for (int k = 0; k < pairs; k++) {
                int fr[LAYERS * COLS * ROWS], m = 0;
                for (int p = 0; p < NP; p++)
                    if (isFree(p))
                        fr[m++] = p;
                if (m < 2) {
                    ok = false;
                    break;
                }
                int a = esp_random() % m, b;
                do {
                    b = esp_random() % m;
                } while (b == a);
                face[fr[a]] = face[fr[b]] = pf[k];
                occ[fr[a]] = occ[fr[b]] = false;
            }
        }
        if (!ok) // astronomically unlikely; just pair whatever is left so no tile is blank
            for (int p = 0, prev = -1; p < NP; p++)
                if (exists[p] && occ[p]) {
                    if (prev < 0)
                        prev = p;
                    else {
                        face[p] = face[prev] = 0;
                        occ[p] = occ[prev] = false;
                        prev = -1;
                    }
                }
        for (int p = 0; p < NP; p++)
            if (exists[p])
                occ[p] = true; // put them all back for play
        sel = -1;
        curPos = -1;
        validCursor();
        won = false;
    }

    void select()
    {
        int p = curPos;
        if (p < 0 || !isFree(p))
            return;
        if (sel < 0) {
            sel = p;
        } else if (sel == p) {
            sel = -1;
        } else if (face[sel] == face[p]) {
            occ[sel] = occ[p] = false; // a match: clear both
            sel = -1;
            int left = 0;
            for (int q = 0; q < NP; q++)
                if (occ[q])
                    left++;
            won = left == 0;
            validCursor(); // keep the cursor on a tile that's still there
        } else {
            sel = p; // different face: move the selection
        }
    }

    // ---- drawing -------------------------------------------------------------------------------
    void tile(OLEDDisplay *d, int px, int py, uint8_t type, bool selected, bool cursor)
    {
        int gx = px + TW / 2, gy = py + TH / 2;
        d->setColor(WHITE);
        d->fillRect(px, py, TW, TH); // the pale tile face
        if (selected) {              // picked: inverted
            d->setColor(BLACK);
            d->fillRect(px + 1, py + 1, TW - 2, TH - 2);
            d->setColor(WHITE);
            glyph(d, gx, gy, type);
            d->setColor(BLACK);
            d->drawRect(px, py, TW, TH);
        } else {
            d->setColor(BLACK);
            d->drawRect(px, py, TW, TH);
            glyph(d, gx, gy, type);
        }
        d->setColor(WHITE);
        if (cursor) { // the moving cursor: a bold frame
            d->drawRect(px - 1, py - 1, TW + 2, TH + 2);
            d->drawRect(px - 2, py - 2, TW + 4, TH + 4);
        }
    }
    // a distinct face per type, drawn in the current colour, centred on (cx, cy)
    void glyph(OLEDDisplay *d, int cx, int cy, uint8_t t)
    {
        if (t <= 2) { // 1-3 dots in a row
            int n = t + 1;
            for (int i = 0; i < n; i++)
                d->fillCircle(cx - (n - 1) * 2 + i * 4, cy, 1);
        } else if (t <= 5) { // 1-3 vertical bars (bamboo)
            int n = t - 2;
            for (int i = 0; i < n; i++)
                d->drawLine(cx - (n - 1) * 2 + i * 4, cy - 5, cx - (n - 1) * 2 + i * 4, cy + 5);
        } else if (t <= 8) { // the numeral 1-3 (characters)
            char b[2] = {(char)('1' + t - 6), 0};
            d->drawString(cx - 2, cy - 6, b);
        } else if (t == 9) { // diamond
            d->drawLine(cx, cy - 5, cx - 5, cy);
            d->drawLine(cx - 5, cy, cx, cy + 5);
            d->drawLine(cx, cy + 5, cx + 5, cy);
            d->drawLine(cx + 5, cy, cx, cy - 5);
        } else if (t == 10) { // cross / asterisk
            d->drawLine(cx - 5, cy, cx + 5, cy);
            d->drawLine(cx, cy - 5, cx, cy + 5);
            d->drawLine(cx - 3, cy - 3, cx + 3, cy + 3);
            d->drawLine(cx - 3, cy + 3, cx + 3, cy - 3);
        } else if (t == 11) { // filled triangle
            d->fillTriangle(cx, cy - 5, cx - 5, cy + 4, cx + 5, cy + 4);
        } else if (t == 12) { // filled square
            d->fillRect(cx - 4, cy - 4, 9, 9);
        } else if (t == 13) { // plus
            d->drawLine(cx - 5, cy, cx + 5, cy);
            d->drawLine(cx - 5, cy - 1, cx + 5, cy - 1);
            d->drawLine(cx, cy - 5, cx, cy + 5);
            d->drawLine(cx - 1, cy - 5, cx - 1, cy + 5);
        } else if (t == 14) { // ring
            d->drawCircle(cx, cy, 5);
            d->drawCircle(cx, cy, 4);
        } else { // t == 15: X
            d->drawLine(cx - 5, cy - 5, cx + 5, cy + 5);
            d->drawLine(cx - 5, cy + 5, cx + 5, cy - 5);
            d->drawLine(cx - 5, cy - 4, cx + 4, cy + 5);
            d->drawLine(cx - 4, cy - 5, cx + 5, cy + 4);
        }
    }
};

static MahjongApp mahjongApp;

void openMahjong()
{
    appOpen(&mahjongApp);
}

} // namespace badge

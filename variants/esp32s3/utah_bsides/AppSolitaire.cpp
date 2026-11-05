// Klondike solitaire on the keypad.
//   4/6 move the cursor across the stock, waste, foundations and the seven columns
//   2/8 on a column pick how deep into its face-up run to grab
//   5/# draw from the stock, pick a card/run up, or drop it; * cancels a pickup, or quits
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

// card 0..51: rank = c%13 (0=A..12=K), suit = c/13 (0 spade, 1 heart, 2 diamond, 3 club)
static int rankOf(uint8_t c) { return c % 13; }
static int suitOf(uint8_t c) { return c / 13; }
static bool isRed(uint8_t c) { return suitOf(c) == 1 || suitOf(c) == 2; }
static const char *rankStr(uint8_t c)
{
    static const char *R[] = {"A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};
    return R[rankOf(c)];
}

class SolitaireApp : public App
{
  public:
    const char *name() const override { return "Solitaire"; }
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
        if (k == AK_0) { // the help card
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
        if (k == AK_LEFT || k == AK_4)
            horiz(-1);
        else if (k == AK_RIGHT || k == AK_6)
            horiz(+1);
        else if (k == AK_UP || k == AK_2)
            vert(-1);
        else if (k == AK_DOWN || k == AK_8)
            vert(+1);
        else if (k == AK_HASH || k == AK_5)
            act();
        else if (k == AK_STAR)
            pick = -1; // cancel a pickup
        appRedraw();
    }

    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        d->setColor(WHITE);
        d->setFont(FONT_SMALL);
        if (showHelp) {
            const char *L[] = {"How to play",
                               "4 6 8 2  move the cursor",
                               "5/# draw, pick up, drop",
                               "* cancel     hold * quit",
                               "Ranks: A 2-9 10 J Q K",
                               "Suits: S H D C =",
                               " spade heart diamond club",
                               "white card = red suit"};
            for (int i = 0; i < 8; i++)
                d->drawString(x + 2, y + 1 + i * 14, L[i]);
            return;
        }
        if (won) {
            drawCentered(d, x, y + 40, "You won!", FONT_MEDIUM);
            drawCentered(d, x, y + 70, "# deal again   * quit", FONT_SMALL);
            return;
        }
        d->drawString(x + 46, y, "0 help"); // the empty middle of the top row
        // top row: stock, waste, then the four foundations on the right
        card(d, x + 1, y, stockN ? 0xFF : 0xFE, false, cur == 0); // 0xFF back, 0xFE empty
        card(d, x + 23, y, wasteN ? waste[wasteN - 1] : 0xFE, true, cur == 1);
        for (int s = 0; s < 4; s++)
            card(d, x + 80 + s * 20, y, fN[s] ? (uint8_t)(fSuit[s] * 13 + fN[s] - 1) : 0xFE, true, cur == 2 + s);
        // seven tableau columns; face-up cards get a full 14px band so the whole label shows.
        // If a column is too long for the screen, squeeze its face-up spacing so it still fits and
        // every card can be seen and grabbed.
        int colW = 22, top = y + 27, availH = d->height() - 27;
        for (int i = 0; i < 7; i++) {
            int cx = x + 1 + i * colW;
            if (!tabN[i]) {
                slot(d, cx, top, cur == 6 + i);
                continue;
            }
            int faceUp = tabN[i] - tabUp[i], fd = tabUp[i], upOff = 14;
            int need = fd * 5 + (faceUp > 1 ? (faceUp - 1) * 14 : 0) + 15;
            if (need > availH && faceUp > 1) { // too tall: shrink the face-up spacing to fit
                upOff = (availH - fd * 5 - 15) / (faceUp - 1);
                if (upOff < 7)
                    upOff = 7;
                if (upOff > 14)
                    upOff = 14;
            }
            int yy = top;
            for (int j = 0; j < tabN[i]; j++) {
                bool up = j >= tabUp[i];
                // cursor: while free, outline the whole run that would be grabbed; while holding a
                // card, mark the column's top card as the drop target so you can see where you are
                bool selHere = cur == 6 + i && (pick < 0 ? j >= grabIdx(i) : j == tabN[i] - 1);
                bool inRun = pick == 6 + i && j >= pickDepth;
                strip(d, cx, yy, tab[i][j], up, selHere, inRun);
                yy += (j == tabN[i] - 1) ? 0 : (up ? upOff : 5);
            }
        }
        // the card(s) you're holding, named along the bottom (kept fully on screen)
        if (pick >= 0) {
            char lab[4], b[20];
            label(heldCard(), lab);
            snprintf(b, sizeof(b), "hold %s", lab);
            int lh = fontHeight(FONT_SMALL), by = y + d->height() - lh;
            d->setColor(BLACK);
            d->fillRect(x, by, 48, lh);
            d->setColor(WHITE);
            d->drawString(x + 1, by, b);
        }
    }

  private:
    uint8_t tab[7][20];
    int tabN[7] = {0}, tabUp[7] = {0};
    uint8_t stock[24], waste[24];
    int stockN = 0, wasteN = 0;
    int fN[4] = {0, 0, 0, 0};          // cards on each foundation pile
    int fSuit[4] = {-1, -1, -1, -1};   // the suit each pile holds (claimed by its first ace), -1 empty
    int cur = 6;           // 0 stock, 1 waste, 2-5 foundations, 6-12 tableau
    int pick = -1;         // the zone a card/run was picked up from
    int pickDepth = 0;     // for a tableau pickup: the grab index
    int grab[7] = {0};     // per-column: how far above the top to grab (0 = top card)
    bool won = false;
    bool showHelp = false;

    void deal()
    {
        uint8_t d[52];
        for (int i = 0; i < 52; i++)
            d[i] = i;
        for (int i = 51; i > 0; i--) { // Fisher-Yates
            int j = esp_random() % (i + 1);
            uint8_t t = d[i];
            d[i] = d[j];
            d[j] = t;
        }
        int p = 0;
        for (int i = 0; i < 7; i++) {
            tabN[i] = i + 1;
            tabUp[i] = i; // only the last card starts face up
            grab[i] = 0;
            for (int j = 0; j <= i; j++)
                tab[i][j] = d[p++];
        }
        stockN = 0;
        while (p < 52)
            stock[stockN++] = d[p++];
        wasteN = 0;
        for (int s = 0; s < 4; s++) {
            fN[s] = 0;
            fSuit[s] = -1;
        }
        cur = 6;
        pick = -1;
        won = false;
    }

    int grabIdx(int i) const { return tabN[i] - 1 - grab[i]; } // which card the cursor points at

    // left/right within the current row (top row 0-5, or the seven columns 6-12)
    void horiz(int dir)
    {
        int c = cur + dir;
        if (cur < 6) {
            if (c >= 0 && c <= 5)
                cur = c;
        } else if (c >= 6 && c <= 12) {
            cur = c;
            grab[cur - 6] = 0; // a fresh column starts on its playable (top) card
        }
    }
    // up/down: cross between the top row and the columns, and step through a column's face-up cards
    void vert(int dir) // dir -1 = up (2), +1 = down (8)
    {
        static const int DOWN_COL[6] = {0, 1, 3, 4, 5, 6}; // a top-row cell -> a column beneath it
        static const int UP_CELL[7] = {0, 1, 2, 3, 4, 5, 5}; // a column -> a top-row cell above it
        if (cur < 6) {
            if (dir > 0) { // down into the tableau, onto the playable card
                cur = 6 + DOWN_COL[cur];
                grab[cur - 6] = 0;
            }
            return;
        }
        int i = cur - 6, faceUp = tabN[i] - tabUp[i];
        if (dir < 0) { // up: dig one card deeper, or step out to the top row
            if (faceUp > 1 && grab[i] < faceUp - 1)
                grab[i]++;
            else
                cur = UP_CELL[i];
        } else { // down: back toward the playable card
            if (grab[i] > 0)
                grab[i]--;
        }
    }

    uint8_t heldCard() const // the first (lowest-rank-on-top... actually the moving) card
    {
        if (pick == 1)
            return waste[wasteN - 1];
        if (pick >= 2 && pick <= 5)
            return (uint8_t)(fSuit[pick - 2] * 13 + fN[pick - 2] - 1);
        return tab[pick - 6][pickDepth];
    }

    void act()
    {
        if (cur == 0) { // the stock: draw one / recycle the waste, or (while holding) cancel
            if (pick >= 0) {
                pick = -1;
                return;
            }
            if (stockN) {
                waste[wasteN++] = stock[--stockN];
            } else {
                while (wasteN)
                    stock[stockN++] = waste[--wasteN];
            }
            return;
        }
        if (pick < 0) { // nothing held: pick a card up
            tryPick();
            return;
        }
        if (cur == pick) { // pressing the card again puts it back down
            pick = -1;
            return;
        }
        if (drop()) // a legal move
            return;
        pick = -1; // not a legal move here: drop the old pick and try to grab this card instead
        tryPick();
    }

    void tryPick()
    {
        if (cur == 1 && wasteN) {
            pick = 1;
        } else if (cur >= 2 && cur <= 5 && fN[cur - 2]) {
            pick = cur;
        } else if (cur >= 6) {
            int i = cur - 6;
            if (tabN[i] && grabIdx(i) >= tabUp[i]) {
                pick = cur;
                pickDepth = grabIdx(i);
            }
        }
    }

    bool drop()
    {
        uint8_t first = heldCard();
        if (cur >= 2 && cur <= 5) { // onto a foundation (any empty pile takes an ace; then by suit)
            int p = cur - 2;
            bool single = !(pick >= 6 && pickDepth < tabN[pick - 6] - 1);
            bool ok = single && (fN[p] == 0 ? rankOf(first) == 0 : suitOf(first) == fSuit[p] && rankOf(first) == fN[p]);
            if (!ok)
                return false;
            if (fN[p] == 0)
                fSuit[p] = suitOf(first);
            fN[p]++;
            removeHeld();
            return true;
        }
        if (cur >= 6) { // onto a tableau column
            int i = cur - 6;
            bool ok;
            if (!tabN[i])
                ok = rankOf(first) == 12; // only a King on an empty column
            else {
                uint8_t topc = tab[i][tabN[i] - 1];
                ok = (isRed(topc) != isRed(first)) && rankOf(topc) == rankOf(first) + 1;
            }
            if (!ok)
                return false;
            if (pick >= 6) { // move a run of cards
                int src = pick - 6;
                for (int j = pickDepth; j < tabN[src]; j++)
                    tab[i][tabN[i]++] = tab[src][j];
            } else {
                tab[i][tabN[i]++] = first;
            }
            grab[i] = 0;
            removeHeld();
            return true;
        }
        return false;
    }

    void removeHeld()
    {
        if (pick == 1) {
            wasteN--;
        } else if (pick >= 2 && pick <= 5) {
            if (--fN[pick - 2] == 0)
                fSuit[pick - 2] = -1; // emptied: the pile can take any ace again
        } else {
            int src = pick - 6;
            tabN[src] = pickDepth;
            if (tabN[src] && tabUp[src] >= tabN[src])
                tabUp[src] = tabN[src] - 1; // flip the newly exposed card
            grab[src] = 0;
        }
        pick = -1;
        won = (fN[0] + fN[1] + fN[2] + fN[3]) == 52;
    }

    // ---- drawing -------------------------------------------------------------------------------
    // a card's text label: rank + suit letter, e.g. "7H", "10D", "AS", "KC". out must hold 4 chars.
    static void label(uint8_t c, char *out)
    {
        const char *r = rankStr(c);
        int i = 0;
        for (; r[i]; i++)
            out[i] = r[i];
        out[i] = "SHDC"[suitOf(c)];
        out[i + 1] = 0;
    }
    void card(OLEDDisplay *d, int cx, int cy, uint8_t c, bool up, bool sel)
    {
        d->setColor(WHITE);
        if (c == 0xFE) { // empty slot
            slot(d, cx, cy, sel);
            return;
        }
        if (c != 0xFF && up && isRed(c)) // a red card: white body, so colour reads at a glance
            d->fillRect(cx, cy, 20, 24);
        d->setColor(c != 0xFF && up && isRed(c) ? BLACK : WHITE);
        d->drawRect(cx, cy, 20, 24);
        if (c == 0xFF || !up) { // a face-down back: hatched
            d->setColor(WHITE);
            for (int i = 2; i < 23; i += 3)
                d->drawLine(cx + 1, cy + i, cx + 19, cy + i - 1);
        } else {
            char b[4];
            label(c, b);
            d->setFont(FONT_SMALL);
            int w = d->getStringWidth(b);
            d->drawString(cx + (20 - w) / 2, cy + 6, b); // centred; colour already set per suit
        }
        if (sel)
            frame(d, cx, cy, 20, 24);
    }
    void slot(OLEDDisplay *d, int cx, int cy, bool sel)
    {
        d->setColor(WHITE);
        for (int i = 0; i < 24; i += 3) {
            d->setPixel(cx, cy + i);
            d->setPixel(cx + 19, cy + i);
        }
        d->drawLine(cx, cy, cx + 19, cy);
        d->drawLine(cx, cy + 23, cx + 19, cy + 23);
        if (sel)
            frame(d, cx, cy, 20, 24);
    }
    // one stacked card in a column: a black body (so the card below is hidden) with its label on top
    void strip(OLEDDisplay *d, int cx, int cy, uint8_t c, bool up, bool sel, bool inRun)
    {
        int h = up ? 15 : 7;
        bool red = up && isRed(c);
        d->setColor(red ? WHITE : BLACK); // red card = white body, black card = black body
        d->fillRect(cx, cy, 21, h);
        d->setColor(red ? BLACK : WHITE);
        d->drawRect(cx, cy, 21, h);
        if (!up) {
            d->setColor(WHITE);
            d->drawLine(cx + 2, cy + 3, cx + 18, cy + 3); // a face-down sliver
        } else {
            char b[4];
            label(c, b);
            d->setFont(FONT_SMALL);
            int w = d->getStringWidth(b);
            d->drawString(cx + (21 - w) / 2, cy, b); // centred; black on a red card, white on a black one
        }
        d->setColor(WHITE);
        if (inRun)
            d->drawRect(cx - 1, cy - 1, 23, h + 2);
        if (sel)
            frame(d, cx, cy, 21, h);
    }
    void frame(OLEDDisplay *d, int cx, int cy, int w, int h)
    {
        d->setColor(WHITE);
        d->drawRect(cx - 1, cy - 1, w + 2, h + 2);
        d->drawRect(cx - 2, cy - 2, w + 4, h + 4);
    }
};

static SolitaireApp solitaireApp;

void openSolitaire()
{
    appOpen(&solitaireApp);
}

} // namespace badge

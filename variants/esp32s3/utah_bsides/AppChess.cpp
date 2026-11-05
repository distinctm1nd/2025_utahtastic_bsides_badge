// Chess: against the badge (Easy / Normal / Hard) or two players passing the badge.
//
//   joystick   move the cursor        centre / #   pick up a piece, put it down
//   *          put the piece back     hold *       quit
// Pawns reaching the end become queens. White pieces are drawn hollow, Black solid. Your
// pieces are always at the bottom (two players: the board turns for each move).
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeChess.h"
#include "BadgeLeds.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <Arduino.h>
#include <initializer_list>
#include <new>
#include <stdio.h>

namespace badge
{

// 12x12 pieces, '#' = filled
static const char *const PIECE_ART[7][12] = {
    {},
    {"............", ".....##.....", "....####....", "....####....", ".....##.....", "....####....",
     ".....##.....", ".....##.....", "....####....", "...######...", "..########..", "............"},
    {"............", "....##......", "...#####....", "..#######...", ".####.###...", ".###..###...",
     "......###...", ".....####...", "....#####...", "...######...", "..########..", "............"},
    {"............", ".....##.....", "....####....", "...##.###...", "...#.####...", "...######...",
     "....####....", ".....##.....", "....####....", "...######...", "..########..", "............"},
    {"............", "..##.##.##..", "..########..", "..########..", "...######...", "...######...",
     "...######...", "...######...", "...######...", "..########..", "..########..", "............"},
    {"............", ".#...##...#.", ".##.####.##.", "..########..", "..########..", "...######...",
     "...######...", "....####....", "...######...", "..########..", "..########..", "............"},
    {".....##.....", "....####....", ".....##.....", "..##.##.##..", ".##########.", ".##########.",
     "..########..", "...######...", "...######...", "..########..", "..########..", "............"},
};

class ChessApp : public App
{
    static const int SQ = 16;

  public:
    const char *name() const override { return "Chess"; }
    uint32_t frameMs() const override { return 500; }
    bool keepAwake() const override { return true; }
    void begin() override
    {
        state = CH_MENU;
        menuRow = 0;
        quitHeld = false;
        if (!h)
            h = new (std::nothrow) Engine();
    }
    void end() override
    {
        delete h;
        h = nullptr;
    }
    void key(int k, KeyAction a) override
    {
        if (k == AK_STAR) {
            if (a == KA_HOLD) {
                quitHeld = true;
                appClose();
            } else if (a == KA_RELEASE && !quitHeld) {
                if (state == CH_MENU)
                    appClose();
                else
                    selected = -1;
            }
            return;
        }
        if (!h || (a != KA_PRESS && a != KA_REPEAT))
            return;
        if (state == CH_MENU) {
            menuKey(k, a);
            return;
        }
        if (state == CH_OVER) {
            if (k == AK_HASH && a == KA_PRESS)
                state = CH_MENU;
            return;
        }
        if (state != CH_PLAYER)
            return;
        // the cursor moves the way you push on screen (the board is flipped when you're Black)
        int fl = flipped() ? -1 : 1;
        int f = Chess::fileOf(cursor), r = Chess::rankOf(cursor);
        if (k == AK_UP || k == AK_2)
            r += fl;
        else if (k == AK_DOWN || k == AK_8)
            r -= fl;
        else if (k == AK_LEFT || k == AK_4)
            f -= fl;
        else if (k == AK_RIGHT || k == AK_6)
            f += fl;
        else if ((k == AK_HASH || k == AK_5) && a == KA_PRESS)
            choose();
        if (f >= 0 && f < 8 && r >= 0 && r < 8)
            cursor = Chess::sqAt(f, r);
    }
    void tick(uint32_t now) override
    {
        if (h && state == CH_CPU && now >= thinkAt) {
            ChessMove m;
            h->game.noise = level == 0 ? 15 : level == 1 ? 6 : 0;
            static const long BUDGET[3] = {2500, 20000, 80000};
            if (h->game.think(m, BUDGET[level], level == 0 ? 2 : 6))
                play(m);
            appRedraw();
        }
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        if (!h) {
            drawTitle(d, x, y, "Chess", "*back");
            drawCentered(d, x, y + 2 * fontHeight(FONT_SMALL), "Not enough memory", FONT_SMALL);
            return;
        }
        if (state == CH_MENU) {
            drawMenu(d, x, y);
            return;
        }
        drawBoard(d, x, y);
        drawPanel(d, x + 8 * SQ + 1, y);
        if (state == CH_OVER) {
            int bw = 8 * SQ - 16, lh = fontHeight(FONT_SMALL);
            d->setColor(BLACK);
            d->fillRect(x + 8, y + 40, bw, 2 * lh + 6);
            d->setColor(WHITE);
            d->drawRect(x + 8, y + 40, bw, 2 * lh + 6);
            drawCenteredIn(d, x + 8, bw, y + 42, result);
            drawCenteredIn(d, x + 8, bw, y + 42 + lh, "# new game");
        }
    }

  private:
    enum State { CH_MENU, CH_PLAYER, CH_CPU, CH_OVER } state = CH_MENU;
    // the engine's search buffers are ~13 KB: only held while Chess is open
    struct Engine {
        Chess game;
        ChessMove moves[Chess::MAXMOVES];
    };
    Engine *h = nullptr;
    int mode = 0;  // 0 you White, 1 you Black, 2 two players
    int level = 1; // 0 Easy, 1 Normal, 2 Hard
    int menuRow = 0;
    int cursor = 0x14, selected = -1;
    int lastFrom = -1, lastTo = -1;
    bool quitHeld = false;
    uint32_t thinkAt = 0;
    const char *result = "";
    int nmoves = 0;

    // your pieces are always at the bottom: Black at the bottom when you play Black, and in a
    // two-player game the board turns to face whoever's move it is
    bool flipped() const { return mode == 1 || (mode == 2 && h->game.side < 0); }
    bool cpuSide(int side) const { return mode == 0 ? side < 0 : mode == 1 ? side > 0 : false; }

    void menuKey(int k, KeyAction a)
    {
        if (k == AK_UP || k == AK_2)
            menuRow = (menuRow + 3) % 4;
        else if (k == AK_DOWN || k == AK_8)
            menuRow = (menuRow + 1) % 4;
        else if ((k == AK_LEFT || k == AK_4 || k == AK_RIGHT || k == AK_6 || k == AK_HASH || k == AK_5) && menuRow == 3)
            level = (level + ((k == AK_LEFT || k == AK_4) ? 2 : 1)) % 3;
        else if ((k == AK_HASH || k == AK_5) && a == KA_PRESS) {
            mode = menuRow;
            newGame();
        }
    }
    void newGame()
    {
        h->game.reset();
        selected = -1;
        lastFrom = lastTo = -1;
        cursor = flipped() ? 0x64 : 0x14; // e7 or e2
        afterMove();
    }
    void choose()
    {
        int pc = h->game.board[cursor];
        if (selected >= 0) {
            for (int i = 0; i < nmoves; i++)
                if (h->moves[i].from == selected && h->moves[i].to == cursor && (!h->moves[i].promo || h->moves[i].promo == Chess::QUEEN)) {
                    play(h->moves[i]);
                    return;
                }
        }
        // pick up (or switch to) one of your pieces that can move
        if (pc * h->game.side > 0)
            for (int i = 0; i < nmoves; i++)
                if (h->moves[i].from == cursor) {
                    selected = cursor;
                    return;
                }
        selected = -1;
    }
    void play(const ChessMove &m)
    {
        Chess::Undo u;
        bool capture = h->game.board[m.to] != 0 || (m.flags & Chess::CM_EP);
        h->game.make(m, u);
        lastFrom = m.from;
        lastTo = m.to;
        selected = -1;
        if (capture)
            ledsMeshPulse();
        afterMove();
    }
    void afterMove()
    {
        nmoves = h->game.legal(h->moves);
        Chess::Status st = h->game.status();
        if (st != Chess::PLAYING) {
            state = CH_OVER;
            if (st == Chess::CHECKMATE) {
                bool loserCpu = cpuSide(h->game.side);
                result = mode == 2 ? (h->game.side > 0 ? "Black wins!" : "White wins!") : loserCpu ? "Checkmate! You win" : "Checkmate. Badge wins";
                if (mode != 2 && loserCpu)
                    ledsFlourish();
            } else {
                result = st == Chess::STALEMATE ? "Stalemate: draw" : st == Chess::DRAW_50 ? "50 moves: draw" : "Draw";
            }
            return;
        }
        if (cpuSide(h->game.side)) {
            state = CH_CPU;
            thinkAt = millis() + 150; // let "Thinking" reach the screen first
        } else {
            state = CH_PLAYER;
            if (mode == 2) // pass the badge: the cursor starts on the next player's king's file
                cursor = h->game.side > 0 ? 0x14 : 0x64;
        }
    }

    // ---- drawing ------------------------------------------------------------------------
    void drawMenu(OLEDDisplay *d, int16_t x, int16_t y)
    {
        static const char *LEVELS[3] = {"Easy", "Normal", "Hard"};
        drawTitle(d, x, y, "Chess", "*back");
        int lh = fontHeight(FONT_SMALL) + 2;
        char lv[24];
        snprintf(lv, sizeof(lv), "Level: < %s >", LEVELS[level]);
        const char *rows[4] = {"Play White", "Play Black", "Two players", lv};
        int top = y + lh + 6;
        for (int i = 0; i < 4; i++) {
            int ry = top + i * lh;
            if (i == menuRow) {
                d->fillRect(x + 6, ry, d->width() - 12, lh);
                d->setColor(BLACK);
            }
            d->drawString(x + 12, ry, rows[i]);
            d->setColor(WHITE);
        }
        drawFooter(d, x, y, "centre: start");
    }
    void squareXY(int s, int16_t x, int16_t y, int &sx, int &sy) const
    {
        int f = Chess::fileOf(s), r = Chess::rankOf(s);
        if (flipped())
            f = 7 - f, r = 7 - r;
        sx = x + f * SQ;
        sy = y + (7 - r) * SQ;
    }
    void drawBoard(OLEDDisplay *d, int16_t x, int16_t y)
    {
        for (int r = 0; r < 8; r++)
            for (int f = 0; f < 8; f++) {
                int s = Chess::sqAt(f, r), sx, sy;
                squareXY(s, x, y, sx, sy);
                if (((f + r) & 1) == 0) // dark square: a light dot pattern
                    for (int py = 1; py < SQ; py += 2)
                        for (int px = (py & 2) ? 0 : 1; px < SQ; px += 2)
                            if (((px + py) & 3) == 1)
                                d->setPixel(sx + px, sy + py);
                int pc = h->game.board[s];
                if (pc)
                    drawPiece(d, sx + 2, sy + 2, pc < 0 ? -pc : pc, pc < 0);
            }
        d->drawRect(x, y, 8 * SQ, 8 * SQ);
        // last move: corner ticks
        for (int s : {lastFrom, lastTo}) {
            if (s < 0)
                continue;
            int sx, sy;
            squareXY(s, x, y, sx, sy);
            d->fillRect(sx + 1, sy + 1, 2, 2);
            d->fillRect(sx + SQ - 3, sy + SQ - 3, 2, 2);
        }
        // where the picked-up piece can go
        if (selected >= 0)
            for (int i = 0; i < nmoves; i++)
                if (h->moves[i].from == selected && (!h->moves[i].promo || h->moves[i].promo == Chess::QUEEN)) {
                    int sx, sy;
                    squareXY(h->moves[i].to, x, y, sx, sy);
                    d->setColor(BLACK);
                    d->fillRect(sx + 6, sy + 6, 4, 4);
                    d->setColor(WHITE);
                    d->fillRect(sx + 7, sy + 7, 2, 2);
                }
        if (state == CH_PLAYER) {
            int sx, sy;
            if (selected >= 0) {
                squareXY(selected, x, y, sx, sy);
                d->drawRect(sx, sy, SQ, SQ);
                d->drawRect(sx + 1, sy + 1, SQ - 2, SQ - 2);
            }
            squareXY(cursor, x, y, sx, sy);
            bool blink = (millis() / 400) & 1;
            d->setColor(INVERSE);
            d->drawRect(sx, sy, SQ, SQ);
            if (blink)
                d->drawRect(sx + 1, sy + 1, SQ - 2, SQ - 2);
            d->setColor(WHITE);
        }
    }
    static void drawPiece(OLEDDisplay *d, int px, int py, int t, bool solid)
    {
        const char *const *art = PIECE_ART[t];
        auto on = [&](int cx, int cy) { return cx >= 0 && cy >= 0 && cx < 12 && cy < 12 && art[cy][cx] == '#'; };
        // a black halo so the piece reads on the dotted squares
        d->setColor(BLACK);
        for (int cy = 0; cy < 12; cy++)
            for (int cx = 0; cx < 12; cx++)
                if (on(cx, cy) || on(cx - 1, cy) || on(cx + 1, cy) || on(cx, cy - 1) || on(cx, cy + 1))
                    d->setPixel(px + cx, py + cy);
        d->setColor(WHITE);
        for (int cy = 0; cy < 12; cy++)
            for (int cx = 0; cx < 12; cx++) {
                if (!on(cx, cy))
                    continue;
                bool edge = !on(cx - 1, cy) || !on(cx + 1, cy) || !on(cx, cy - 1) || !on(cx, cy + 1);
                if (solid || edge)
                    d->setPixel(px + cx, py + cy);
            }
    }
    void drawPanel(OLEDDisplay *d, int16_t x, int16_t y)
    {
        int lh = fontHeight(FONT_SMALL);
        // whose turn: a king of that colour
        drawPiece(d, x + 9, y + 2, Chess::KING, h->game.side < 0);
        const char *who = state == CH_CPU ? "..." : mode == 2 ? (h->game.side > 0 ? "W" : "B") : "You";
        drawCenteredIn(d, x, d->width() - x, y + 16, who);
        if (state != CH_OVER && h->game.inCheck(h->game.side))
            drawCenteredIn(d, x, d->width() - x, y + 16 + lh, "chk!");
        if (lastFrom >= 0) {
            char mv[6] = {(char)('a' + Chess::fileOf(lastFrom)), (char)('1' + Chess::rankOf(lastFrom)),
                          (char)('a' + Chess::fileOf(lastTo)), (char)('1' + Chess::rankOf(lastTo)), 0};
            drawCenteredIn(d, x, d->width() - x, y + 16 + 2 * lh + 4, mv);
        }
        static const char *LV[3] = {"easy", "norm", "hard"};
        if (mode != 2)
            drawCenteredIn(d, x, d->width() - x, y + d->height() - 2 * lh - 2, LV[level]);
        drawCenteredIn(d, x, d->width() - x, y + d->height() - 2 * lh, "2/8/4/6");
        drawCenteredIn(d, x, d->width() - x, y + d->height() - lh, "# pick");
    }
    static void drawCenteredIn(OLEDDisplay *d, int x, int w, int y, const char *s)
    {
        d->setFont(FONT_SMALL);
        d->setTextAlignment(TEXT_ALIGN_CENTER);
        d->drawString(x + w / 2, y, s);
        d->setTextAlignment(TEXT_ALIGN_LEFT);
    }
};

static ChessApp chessApp;

void openChess()
{
    appOpen(&chessApp);
}

} // namespace badge

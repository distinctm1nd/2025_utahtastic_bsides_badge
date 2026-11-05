// Tic-tac-toe on keys 1-9 (they are laid out like the board), against the badge or against
// another badge over the mesh. The keys light up: red X, blue O.
//
// Mesh protocol on the private app port, as direct messages:
//   "BTT" + type + game id (2 bytes) + cell      types: I invite, A accept, D decline,
//                                                       M move (cell 0..8), Q quit
// Whoever invites plays X and moves first.
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeLeds.h"
#include "BadgeMesh.h"
#include "BadgeScenes.h"
#include "BadgeStats.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

extern graphics::Screen *screen;

namespace badge
{

static int winner(const int8_t b[9]) // 1 = X, 2 = O, 3 = draw, 0 = still playing
{
    static const int8_t L[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};
    for (auto &l : L)
        if (b[l[0]] && b[l[0]] == b[l[1]] && b[l[1]] == b[l[2]])
            return b[l[0]];
    for (int i = 0; i < 9; i++)
        if (!b[i])
            return 0;
    return 3;
}

// perfect play, with a little randomness so it isn't the same game every time
static int minimax(int8_t b[9], int me, int turn, int depth)
{
    int w = winner(b);
    if (w == me)
        return 10 - depth;
    if (w == 3)
        return 0;
    if (w)
        return depth - 10;
    int best = turn == me ? -100 : 100;
    for (int i = 0; i < 9; i++) {
        if (b[i])
            continue;
        b[i] = (int8_t)turn;
        int s = minimax(b, me, 3 - turn, depth + 1);
        b[i] = 0;
        best = turn == me ? (s > best ? s : best) : (s < best ? s : best);
    }
    return best;
}

class TttApp : public App
{
  public:
    enum Mode { TT_CPU, TT_MESH };
    enum State { TT_WAITING, TT_PLAYING, TT_DONE };

    void setupCpu()
    {
        mode = TT_CPU;
        reset();
        state = TT_PLAYING;
        myMark = 1;
    }
    void setupMesh(uint32_t opp, bool weInvited, uint16_t gid)
    {
        mode = TT_MESH;
        reset();
        opponent = opp;
        gameId = gid;
        myMark = weInvited ? 1 : 2;
        state = weInvited ? TT_WAITING : TT_PLAYING;
        waitSince = millis();
        meshNodeName(opp, oppName, sizeof(oppName));
    }

    const char *name() const override { return "Tic-tac-toe"; }
    uint32_t frameMs() const override { return 500; }

    void end() override
    {
        if (mode == TT_MESH && state != TT_DONE && opponent)
            send('Q', 0);
    }

    void key(int k, KeyAction a) override
    {
        if (k == AK_STAR && a == KA_RELEASE) {
            appClose();
            return;
        }
        if (a != KA_PRESS)
            return;
        if (k == AK_HASH && state == TT_DONE && mode == TT_CPU) {
            setupCpu();
            return;
        }
        int dg = appKeyDigit(k);
        if (dg < 1 || state != TT_PLAYING || turn != myMark)
            return;
        int c = dg - 1;
        if (board[c])
            return;
        play(c, myMark);
        if (mode == TT_MESH)
            send('M', (uint8_t)c);
    }

    void tick(uint32_t now) override
    {
        if (mode == TT_CPU && state == TT_PLAYING && turn != myMark && now - movedAt > 500) {
            int8_t b[9];
            memcpy(b, board, 9);
            int best = -100, pick = -1;
            for (int i = 0; i < 9; i++) {
                if (b[i])
                    continue;
                b[i] = (int8_t)turn;
                int s = minimax(b, turn, 3 - turn, 1) * 4 + (int)random(3);
                b[i] = 0;
                if (s > best) {
                    best = s;
                    pick = i;
                }
            }
            if (pick >= 0)
                play(pick, turn);
        }
        if (mode == TT_MESH && state == TT_WAITING && now - waitSince > 120000) {
            state = TT_DONE;
            snprintf(result, sizeof(result), "No answer from %s", oppName);
            appRedraw();
        }
    }

    // a packet for this game arrived
    void onPacket(uint32_t from, char type, uint16_t gid, uint8_t cell)
    {
        if (mode != TT_MESH || from != opponent || gid != gameId)
            return;
        if (type == 'A' && state == TT_WAITING) {
            state = TT_PLAYING;
        } else if (type == 'D' && state == TT_WAITING) {
            state = TT_DONE;
            snprintf(result, sizeof(result), "%s said no", oppName);
        } else if (type == 'Q' && state != TT_DONE) {
            state = TT_DONE;
            snprintf(result, sizeof(result), "%s left the game", oppName);
        } else if (type == 'M' && state == TT_PLAYING && turn != myMark && cell < 9 && !board[cell]) {
            play(cell, 3 - myMark);
        }
        appRedraw();
    }

    bool keypadLeds(Rgb *k, uint32_t now) override
    {
        static const int8_t ledOfCell[9] = {2, 1, 0, 3, 4, 5, 8, 7, 6}; // keys 1..9 -> keypad LED
        for (int i = 0; i < KEYPAD_LEDS; i++)
            k[i][0] = k[i][1] = k[i][2] = 0.01f;
        bool myTurn = state == TT_PLAYING && turn == myMark;
        for (int c = 0; c < 9; c++) {
            Rgb &o = k[ledOfCell[c]];
            bool winCell = state == TT_DONE && isWinCell(c) && (now / 300) % 2;
            if (board[c] == 1)
                o[0] = winCell ? 0.2f : 1.0f, o[1] = 0.0f, o[2] = 0.0f;
            else if (board[c] == 2)
                o[0] = 0.0f, o[1] = 0.1f, o[2] = winCell ? 0.2f : 1.0f;
            else if (myTurn)
                o[0] = o[1] = o[2] = 0.08f;
        }
        return true;
    }

    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        char title[48];
        if (mode == TT_CPU)
            snprintf(title, sizeof(title), "You (X) vs badge");
        else
            snprintf(title, sizeof(title), "vs %s", oppName);
        drawTitle(d, x, y, title, myMark == 1 ? "X" : "O");
        int top = y + fontHeight(FONT_SMALL) + 4;
        int size = d->height() - top - 2;
        if (size > 90)
            size = 90;
        int cs = size / 3;
        int gx = x + 6;
        for (int i = 1; i < 3; i++) {
            d->drawVerticalLine(gx + i * cs, top, cs * 3);
            d->drawHorizontalLine(gx, top + i * cs, cs * 3);
        }
        for (int c = 0; c < 9; c++) {
            int cx = gx + (c % 3) * cs + cs / 2, cy = top + (c / 3) * cs + cs / 2, r = cs / 2 - 5;
            if (board[c] == 1) {
                d->drawLine(cx - r, cy - r, cx + r, cy + r);
                d->drawLine(cx - r, cy + r, cx + r, cy - r);
            } else if (board[c] == 2) {
                d->drawCircle(cx, cy, r);
            } else {
                char n[2] = {(char)('1' + c), 0};
                d->setTextAlignment(TEXT_ALIGN_CENTER);
                d->drawString(cx, cy - fontHeight(FONT_SMALL) / 2, n);
                d->setTextAlignment(TEXT_ALIGN_LEFT);
            }
        }
        int sx = gx + cs * 3 + 8;
        const char *msg;
        if (state == TT_WAITING)
            msg = "Waiting...";
        else if (state == TT_DONE)
            msg = result;
        else
            msg = turn == myMark ? "Your turn" : (mode == TT_CPU ? "Thinking" : "Their turn");
        drawLines(d, sx, top + 4, d->width() - sx - 2, msg);
        int lh = fontHeight(FONT_SMALL);
        if (state == TT_PLAYING && turn == myMark)
            d->drawString(sx, y + d->height() - 2 * lh - 2, "keys 1-9");
        d->drawString(sx, y + d->height() - lh, state == TT_DONE && mode == TT_CPU ? "#again *quit" : "*quit");
    }

  private:
    Mode mode = TT_CPU;
    State state = TT_PLAYING;
    int8_t board[9];
    int turn = 1, myMark = 1;
    uint32_t opponent = 0, movedAt = 0, waitSince = 0;
    uint16_t gameId = 0;
    char oppName[40] = "";
    char result[48] = "";

    void reset()
    {
        memset(board, 0, sizeof(board));
        turn = 1;
        result[0] = 0;
        movedAt = millis();
    }
    void send(char type, uint8_t cell)
    {
        uint8_t p[7] = {'B', 'T', 'T', (uint8_t)type, (uint8_t)(gameId & 0xFF), (uint8_t)(gameId >> 8), cell};
        meshSendPrivate(opponent, p, sizeof(p), true);
    }
    void play(int c, int mark)
    {
        board[c] = (int8_t)mark;
        turn = 3 - mark;
        movedAt = millis();
        int w = winner(board);
        if (!w)
            return;
        state = TT_DONE;
        if (w == 3) {
            snprintf(result, sizeof(result), "Draw");
        } else if (w == myMark) {
            snprintf(result, sizeof(result), "You win!");
            ledsFlourish();
            if (mode == TT_MESH) {
                statsReportScore(GAME_TTT_WINS, (uint16_t)(statsHigh(GAME_TTT_WINS) + 1));
                statsAchieve(ACH_TTT);
            }
        } else {
            snprintf(result, sizeof(result), mode == TT_CPU ? "Badge wins" : "They win");
        }
    }
    bool isWinCell(int c) const
    {
        static const int8_t L[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};
        for (auto &l : L)
            if (board[l[0]] && board[l[0]] == board[l[1]] && board[l[1]] == board[l[2]])
                if (l[0] == c || l[1] == c || l[2] == c)
                    return true;
        return false;
    }
    static void drawLines(OLEDDisplay *d, int x, int y, int w, const char *msg)
    {
        // break at spaces to fit the side column
        char buf[64];
        strncpy(buf, msg, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        char *s = buf;
        int lh = fontHeight(FONT_SMALL);
        while (*s) {
            int n = (int)strlen(s), cut = n;
            while (cut > 1 && d->getStringWidth(s, (uint16_t)cut) > w) {
                int sp = cut - 1;
                while (sp > 0 && s[sp] != ' ')
                    sp--;
                cut = sp > 0 ? sp : cut - 1;
            }
            char keep = s[cut];
            s[cut] = 0;
            d->drawString(x, y, s);
            s[cut] = keep;
            s += cut;
            while (*s == ' ')
                s++;
            y += lh;
        }
    }
};

static TttApp ttt;

// invitation that arrived while the keypad thread was busy; shown by tttPoll()
static uint32_t inviteFrom = 0;
static uint16_t inviteGid = 0;
static bool inviteBanner = false;

static void reply(uint32_t to, char type, uint16_t gid)
{
    uint8_t p[7] = {'B', 'T', 'T', (uint8_t)type, (uint8_t)(gid & 0xFF), (uint8_t)(gid >> 8), 0};
    meshSendPrivate(to, p, sizeof(p), true);
}

static void onTttPacket(uint32_t from, const uint8_t *b, size_t len)
{
    if (len < 4)
        return;
    char type = (char)b[0];
    uint16_t gid = (uint16_t)(b[1] | (b[2] << 8));
    if (type == 'I') {
        if (appActive() || inviteFrom) {
            reply(from, 'D', gid); // busy
            return;
        }
        inviteFrom = from;
        inviteGid = gid;
        inviteBanner = true;
        return;
    }
    ttt.onPacket(from, type, gid, b[3]);
}

void tttPoll()
{
    static bool registered = false;
    if (!registered) {
        registered = true;
        meshSetTttHandler(onTttPacket);
    }
    if (!inviteBanner || !screen || screen->isOverlayBannerShowing() || appActive())
        return;
    inviteBanner = false;
    static char msg[80];
    char name[40];
    snprintf(msg, sizeof(msg), "%s wants to play\ntic-tac-toe", meshNodeName(inviteFrom, name, sizeof(name)));
    static const char *opts[] = {"Play", "No thanks"};
    graphics::BannerOverlayOptions o;
    o.message = msg;
    o.optionsArrayPtr = opts;
    o.optionsCount = 2;
    o.durationMs = 60000;
    o.bannerCallback = [](int sel) -> void {
        uint32_t from = inviteFrom;
        inviteFrom = 0;
        if (sel == 0) {
            reply(from, 'A', inviteGid);
            ttt.setupMesh(from, false, inviteGid);
            appOpen(&ttt);
        } else {
            reply(from, 'D', inviteGid);
        }
    };
    screen->showOverlayBanner(o);
}

void openTicTacToeCpu()
{
    ttt.setupCpu();
    appOpen(&ttt);
}

void openTicTacToeMesh(uint32_t opponent, bool weInvited)
{
    if (!opponent || opponent == meshMyNode())
        return;
    uint16_t gid = (uint16_t)random(1, 65535);
    ttt.setupMesh(opponent, weInvited, gid);
    if (weInvited) {
        uint8_t p[7] = {'B', 'T', 'T', 'I', (uint8_t)(gid & 0xFF), (uint8_t)(gid >> 8), 0};
        meshSendPrivate(opponent, p, sizeof(p), true);
    }
    appOpen(&ttt);
}

} // namespace badge

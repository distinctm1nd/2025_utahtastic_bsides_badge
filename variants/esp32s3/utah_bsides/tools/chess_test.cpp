// PC test for BadgeChess.h: perft counts from the Chess Programming Wiki, and the computer
// finding simple mates.   g++ -std=c++17 -O2 -I.. chess_test.cpp -o chess_test && ./chess_test
#include "../BadgeChess.h"
#include <cstdio>
#include <chrono>
using namespace badge;
static int fails = 0;
static void perft(const char *name, const char *fen, int depth, uint64_t want)
{
    static Chess c;
    c.loadFen(fen);
    auto t0 = std::chrono::steady_clock::now();
    uint64_t got = c.perft(depth);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    bool ok = got == want;
    fails += !ok;
    printf("%s  %-10s depth %d: %llu (want %llu)  %.0f ms\n", ok ? "PASS" : "FAIL", name, depth,
           (unsigned long long)got, (unsigned long long)want, ms);
}
static void mate(const char *name, const char *fen, const char *want)
{
    static Chess c;
    c.loadFen(fen);
    ChessMove m;
    c.think(m, 30000);
    char got[5] = {(char)('a' + (m.from & 7)), (char)('1' + (m.from >> 4)), (char)('a' + (m.to & 7)), (char)('1' + (m.to >> 4)), 0};
    bool ok = strstr(want, got) != nullptr; // want may list several good moves
    fails += !ok;
    printf("%s  %s: plays %s (want %s), %ld nodes\n", ok ? "PASS" : "FAIL", name, got, want, c.nodes);
}
int main()
{
    const char *start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    const char *kiwi = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -";
    const char *pos3 = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -";
    const char *pos4 = "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1";
    const char *pos5 = "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8";
    perft("start", start, 1, 20);
    perft("start", start, 3, 8902);
    perft("start", start, 4, 197281);
    perft("kiwipete", kiwi, 1, 48);
    perft("kiwipete", kiwi, 2, 2039);
    perft("kiwipete", kiwi, 3, 97862);
    perft("pos3", pos3, 4, 43238);
    perft("pos3", pos3, 5, 674624);
    perft("pos4", pos4, 3, 9467);
    perft("pos5", pos5, 3, 62379);
    mate("mate in 1 (back rank)", "6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1", "a1a8");
    mate("mate in 1 (queen)", "k7/8/1K6/8/8/8/8/7Q w - - 0 1", "h1h8 h1b7");
    mate("mate in 2", "6k1/5ppp/8/8/8/8/1Q6/1R4K1 w - - 0 1", "b2b8 b1b8");
    mate("takes a free queen", "4k3/8/8/3q4/8/8/8/3RK3 w - - 0 1", "d1d5");
    {
        Chess c;
        c.loadFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
        bool ok = c.status() == Chess::STALEMATE;
        fails += !ok;
        printf("%s  stalemate detected\n", ok ? "PASS" : "FAIL");
        c.loadFen("7k/6Q1/6K1/8/8/8/8/8 b - - 0 1");
        ok = c.status() == Chess::CHECKMATE;
        fails += !ok;
        printf("%s  checkmate detected\n", ok ? "PASS" : "FAIL");
    }
    {
        // how long a move takes at the badge's budget (the badge is roughly 10-20x slower)
        Chess c;
        ChessMove m;
        auto t0 = std::chrono::steady_clock::now();
        c.think(m, 15000);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        printf("INFO  opening move with 15000 nodes: %.1f ms on this PC\n", ms);
    }
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}

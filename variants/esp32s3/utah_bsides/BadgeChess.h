// Chess rules and a small computer player for the badge. Pure C++ (no Arduino), so the move
// generator is checked on a PC against the standard "perft" move counts in tools/keypad_test.cpp.
//
// Board: 0x88 layout, square = rank * 16 + file, rank 0 = White's back rank. Pieces: 1 pawn,
// 2 knight, 3 bishop, 4 rook, 5 queen, 6 king; positive White, negative Black.
#pragma once
#include <stdint.h>
#include <string.h>

namespace badge
{

struct ChessMove {
    uint8_t from, to;
    int8_t promo;  // piece type promoted to, 0 if none
    uint8_t flags; // CM_DOUBLE, CM_EP, CM_CASTLE
};

class Chess
{
  public:
    enum { EMPTY = 0, PAWN = 1, KNIGHT, BISHOP, ROOK, QUEEN, KING };
    enum { CM_DOUBLE = 1, CM_EP = 2, CM_CASTLE = 4 };
    static const int MAXMOVES = 160; // more than any real game position needs
    static const int MAXPLY = 12;

    int8_t board[128];
    int side = 1;       // 1 White to move, -1 Black
    uint8_t castling;   // 1 White short, 2 White long, 4 Black short, 8 Black long
    int ep = -1;        // en passant target square, -1 none
    int kingSq[2];      // [0] White, [1] Black
    int halfmoves = 0;  // since the last capture or pawn move (50-move rule)

    Chess() { reset(); }
    void reset() { loadFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"); }

    static bool onBoard(int s) { return !(s & 0x88); }
    static int sq(int file, int rank) { return rank * 16 + file; }
    static int fileOf(int s) { return s & 7; }
    static int rankOf(int s) { return s >> 4; }

    bool loadFen(const char *f)
    {
        memset(board, 0, sizeof(board));
        int rank = 7, file = 0;
        for (; *f && *f != ' '; f++) {
            char c = *f;
            if (c == '/') {
                rank--, file = 0;
            } else if (c >= '1' && c <= '8') {
                file += c - '0';
            } else {
                int t = pieceOf(c);
                if (!t || rank < 0 || file > 7)
                    return false;
                board[sq(file, rank)] = (int8_t)((c >= 'a') ? -t : t);
                if (t == KING)
                    kingSq[c >= 'a' ? 1 : 0] = sq(file, rank);
                file++;
            }
        }
        while (*f == ' ')
            f++;
        side = (*f == 'b') ? -1 : 1;
        while (*f && *f != ' ')
            f++;
        while (*f == ' ')
            f++;
        castling = 0;
        for (; *f && *f != ' '; f++)
            castling |= *f == 'K' ? 1 : *f == 'Q' ? 2 : *f == 'k' ? 4 : *f == 'q' ? 8 : 0;
        while (*f == ' ')
            f++;
        ep = -1;
        if (f[0] >= 'a' && f[0] <= 'h' && f[1] >= '1' && f[1] <= '8')
            ep = sq(f[0] - 'a', f[1] - '1');
        halfmoves = 0;
        return true;
    }

    // is square s attacked by side `by`?
    bool attacked(int s, int by) const
    {
        // pawns
        int p1 = s - by * 15, p2 = s - by * 17;
        if (onBoard(p1) && board[p1] == by * PAWN)
            return true;
        if (onBoard(p2) && board[p2] == by * PAWN)
            return true;
        static const int8_t KN[8] = {33, 31, 18, 14, -33, -31, -18, -14};
        static const int8_t KG[8] = {17, 15, -17, -15, 16, -16, 1, -1};
        for (int d : KN) {
            int t = s + d;
            if (onBoard(t) && board[t] == by * KNIGHT)
                return true;
        }
        for (int d : KG) {
            int t = s + d;
            if (onBoard(t) && board[t] == by * KING)
                return true;
        }
        for (int i = 0; i < 8; i++) {
            int d = KG[i];
            bool diag = i < 4;
            for (int t = s + d; onBoard(t); t += d) {
                int pc = board[t];
                if (!pc)
                    continue;
                if (pc == by * QUEEN || pc == by * (diag ? BISHOP : ROOK))
                    return true;
                break;
            }
        }
        return false;
    }
    bool inCheck(int who) const { return attacked(kingSq[who > 0 ? 0 : 1], -who); }

    // all moves, some of which may leave the king in check (filtered by legal())
    int pseudo(ChessMove *out) const
    {
        int n = 0;
        static const int8_t KN[8] = {33, 31, 18, 14, -33, -31, -18, -14};
        static const int8_t KG[8] = {17, 15, -17, -15, 16, -16, 1, -1};
        for (int s = 0; s < 128; s++) {
            if (!onBoard(s)) {
                s += 7;
                continue;
            }
            int pc = board[s] * side;
            if (pc <= 0)
                continue;
            if (pc == PAWN) {
                int fwd = side * 16, start = side > 0 ? 1 : 6, last = side > 0 ? 7 : 0;
                int t = s + fwd;
                if (onBoard(t) && !board[t]) {
                    n = addPawn(out, n, s, t, rankOf(t) == last, 0);
                    int t2 = t + fwd;
                    if (rankOf(s) == start && !board[t2])
                        out[n++] = {(uint8_t)s, (uint8_t)t2, 0, CM_DOUBLE};
                }
                for (int dx = -1; dx <= 1; dx += 2) {
                    int c = s + fwd + dx;
                    if (!onBoard(c))
                        continue;
                    if (board[c] * side < 0)
                        n = addPawn(out, n, s, c, rankOf(c) == last, 0);
                    else if (c == ep)
                        out[n++] = {(uint8_t)s, (uint8_t)c, 0, CM_EP};
                }
            } else if (pc == KNIGHT || pc == KING) {
                const int8_t *dirs = pc == KNIGHT ? KN : KG;
                for (int i = 0; i < 8; i++) {
                    int t = s + dirs[i];
                    if (onBoard(t) && board[t] * side <= 0)
                        out[n++] = {(uint8_t)s, (uint8_t)t, 0, 0};
                }
            } else {
                int from = pc == ROOK ? 4 : 0, to = pc == BISHOP ? 4 : 8;
                for (int i = from; i < to; i++)
                    for (int t = s + KG[i]; onBoard(t); t += KG[i]) {
                        if (board[t] * side > 0)
                            break;
                        out[n++] = {(uint8_t)s, (uint8_t)t, 0, 0};
                        if (board[t])
                            break;
                    }
            }
            if (n > MAXMOVES - 12)
                break;
        }
        // castling
        int home = side > 0 ? 0 : 0x70;
        uint8_t shortBit = side > 0 ? 1 : 4, longBit = side > 0 ? 2 : 8;
        if ((castling & shortBit) && board[home + 4] == side * KING && board[home + 7] == side * ROOK &&
            !board[home + 5] && !board[home + 6] && !attacked(home + 4, -side) && !attacked(home + 5, -side) &&
            !attacked(home + 6, -side))
            out[n++] = {(uint8_t)(home + 4), (uint8_t)(home + 6), 0, CM_CASTLE};
        if ((castling & longBit) && board[home + 4] == side * KING && board[home] == side * ROOK &&
            !board[home + 3] && !board[home + 2] && !board[home + 1] && !attacked(home + 4, -side) &&
            !attacked(home + 3, -side) && !attacked(home + 2, -side))
            out[n++] = {(uint8_t)(home + 4), (uint8_t)(home + 2), 0, CM_CASTLE};
        return n;
    }

    struct Undo {
        int8_t captured;
        uint8_t castling;
        int ep, halfmoves;
    };

    void make(const ChessMove &m, Undo &u)
    {
        u.castling = castling;
        u.ep = ep;
        u.halfmoves = halfmoves;
        int pc = board[m.from];
        int capSq = (m.flags & CM_EP) ? m.to - side * 16 : m.to;
        u.captured = board[capSq];
        board[capSq] = 0;
        board[m.to] = m.promo ? (int8_t)(side * m.promo) : (int8_t)pc;
        board[m.from] = 0;
        if (pc * side == KING)
            kingSq[side > 0 ? 0 : 1] = m.to;
        if (m.flags & CM_CASTLE) {
            int home = m.to & 0x70;
            if ((m.to & 7) == 6)
                board[home + 5] = board[home + 7], board[home + 7] = 0;
            else
                board[home + 3] = board[home], board[home] = 0;
        }
        ep = (m.flags & CM_DOUBLE) ? m.from + side * 16 : -1;
        castling &= rightsMask(m.from) & rightsMask(m.to);
        halfmoves = (u.captured || pc * side == PAWN) ? 0 : halfmoves + 1;
        side = -side;
    }
    void unmake(const ChessMove &m, const Undo &u)
    {
        side = -side;
        int pc = m.promo ? side * PAWN : board[m.to];
        board[m.from] = (int8_t)pc;
        board[m.to] = 0;
        int capSq = (m.flags & CM_EP) ? m.to - side * 16 : m.to;
        board[capSq] = u.captured;
        if (pc * side == KING)
            kingSq[side > 0 ? 0 : 1] = m.from;
        if (m.flags & CM_CASTLE) {
            int home = m.to & 0x70;
            if ((m.to & 7) == 6)
                board[home + 7] = board[home + 5], board[home + 5] = 0;
            else
                board[home] = board[home + 3], board[home + 3] = 0;
        }
        castling = u.castling;
        ep = u.ep;
        halfmoves = u.halfmoves;
    }

    int legal(ChessMove *out)
    {
        ChessMove tmp[MAXMOVES];
        int n = pseudo(tmp), k = 0;
        for (int i = 0; i < n; i++) {
            Undo u;
            make(tmp[i], u);
            if (!inCheck(-side))
                out[k++] = tmp[i];
            unmake(tmp[i], u);
        }
        return k;
    }

    uint64_t perft(int depth)
    {
        if (depth == 0)
            return 1;
        ChessMove ms[MAXMOVES];
        int n = pseudo(ms);
        uint64_t total = 0;
        for (int i = 0; i < n; i++) {
            Undo u;
            make(ms[i], u);
            if (!inCheck(-side))
                total += perft(depth - 1);
            unmake(ms[i], u);
        }
        return total;
    }

    enum Status { PLAYING, CHECKMATE, STALEMATE, DRAW_50, DRAW_MATERIAL };
    Status status()
    {
        ChessMove ms[MAXMOVES];
        if (legal(ms) == 0)
            return inCheck(side) ? CHECKMATE : STALEMATE;
        if (halfmoves >= 100)
            return DRAW_50;
        int minor = 0, other = 0;
        for (int s = 0; s < 128; s++) {
            if (!onBoard(s) || !board[s])
                continue;
            int t = board[s] < 0 ? -board[s] : board[s];
            if (t == KNIGHT || t == BISHOP)
                minor++;
            else if (t != KING)
                other++;
        }
        if (!other && minor <= 1)
            return DRAW_MATERIAL;
        return PLAYING;
    }

    // ---- the computer player: alpha-beta search with a node budget -------------------------
    // Returns false if there is no legal move. maxNodes bounds the thinking time.
    bool think(ChessMove &best, long maxNodes, int maxDepth = 5)
    {
        ChessMove root[MAXMOVES];
        int n = legal(root);
        if (!n)
            return false;
        best = root[0];
        nodes = 0;
        budget = maxNodes;
        stopped = false;
        order(root, n, nullptr);
        for (int depth = 1; depth <= maxDepth; depth++) {
            int alpha = -INF, bestIdx = -1;
            for (int i = 0; i < n; i++) {
                Undo u;
                make(root[i], u);
                int v = -search(depth - 1, -INF, -alpha, 1);
                unmake(root[i], u);
                // a few points of variety between near-equal moves, so games differ
                if (v > -MATE + 100 && v < MATE - 100)
                    v += (root[i].from * 7 + root[i].to * 13 + noise * 29) % (noise ? 9 : 1);
                if (stopped)
                    break;
                if (v > alpha)
                    alpha = v, bestIdx = i;
            }
            if (stopped || bestIdx < 0)
                break; // keep the last finished depth's choice
            best = root[bestIdx];
            // search the best move first next time
            ChessMove b = root[bestIdx];
            memmove(root + 1, root, sizeof(ChessMove) * bestIdx);
            root[0] = b;
            if (alpha > MATE - 100)
                break; // found a mate
        }
        return true;
    }
    long nodes = 0;

    // a little variety, so the badge doesn't play the same game every time (0..15)
    int noise = 0;

  private:
    static const int INF = 32000;
    static const int MATE = 30000;
    long budget = 0;
    bool stopped = false;
    ChessMove buf[MAXPLY + 8][MAXMOVES];

    static int pieceOf(char c)
    {
        switch (c | 32) {
        case 'p': return PAWN;
        case 'n': return KNIGHT;
        case 'b': return BISHOP;
        case 'r': return ROOK;
        case 'q': return QUEEN;
        case 'k': return KING;
        default: return 0;
        }
    }
    static int addPawn(ChessMove *out, int n, int s, int t, bool promo, uint8_t flags)
    {
        if (!promo) {
            out[n++] = {(uint8_t)s, (uint8_t)t, 0, flags};
            return n;
        }
        for (int p = QUEEN; p >= KNIGHT; p--)
            out[n++] = {(uint8_t)s, (uint8_t)t, (int8_t)p, flags};
        return n;
    }
    static uint8_t rightsMask(int s)
    {
        switch (s) {
        case 0x00: return (uint8_t)~2;
        case 0x07: return (uint8_t)~1;
        case 0x04: return (uint8_t)~3;
        case 0x70: return (uint8_t)~8;
        case 0x77: return (uint8_t)~4;
        case 0x74: return (uint8_t)~12;
        default: return 0xFF;
        }
    }

    static int value(int t)
    {
        static const int V[7] = {0, 100, 320, 330, 500, 900, 0};
        return V[t];
    }
    // material plus simple square bonuses, from the side to move's point of view
    int evaluate() const
    {
        int score = 0;
        for (int s = 0; s < 128; s++) {
            if (!onBoard(s)) {
                s += 7;
                continue;
            }
            int pc = board[s];
            if (!pc)
                continue;
            int col = pc > 0 ? 1 : -1, t = pc * col;
            int f = fileOf(s), r = rankOf(s), adv = col > 0 ? r : 7 - r;
            int centre = 7 - (abs2(2 * f - 7) + abs2(2 * r - 7)) / 2; // 0 corner .. 6 middle
            int v = value(t);
            switch (t) {
            case PAWN: v += adv * adv * 2 + ((f == 3 || f == 4) ? adv * 4 : 0); break;
            case KNIGHT: v += centre * 5 - 10; break;
            case BISHOP: v += centre * 3; break;
            case ROOK: v += adv == 6 ? 15 : 0; break;
            case QUEEN: v += centre; break;
            case KING: v += (adv == 0 && (f <= 2 || f >= 6)) ? 20 : -adv * 6; break;
            }
            score += col * v;
        }
        return score * side;
    }
    static int abs2(int v) { return v < 0 ? -v : v; }

    // captures first (most valuable victim, least valuable attacker)
    void order(ChessMove *ms, int n, int *scratch) const
    {
        (void)scratch;
        int key[MAXMOVES];
        for (int i = 0; i < n; i++) {
            int victim = board[ms[i].to];
            victim = victim < 0 ? -victim : victim;
            int att = board[ms[i].from];
            att = att < 0 ? -att : att;
            key[i] = (victim ? 10 * value(victim) - value(att) + 1000 : 0) + (ms[i].promo ? 800 : 0);
        }
        for (int i = 1; i < n; i++) { // insertion sort, small lists
            ChessMove m = ms[i];
            int k = key[i], j = i - 1;
            while (j >= 0 && key[j] < k) {
                ms[j + 1] = ms[j];
                key[j + 1] = key[j];
                j--;
            }
            ms[j + 1] = m;
            key[j + 1] = k;
        }
    }

    int search(int depth, int alpha, int beta, int ply)
    {
        if (++nodes > budget) {
            stopped = true;
            return 0;
        }
        if (depth <= 0 || ply >= MAXPLY)
            return quiesce(alpha, beta, ply);
        ChessMove *ms = buf[ply];
        int n = pseudo(ms), legalCount = 0;
        order(ms, n, nullptr);
        for (int i = 0; i < n; i++) {
            Undo u;
            make(ms[i], u);
            if (inCheck(-side)) {
                unmake(ms[i], u);
                continue;
            }
            legalCount++;
            int v = -search(depth - 1, -beta, -alpha, ply + 1);
            unmake(ms[i], u);
            if (stopped)
                return 0;
            if (v >= beta)
                return beta;
            if (v > alpha)
                alpha = v;
        }
        if (!legalCount)
            return inCheck(side) ? -(MATE - ply) : 0;
        if (halfmoves >= 100)
            return 0;
        return alpha;
    }
    int quiesce(int alpha, int beta, int ply)
    {
        if (++nodes > budget) {
            stopped = true;
            return 0;
        }
        int stand = evaluate();
        if (stand >= beta)
            return beta;
        if (stand > alpha)
            alpha = stand;
        if (ply >= MAXPLY + 6)
            return alpha;
        ChessMove *ms = buf[ply];
        int n = pseudo(ms), k = 0;
        for (int i = 0; i < n; i++) // captures and promotions only
            if (board[ms[i].to] || (ms[i].flags & CM_EP) || ms[i].promo)
                ms[k++] = ms[i];
        order(ms, k, nullptr);
        for (int i = 0; i < k; i++) {
            Undo u;
            make(ms[i], u);
            if (inCheck(-side)) {
                unmake(ms[i], u);
                continue;
            }
            int v = -quiesce(-beta, -alpha, ply + 1);
            unmake(ms[i], u);
            if (stopped)
                return 0;
            if (v >= beta)
                return beta;
            if (v > alpha)
                alpha = v;
        }
        return alpha;
    }
};

} // namespace badge

// Plays a scripted run of Skinwalker Mine on a PC and writes frames for a look before flashing.
//   g++ -std=c++17 -O2 -I.. mine_preview.cpp -o mine_preview && ./mine_preview > mine.raw
//   python3 mine_preview.py mine.raw      (writes mine.gif, needs Pillow)
// Also checks the level and that the game can be won and lost.
#include "../BadgeMine.h"
#include <chrono>
#include <cstdio>
using namespace badge;
static Mine g;
static uint8_t fb[Mine::FB_BYTES];
static int frames = 0;
static void out()
{
    g.render(fb);
    fwrite(fb, 1, sizeof(fb), stdout);
    frames++;
}
int main()
{
    int fails = 0;
    // every chapter: rows the right width, walls all round, enemies within limits, and every
    // skinwalker and item reachable from the start
    for (int c = 0; c < Mine::CHAPTERS; c++) {
        g.reset(3, c);
        int bad = 0, starts = 0, count = 0;
        for (int y = 0; y < Mine::MAP_H; y++) {
            if (strlen(g.level[y]) != Mine::MAP_W) {
                fprintf(stderr, "FAIL chapter %d row %d is %zu wide\n", c + 1, y, strlen(g.level[y]));
                bad++;
                continue;
            }
            for (int x = 0; x < Mine::MAP_W; x++) {
                char ch = g.level[y][x];
                bool edge = x == 0 || y == 0 || x == Mine::MAP_W - 1 || y == Mine::MAP_H - 1;
                if (edge && !g.wall(x, y)) { fprintf(stderr, "FAIL chapter %d open edge at %d,%d\n", c + 1, x, y); bad++; }
                starts += ch == 'P';
                count += ch == 'S' || ch == 'C' || ch == 'B';
            }
        }
        if (starts != 1 || count > Mine::MAX_ENEMIES || count != g.total) { fprintf(stderr, "FAIL chapter %d: %d starts, %d enemies\n", c + 1, starts, count); bad++; }
        bool seen[Mine::MAP_H][Mine::MAP_W] = {};
        int qx[400], qy[400], h = 0, t = 0;
        qx[t] = (int)g.px; qy[t++] = (int)g.py; seen[(int)g.py][(int)g.px] = true;
        while (h < t) {
            int x = qx[h], y = qy[h++];
            const int d[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
            for (auto &k : d) { int nx = x + k[0], ny = y + k[1];
                if (!g.wall(nx, ny) && !seen[ny][nx]) { seen[ny][nx] = true; qx[t] = nx; qy[t++] = ny; } }
        }
        for (int i = 0; i < g.nEnemies; i++) bad += !seen[(int)g.enemies[i].y][(int)g.enemies[i].x];
        for (int i = 0; i < g.nItems; i++) bad += !seen[(int)g.items[i].y][(int)g.items[i].x];
        int sk = 0, co = 0, bo = 0;
        for (int i = 0; i < g.nEnemies; i++) { sk += g.enemies[i].kind == 'S'; co += g.enemies[i].kind == 'C'; bo += g.enemies[i].kind == 'B'; }
        fprintf(stderr, "%s  chapter %d %-18s %d skinwalkers, %d coyotes, %d boss, %d items, all reachable\n", bad ? "FAIL" : "PASS",
                c + 1, Mine::chapterName(c), sk, co, bo, g.nItems);
        fails += bad != 0;
    }
    g.reset(7);
    g.reset(7);
    fprintf(stderr, "level: %d skinwalkers, %d items\n", g.total, g.nItems);
    Mine::Input in = {};
    // walk forward, turn, look around, shoot
    for (int f = 0; f < 140; f++) {
        in = {};
        if (f < 30) in.fwd = true;
        else if (f < 50) in.right = true;
        else if (f < 80) in.fwd = true;
        else if (f < 100) in.left = true;
        else in.fire = (f % 6) == 0;
        g.update(1.0f / 15, in);
        if (f % 2 == 0) out();
    }
    // timing of one render
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; i++) g.render(fb);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 100;
    fprintf(stderr, "INFO render %.3f ms on this PC\n", ms);
    // aim straight at one in the open and shoot: it dies after a few shots
    {
        g.reset(3);
        Mine::Enemy &e = g.enemies[0];
        g.px = e.x - 2.0f; g.py = e.y; g.angle = 0;
        while (g.wall((int)g.px, (int)g.py)) g.px += 0.5f;
        g.angle = atan2f(e.y - g.py, e.x - g.px);
        int shots = 0;
        for (int i = 0; i < 15 * 10 && e.alive; i++) {
            in = {}; in.fire = true; g.health = 100;
            g.angle = atan2f(e.y - g.py, e.x - g.px);
            if (g.update(1.0f / 15, in)) shots++;
        }
        fprintf(stderr, "%s  aimed shots kill a skinwalker (%d shots)\n", !e.alive ? "PASS" : "FAIL", shots);
        fails += e.alive;
    }
    // a skinwalker that reaches you hurts you
    {
        g.reset(3);
        Mine::Enemy &e = g.enemies[0];
        g.px = e.x + 0.6f; g.py = e.y;
        if (g.wall((int)g.px, (int)g.py)) g.px = e.x - 0.6f;
        for (int i = 0; i < 15 * 30 && !g.dead; i++) { in = {}; g.update(1.0f / 15, in); }
        fprintf(stderr, "%s  standing next to one gets you killed\n", g.dead ? "PASS" : "FAIL");
        fails += !g.dead;
    }
    // clearing a chapter (all enemies killed) moves on; health and bullets carry over
    {
        g.reset(3, 0);
        for (int i = 0; i < g.nEnemies; i++) g.enemies[i].alive = false;
        g.kills = g.total - 1; g.enemies[0].alive = true; g.enemies[0].hp = 0.5f;
        g.px = g.enemies[0].x - 1.5f; g.py = g.enemies[0].y;
        if (g.wall((int)g.px, (int)g.py)) g.px = g.enemies[0].x + 1.5f;
        g.angle = atan2f(g.enemies[0].y - g.py, g.enemies[0].x - g.px);
        Mine::Input fire = {}; fire.fire = true;
        g.health = 50;
        g.update(0.05f, fire);
        bool clear = g.chapterClear && !g.won;
        g.nextChapter();
        bool ok = clear && g.chapter == 1 && g.health == 80 && !g.chapterClear;
        fprintf(stderr, "%s  chapter clear -> next chapter, health carried +30\n", ok ? "PASS" : "FAIL");
        fails += !ok;
        g.reset(3, 4);
        int boss = -1;
        for (int i = 0; i < g.nEnemies; i++) if (g.enemies[i].kind == 'B') boss = i;
        Mine::Enemy &b = g.enemies[boss];
        int shots = 0;
        for (int i = 0; i < 15 * 60 && b.alive; i++) {
            g.px = b.x - 2.0f; g.py = b.y; g.health = 100; g.ammo = 50;
            g.angle = atan2f(b.y - g.py, b.x - g.px);
            Mine::Input in2 = {}; in2.fire = true;
            if (g.update(1.0f / 15, in2)) shots++;
        }
        fprintf(stderr, "%s  the Old One takes %d shots\n", !b.alive && shots >= 8 ? "PASS" : "FAIL", shots);
        fails += b.alive || shots < 8;
    }
    g.reset(5);
    for (int i = 0; i < 15 * 120 && !g.dead; i++) { in = {}; g.update(1.0f / 15, in); }
    // stand still near one: walk to the nearest and wait
    fprintf(stderr, "frames %d\n", frames);
    return fails;
}

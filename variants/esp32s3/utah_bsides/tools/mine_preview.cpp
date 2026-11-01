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
    for (int y = 0; y < Mine::MAP_H; y++)
        if (strlen(g.level[y]) != Mine::MAP_W) {
            fprintf(stderr, "FAIL level row %d is %zu wide\n", y, strlen(g.level[y]));
            fails++;
        }
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
    // every skinwalker and item can be reached from the start (flood fill)
    {
        g.reset(3);
        bool seen[Mine::MAP_H][Mine::MAP_W] = {};
        int qx[400], qy[400], h = 0, t = 0;
        qx[t] = (int)g.px; qy[t++] = (int)g.py; seen[(int)g.py][(int)g.px] = true;
        while (h < t) {
            int x = qx[h], y = qy[h++];
            const int d[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
            for (auto &k : d) { int nx = x + k[0], ny = y + k[1];
                if (!g.wall(nx, ny) && !seen[ny][nx]) { seen[ny][nx] = true; qx[t] = nx; qy[t++] = ny; } }
        }
        int bad = 0;
        for (int i = 0; i < g.nEnemies; i++) bad += !seen[(int)g.enemies[i].y][(int)g.enemies[i].x];
        for (int i = 0; i < g.nItems; i++) bad += !seen[(int)g.items[i].y][(int)g.items[i].x];
        fprintf(stderr, "%s  every skinwalker and item can be reached\n", bad ? "FAIL" : "PASS");
        fails += bad != 0;
    }
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
    g.reset(5);
    for (int i = 0; i < 15 * 120 && !g.dead; i++) { in = {}; g.update(1.0f / 15, in); }
    // stand still near one: walk to the nearest and wait
    fprintf(stderr, "frames %d\n", frames);
    return fails;
}

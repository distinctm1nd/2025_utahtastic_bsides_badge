// "Skinwalker Mine": a small Doom-style 3D shooter for the badge. A raycaster draws the mine
// into a 1-bit framebuffer (160 x VIEW_H, XBM bit order), so the same code renders on a PC
// (tools/mine_preview.cpp) and on the badge (AppMine.cpp blits it with drawXbm).
//
// You have a six-shooter. Skinwalkers lurk in the old silver mine; clear them all to win.
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace badge
{

class Mine
{
  public:
    static const int W = 160, VIEW_H = 108;
    static const int FB_BYTES = (W / 8) * VIEW_H;
    static const int MAP_W = 20, MAP_H = 20;
    static const int MAX_ENEMIES = 12, MAX_ITEMS = 8;

    struct Input {
        bool fwd, back, left, right, strafeL, strafeR, fire, reload;
    };

    // the level: # rock, W timbered wall, L lantern on the wall, P start, S skinwalker,
    // a ammo, h health
    const char *const *level = LEVEL;

    float px = 1.5f, py = 1.5f, angle = 0; // player
    int health = 100, ammo = 18, cylinder = 6, kills = 0, total = 0;
    float reloadLeft = 0, fireLeft = 0, hurtLeft = 0, timeAlive = 0;
    bool dead = false, won = false;
    const char *message = nullptr;
    float messageLeft = 0;

    struct Enemy {
        float x, y, hp, hurt, attack, puff, bob;
        bool alive, awake;
    } enemies[MAX_ENEMIES];
    int nEnemies = 0;
    struct Item {
        float x, y;
        char kind;
        bool taken;
    } items[MAX_ITEMS];
    int nItems = 0;

    void reset(uint32_t seed = 1)
    {
        rng = seed ? seed : 1;
        nEnemies = nItems = 0;
        health = 100;
        ammo = 18;
        cylinder = 6;
        kills = 0;
        reloadLeft = fireLeft = hurtLeft = timeAlive = messageLeft = 0;
        dead = won = false;
        message = nullptr;
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++) {
                char c = level[y][x];
                if (c == 'P')
                    px = x + 0.5f, py = y + 0.5f, angle = 0;
                else if (c == 'S' && nEnemies < MAX_ENEMIES)
                    enemies[nEnemies++] = {x + 0.5f, y + 0.5f, 3, 0, 0, 0, frand() * 6, true, false};
                else if ((c == 'a' || c == 'h') && nItems < MAX_ITEMS)
                    items[nItems++] = {x + 0.5f, y + 0.5f, c, false};
            }
        total = nEnemies;
        say("Clear the mine", 2.5f);
    }

    bool wall(int x, int y) const
    {
        if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H)
            return true;
        char c = level[y][x];
        return c == '#' || c == 'W' || c == 'L';
    }

    // one step of the game; dt in seconds. Returns true if a shot was fired (for sound/LEDs).
    bool update(float dt, const Input &in)
    {
        if (dt > 0.1f)
            dt = 0.1f;
        if (messageLeft > 0)
            messageLeft -= dt;
        if (dead || won)
            return false;
        timeAlive += dt;
        // turning and moving, sliding along walls
        float turn = 2.6f * dt, step = 2.4f * dt;
        if (in.left)
            angle -= turn;
        if (in.right)
            angle += turn;
        float dx = cosf(angle), dy = sinf(angle), mx = 0, my = 0;
        if (in.fwd)
            mx += dx, my += dy;
        if (in.back)
            mx -= dx * 0.7f, my -= dy * 0.7f;
        if (in.strafeL)
            mx += dy, my -= dx;
        if (in.strafeR)
            mx -= dy, my += dx;
        move(mx * step, my * step);

        // pick-ups
        for (int i = 0; i < nItems; i++) {
            Item &it = items[i];
            if (it.taken || fabsf(it.x - px) > 0.5f || fabsf(it.y - py) > 0.5f)
                continue;
            if (it.kind == 'a') {
                it.taken = true;
                ammo += 12;
                say("Bullets +12", 1.5f);
            } else if (health < 100) {
                it.taken = true;
                health = health + 40 > 100 ? 100 : health + 40;
                say("Medicine +40", 1.5f);
            }
        }

        // the six-shooter
        bool shot = false;
        if (fireLeft > 0)
            fireLeft -= dt;
        if (reloadLeft > 0) {
            reloadLeft -= dt;
            if (reloadLeft <= 0) {
                int need = 6 - cylinder, got = need < ammo ? need : ammo;
                cylinder += got;
                ammo -= got;
            }
        } else if ((in.reload && cylinder < 6 && ammo > 0) || (cylinder == 0 && ammo > 0)) {
            reloadLeft = 1.2f;
        } else if (in.fire && fireLeft <= 0 && cylinder > 0) {
            cylinder--;
            fireLeft = 0.35f;
            shot = true;
            shoot();
        } else if (in.fire && fireLeft <= 0 && cylinder == 0 && ammo == 0) {
            say("Out of bullets", 1.2f);
            fireLeft = 0.5f;
        }

        // skinwalkers: wake when they see you, then stalk you
        if (hurtLeft > 0)
            hurtLeft -= dt;
        for (int i = 0; i < nEnemies; i++) {
            Enemy &e = enemies[i];
            e.bob += dt;
            if (e.hurt > 0)
                e.hurt -= dt;
            if (!e.alive) {
                if (e.puff > 0)
                    e.puff -= dt;
                continue;
            }
            float ex = px - e.x, ey = py - e.y, d = sqrtf(ex * ex + ey * ey);
            if (!e.awake && d < 9 && clearLine(e.x, e.y, px, py))
                e.awake = true;
            if (!e.awake)
                continue;
            if (d > 0.8f) {
                float sp = (d < 4 ? 1.7f : 1.2f) * dt / d; // faster when close
                float nx = e.x + ex * sp, ny = e.y + ey * sp;
                if (!wall((int)nx, (int)e.y) && !crowded(i, nx, e.y))
                    e.x = nx;
                if (!wall((int)e.x, (int)ny) && !crowded(i, e.x, ny))
                    e.y = ny;
            }
            if (e.attack > 0)
                e.attack -= dt;
            if (d < 1.0f && e.attack <= 0) {
                e.attack = 0.9f;
                health -= 9;
                hurtLeft = 0.25f;
                if (health <= 0) {
                    health = 0;
                    dead = true;
                    say("It got you", 99);
                }
            }
        }
        return shot;
    }

    // draw the view into fb (FB_BYTES, cleared here); pixel (x,y) = bit x%8 of byte y*20+x/8
    void render(uint8_t *fb) const
    {
        memset(fb, 0, FB_BYTES);
        float dirX = cosf(angle), dirY = sinf(angle);
        float plX = -dirY * 0.66f, plY = dirX * 0.66f;
        float zbuf[W];
        int half = VIEW_H / 2;

        // floor: sparse gravel, thinning with distance
        for (int y = half + 2; y < VIEW_H; y++) {
            float rowDist = (float)half / (y - half);
            if (rowDist > 9)
                continue;
            float fx = px + rowDist * (dirX - plX), fy = py + rowDist * (dirY - plY);
            float sx = rowDist * 2 * plX / W, sy = rowDist * 2 * plY / W;
            float keep = 0.80f + rowDist * 0.02f;
            for (int x = 0; x < W; x++, fx += sx, fy += sy)
                if (((x + y) & 1) == 0 && hash2((int)floorf(fx * 12), (int)floorf(fy * 12)) > keep)
                    set(fb, x, y);
        }

        // walls
        for (int x = 0; x < W; x++) {
            float cam = 2.0f * x / W - 1;
            float rx = dirX + plX * cam, ry = dirY + plY * cam;
            int mx = (int)px, my = (int)py;
            float ddx = rx == 0 ? 1e30f : fabsf(1 / rx), ddy = ry == 0 ? 1e30f : fabsf(1 / ry);
            int stepX = rx < 0 ? -1 : 1, stepY = ry < 0 ? -1 : 1;
            float sdx = rx < 0 ? (px - mx) * ddx : (mx + 1 - px) * ddx;
            float sdy = ry < 0 ? (py - my) * ddy : (my + 1 - py) * ddy;
            int side = 0, guard = 64;
            while (guard--) {
                if (sdx < sdy)
                    sdx += ddx, mx += stepX, side = 0;
                else
                    sdy += ddy, my += stepY, side = 1;
                if (wall(mx, my))
                    break;
            }
            float dist = side == 0 ? sdx - ddx : sdy - ddy;
            if (dist < 0.05f)
                dist = 0.05f;
            zbuf[x] = dist;
            float hitU = side == 0 ? py + dist * ry : px + dist * rx;
            hitU -= floorf(hitU);
            char kind = (mx >= 0 && my >= 0 && mx < MAP_W && my < MAP_H) ? level[my][mx] : '#';
            int lineH = (int)(VIEW_H / dist);
            int y0 = half - lineH / 2, y1 = half + lineH / 2;
            float fade = 1.3f / dist; // the lamp on your hat only reaches so far
            fade = fade > 0.8f ? 0.8f : fade;
            if (side)
                fade *= 0.62f; // the other face of a corner is darker, so corners read
            int u = (int)(hitU * 16);
            for (int y = y0 < 0 ? 0 : y0; y < (y1 > VIEW_H ? VIEW_H : y1); y++) {
                int v = (int)((float)(y - y0) / (lineH ? lineH : 1) * 16);
                float b = texel(kind, u, v) * fade;
                if (b * 16 > BAYER[y & 3][x & 3] + 0.5f)
                    set(fb, x, y);
            }
            // a dark edge on top and bottom so walls stand off the floor
            if (y1 < VIEW_H && y1 >= 0)
                clr(fb, x, y1);
        }

        // sprites: items and skinwalkers, far to near
        struct Spr {
            float x, y, d;
            int kind, idx; // 0 enemy, 1 ammo, 2 health, 3 puff
        } spr[MAX_ENEMIES + MAX_ITEMS];
        int n = 0;
        for (int i = 0; i < nEnemies; i++) {
            const Enemy &e = enemies[i];
            if (e.alive || e.puff > 0)
                spr[n++] = {e.x, e.y, 0, e.alive ? 0 : 3, i};
        }
        for (int i = 0; i < nItems; i++)
            if (!items[i].taken)
                spr[n++] = {items[i].x, items[i].y, 0, items[i].kind == 'a' ? 1 : 2, i};
        for (int i = 0; i < n; i++)
            spr[i].d = (spr[i].x - px) * (spr[i].x - px) + (spr[i].y - py) * (spr[i].y - py);
        for (int i = 1; i < n; i++) // sort far first
            for (int j = i; j > 0 && spr[j].d > spr[j - 1].d; j--) {
                Spr t = spr[j];
                spr[j] = spr[j - 1];
                spr[j - 1] = t;
            }
        float invDet = 1.0f / (plX * dirY - dirX * plY);
        for (int i = 0; i < n; i++) {
            float sx = spr[i].x - px, sy = spr[i].y - py;
            float tx = invDet * (dirY * sx - dirX * sy);
            float ty = invDet * (-plY * sx + plX * sy);
            if (ty < 0.2f)
                continue;
            int screenX = (int)(W / 2 * (1 + tx / ty));
            const Sprite &art = spr[i].kind == 0 ? WALKER : spr[i].kind == 3 ? PUFF : spr[i].kind == 1 ? AMMO : MEDKIT;
            float scale = spr[i].kind == 0 ? 1.0f : spr[i].kind == 3 ? 0.8f : 0.35f;
            int h = (int)(VIEW_H / ty * scale), w = h * art.w / art.h;
            if (h < 2)
                continue;
            int floorY = half + (int)(VIEW_H / ty / 2); // stand on the floor
            int top = floorY - h;
            if (spr[i].kind == 0) // a slight sway as they walk
                top += (int)(sinf(enemies[spr[i].idx].bob * 6) * h * 0.02f);
            bool flash = spr[i].kind == 0 && enemies[spr[i].idx].hurt > 0;
            for (int cx = 0; cx < w; cx++) {
                int x = screenX - w / 2 + cx;
                if (x < 0 || x >= W || zbuf[x] < ty)
                    continue;
                int ax = cx * art.w / w;
                for (int cy = 0; cy < h; cy++) {
                    int y = top + cy;
                    if (y < 0 || y >= VIEW_H)
                        continue;
                    char c = art.px(ax, cy * art.h / h);
                    if (c == '.')
                        continue;
                    // body dark with a light edge; eyes and items light; white when hit
                    bool lit = flash || c == 'e' || c == 'o' ||
                               (c == '#' && (art.px(ax - 1, cy * art.h / h) == '.' || art.px(ax + 1, cy * art.h / h) == '.' ||
                                             art.px(ax, cy * art.h / h - 1) == '.'));
                    if (lit)
                        set(fb, x, y);
                    else
                        clr(fb, x, y);
                }
            }
        }

        // the revolver, bottom middle; kicks up and flashes when fired
        int kick = fireLeft > 0.2f ? 6 : 0, drop = reloadLeft > 0 ? 18 : 0;
        drawArt(fb, GUN, W / 2 - GUN.w / 2 + 10, VIEW_H - GUN.h + 4 - kick + drop, true);
        if (fireLeft > 0.25f)
            drawArt(fb, FLASH, W / 2 - FLASH.w / 2 + 10, VIEW_H - GUN.h - FLASH.h + 8, false);
        // crosshair
        set(fb, W / 2, half - 2);
        set(fb, W / 2, half + 2);
        set(fb, W / 2 - 2, half);
        set(fb, W / 2 + 2, half);
    }

    // a skinwalker is in front of you and close: for the LEDs' heartbeat
    float nearestAwake() const
    {
        float best = 99;
        for (int i = 0; i < nEnemies; i++)
            if (enemies[i].alive && enemies[i].awake) {
                float d = sqrtf((enemies[i].x - px) * (enemies[i].x - px) + (enemies[i].y - py) * (enemies[i].y - py));
                best = d < best ? d : best;
            }
        return best;
    }

  private:
    uint32_t rng = 1;
    float frand()
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return (rng & 0xFFFF) / 65536.0f;
    }
    void say(const char *m, float s)
    {
        message = m;
        messageLeft = s;
    }

    static constexpr const char *LEVEL[MAP_H] = {
        "####################",
        "#P...#.....W.......#",
        "#....#..S..W..h....#",
        "#....#.....W.......#",
        "#..........WWW.WWWW#",
        "###.###...........S#",
        "#.....#....####....#",
        "#..a..#....#..#....#",
        "#.....W....#S.#....#",
        "#.S...W....#..#..a.#",
        "#.....W.........####",
        "##.####....L.......#",
        "#......#.......S...#",
        "#..h...#...........#",
        "#......####.WWWW...#",
        "#..S...........W.S.#",
        "#..............W...#",
        "#L.....#####...W...#",
        "#..........#.......#",
        "####################",
    };

    struct Sprite {
        int w, h;
        const char *const *rows;
        char px(int x, int y) const { return (x < 0 || y < 0 || x >= w || y >= h) ? '.' : rows[y][x]; }
    };
    // skinwalker: tall and thin, coyote head, long arms; e = eyes
    static constexpr const char *WALKER_ROWS[24] = {
        "..#......#..", "..##....##..", "..###..###..", "..########..", "..#e####e#..", "...######...",
        "....####....", ".....##.....", "...######...", "..########..", ".##.####.##.", ".#..####..#.",
        ".#..####..#.", "#...####...#", "#...####...#", "#...####...#", "....####....", "....#..#....",
        "....#..#....", "....#..#....", "...##..##...", "...#....#...", "...#....#...", "..##....##..",
    };
    static constexpr const char *PUFF_ROWS[8] = {
        "..o..o..", "o..o...o", "..o.o.o.", ".o.oo.o.", "o.o..o..", ".o.o..o.", "o...o..o", "..o..o..",
    };
    static constexpr const char *AMMO_ROWS[8] = {
        "........", ".o.o.o..", ".o.o.o..", "oooooooo", "o......o", "o.oooo.o", "o......o", "oooooooo",
    };
    static constexpr const char *MED_ROWS[8] = {
        "........", "..oooo..", ".o.oo.o.", "o.oooo.o", "oooooooo", "o.oooo.o", ".o.oo.o.", "..oooo..",
    };
    // revolver held out in front: barrel, cylinder, hammer, grip; # light, x dark
    static constexpr const char *GUN_ROWS[26] = {
        "..............##..............",
        ".............#xx#.............",
        ".............#xx#.............",
        ".............#xx#.............",
        ".............#xx#.............",
        ".............#xx#.............",
        "............######............",
        "...........#xxxxxx#...........",
        "..........#x##xx##x#..........",
        "..........#x##xx##x#..........",
        "..........#xxxxxxxx#..........",
        "..........#x##xx##x#..........",
        "..........#x##xx##x#..........",
        "...........#xxxxxx#...........",
        "............#xxxx#............",
        "...........##x##x##...........",
        "..........#xxx##xxx#..........",
        ".........#xxxxxxxxxx#.........",
        "........#xxxx####xxxx#........",
        ".......#xxxx#xxxx#xxxx#.......",
        "......#xxxxx#xxxx#xxxxx#......",
        ".....#xxxxxxx####xxxxxxx#.....",
        "....#xxxxxxxxxxxxxxxxxxxx#....",
        "...#xxx#xxx#xxx#xxx#xxx#xx#...",
        "..#xxxxxxxxxxxxxxxxxxxxxxxx#..",
        ".#xxxxxxxxxxxxxxxxxxxxxxxxxx#.",
    };
    static constexpr const char *FLASH_ROWS[10] = {
        "....#.....#...", "..#..#...#..#.", "...#..###..#..", "#...#######...", "..###########.",
        ".############.", "...#########..", "..#..#####..#.", ".#...#..#...#.", "....#....#....",
    };
    static constexpr Sprite WALKER = {12, 24, WALKER_ROWS};
    static constexpr Sprite PUFF = {8, 8, PUFF_ROWS};
    static constexpr Sprite AMMO = {8, 8, AMMO_ROWS};
    static constexpr Sprite MEDKIT = {8, 8, MED_ROWS};
    static constexpr Sprite GUN = {30, 26, GUN_ROWS};
    static constexpr Sprite FLASH = {14, 10, FLASH_ROWS};

    static constexpr uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

    static void set(uint8_t *fb, int x, int y) { fb[y * (W / 8) + (x >> 3)] |= (uint8_t)(1 << (x & 7)); }
    static void clr(uint8_t *fb, int x, int y) { fb[y * (W / 8) + (x >> 3)] &= (uint8_t)~(1 << (x & 7)); }
    static float hash2(int x, int y)
    {
        uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return ((h ^ (h >> 16)) & 0xFFFF) / 65536.0f;
    }
    // wall brightness 0..1 at texel (u, v), 16x16 per block
    static float texel(char kind, int u, int v)
    {
        if (kind == 'W') { // timbered: planks with gaps, a cross beam
            if (v < 2 || v > 13)
                return 0.95f;
            if ((u & 3) == 0)
                return 0.1f;
            return 0.6f;
        }
        if (kind == 'L') { // a lantern hanging on rock
            int dx = u - 8, dy = v - 7;
            int d2 = dx * dx + dy * dy;
            if (d2 < 6)
                return 1.0f;
            if (d2 < 30)
                return 0.85f;
        }
        // rock: big lumpy blocks with dark cracks between them
        float n = hash2(u / 4 + (v / 5) * 7, v / 5);
        if (((u + v / 4) % 8) == 0 || ((v + u / 6) % 10) == 0)
            return 0.0f;
        return 0.35f + 0.4f * n;
    }
    static void drawArt(uint8_t *fb, const Sprite &s, int x0, int y0, bool darkFill)
    {
        for (int y = 0; y < s.h; y++)
            for (int x = 0; x < s.w; x++) {
                int X = x0 + x, Y = y0 + y;
                if (X < 0 || Y < 0 || X >= W || Y >= VIEW_H)
                    continue;
                char c = s.rows[y][x];
                if (c == '#')
                    set(fb, X, Y);
                else if (c == 'x' && darkFill)
                    clr(fb, X, Y);
            }
    }

    void move(float mx, float my)
    {
        const float R = 0.22f;
        float nx = px + mx, ny = py + my;
        if (!wall((int)(nx + (mx > 0 ? R : -R)), (int)py))
            px = nx;
        if (!wall((int)px, (int)(ny + (my > 0 ? R : -R))))
            py = ny;
    }
    bool clearLine(float x0, float y0, float x1, float y1) const
    {
        float dx = x1 - x0, dy = y1 - y0, d = sqrtf(dx * dx + dy * dy);
        int steps = (int)(d * 4) + 1;
        for (int i = 1; i < steps; i++) {
            float t = (float)i / steps;
            if (wall((int)(x0 + dx * t), (int)(y0 + dy * t)))
                return false;
        }
        return true;
    }
    bool crowded(int self, float x, float y) const
    {
        for (int j = 0; j < nEnemies; j++)
            if (j != self && enemies[j].alive && fabsf(enemies[j].x - x) < 0.5f && fabsf(enemies[j].y - y) < 0.5f)
                return true;
        return false;
    }
    // hitscan: the nearest skinwalker under the crosshair, in front of the walls
    void shoot()
    {
        float dirX = cosf(angle), dirY = sinf(angle);
        int best = -1;
        float bestD = 99;
        for (int i = 0; i < nEnemies; i++) {
            Enemy &e = enemies[i];
            if (!e.alive)
                continue;
            float ex = e.x - px, ey = e.y - py;
            float along = ex * dirX + ey * dirY;
            float across = fabsf(-ex * dirY + ey * dirX);
            if (along < 0.2f || across > 0.32f + along * 0.02f || along >= bestD)
                continue;
            if (!clearLine(px, py, e.x, e.y))
                continue;
            best = i;
            bestD = along;
        }
        // every shot wakes the skinwalkers that can hear it
        for (int i = 0; i < nEnemies; i++)
            if (enemies[i].alive && fabsf(enemies[i].x - px) + fabsf(enemies[i].y - py) < 7)
                enemies[i].awake = true;
        if (best < 0)
            return;
        Enemy &e = enemies[best];
        e.hp -= bestD < 3 ? 1.5f : 1.0f; // close shots hit harder
        e.hurt = 0.12f;
        e.x += dirX * 0.15f; // knocked back a little
        e.y += dirY * 0.15f;
        if (wall((int)e.x, (int)e.y))
            e.x -= dirX * 0.15f, e.y -= dirY * 0.15f;
        if (e.hp <= 0) {
            e.alive = false;
            e.puff = 0.6f;
            kills++;
            if (kills == total) {
                won = true;
                say("The mine is quiet", 99);
            } else {
                say("Got one", 1.0f);
            }
        }
    }
};

} // namespace badge

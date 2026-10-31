// Built-in LED scenes for the Utah BSides badge. See BadgeScenes.h.
//
// Adding a scene: write a function like the ones below and add it to SCENES[] at the end
// of this file. c.out[i] takes red, green, blue from 0 to 1; brightness and gamma are
// applied afterwards. LEDS[i] gives the position of every LED (see BadgeScenes.h).
#include "BadgeScenes.h"
#include <math.h>
#include <string.h>

namespace badge
{

// ------------------------------------------------------------------------------------
// Board geometry
// ------------------------------------------------------------------------------------
const LedInfo LEDS[NUM_LEDS] = {
    // keypad strip (GPIO4), in chain order: row 1 right->left, row 2 left->right, ...
    {0.902f, 0.550f, 1.000f, 0.000f, GROUP_KEYPAD, 2, 0},  // D1  behind 3
    {0.751f, 0.546f, 0.500f, 0.000f, GROUP_KEYPAD, 1, 0},  // D2  behind 2
    {0.583f, 0.550f, 0.000f, 0.000f, GROUP_KEYPAD, 0, 0},  // D3  behind 1
    {0.585f, 0.675f, 0.000f, 0.333f, GROUP_KEYPAD, 0, 1},  // D4  behind 4
    {0.749f, 0.673f, 0.500f, 0.333f, GROUP_KEYPAD, 1, 1},  // D5  behind 5
    {0.902f, 0.675f, 1.000f, 0.333f, GROUP_KEYPAD, 2, 1},  // D6  behind 6
    {0.902f, 0.800f, 1.000f, 0.667f, GROUP_KEYPAD, 2, 2},  // D7  behind 9
    {0.749f, 0.799f, 0.500f, 0.667f, GROUP_KEYPAD, 1, 2},  // D8  behind 8
    {0.585f, 0.800f, 0.000f, 0.667f, GROUP_KEYPAD, 0, 2},  // D9  behind 7
    {0.585f, 0.923f, 0.000f, 1.000f, GROUP_KEYPAD, 0, 3},  // D10 behind *
    {0.751f, 0.921f, 0.500f, 1.000f, GROUP_KEYPAD, 1, 3},  // D11 behind 0
    {0.902f, 0.920f, 1.000f, 1.000f, GROUP_KEYPAD, 2, 3},  // D12 behind #
    {0.498f, 0.921f, -0.500f, 1.000f, GROUP_KEYPAD, -1, 3}, // D13 left of *
    // logo strip (GPIO17): the lower pair first ("eyes"), then the four on the sign
    {0.195f, 0.377f, 0.000f, 0.000f, GROUP_EYES, -1, -1}, // D20
    {0.125f, 0.394f, 1.000f, 0.000f, GROUP_EYES, -1, -1}, // D21
    {0.102f, 0.230f, 0.000f, 1.000f, GROUP_LOGO, -1, -1}, // D22
    {0.108f, 0.139f, 0.333f, 0.422f, GROUP_LOGO, -1, -1}, // D23
    {0.078f, 0.073f, 0.667f, 0.000f, GROUP_LOGO, -1, -1}, // D24
    {0.055f, 0.186f, 1.000f, 0.719f, GROUP_LOGO, -1, -1}, // D25
    // moon (GPIO16); gx = angle around the moon 0..1, gy = distance from its centre
    {0.560f, 0.385f, 0.065f, 1.084f, GROUP_MOON, -1, -1}, // D14
    {0.551f, 0.222f, 0.886f, 1.218f, GROUP_MOON, -1, -1}, // D15
    {0.306f, 0.271f, 0.569f, 1.009f, GROUP_MOON, -1, -1}, // D16
    {0.331f, 0.407f, 0.391f, 0.940f, GROUP_MOON, -1, -1}, // D17
    {0.399f, 0.365f, 0.354f, 0.349f, GROUP_MOON, -1, -1}, // D18
    {0.499f, 0.333f, 0.012f, 0.539f, GROUP_MOON, -1, -1}, // D19
};

//                                    *   0   #   9  8  7  6  5  4  3  2  1  up rt dn lt
const int8_t KEY_BIT_TO_LED[16] = {9, 10, 11, 6, 7, 8, 5, 4, 3, 0, 1, 2, -1, -1, -1, -1};

// ------------------------------------------------------------------------------------
// Helpers
// ------------------------------------------------------------------------------------
static const float TAU = 6.2831853f;

static inline float clamp01(float v)
{
    return v < 0 ? 0 : (v > 1 ? 1 : v);
}
static inline float fractf(float v)
{
    return v - floorf(v);
}
static inline float mixf(float a, float b, float k)
{
    return a + (b - a) * k;
}
static inline float smooth(float v)
{
    v = clamp01(v);
    return v * v * (3 - 2 * v);
}
static inline void set(Rgb &o, float r, float g, float b)
{
    o[0] = r;
    o[1] = g;
    o[2] = b;
}
static inline void setHex(Rgb &o, uint32_t hex, float k = 1)
{
    o[0] = ((hex >> 16) & 0xFF) / 255.0f * k;
    o[1] = ((hex >> 8) & 0xFF) / 255.0f * k;
    o[2] = (hex & 0xFF) / 255.0f * k;
}
static inline void mixHex(Rgb &o, uint32_t a, uint32_t b, float k, float level = 1)
{
    Rgb ca, cb;
    setHex(ca, a);
    setHex(cb, b);
    for (int j = 0; j < 3; j++)
        o[j] = mixf(ca[j], cb[j], k) * level;
}
static inline void add(Rgb &o, float r, float g, float b)
{
    o[0] += r;
    o[1] += g;
    o[2] += b;
}
static void hsv(Rgb &o, float h, float s, float v)
{
    h = fractf(h) * 6;
    int i = (int)h;
    float f = h - i, p = v * (1 - s), q = v * (1 - s * f), u = v * (1 - s * (1 - f));
    switch (i) {
    case 0: set(o, v, u, p); break;
    case 1: set(o, q, v, p); break;
    case 2: set(o, p, v, u); break;
    case 3: set(o, p, q, v); break;
    case 4: set(o, u, p, v); break;
    default: set(o, v, p, q); break;
    }
}
// black -> red -> orange -> yellow -> white
static void heat(Rgb &o, float k)
{
    k = clamp01(k);
    set(o, clamp01(k * 3), clamp01(k * 3 - 1) * 0.8f, clamp01(k * 3 - 2) * 0.6f);
}
static uint32_t rngState = 0x2025B5D5u;
static inline float frand()
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return (rngState & 0xFFFFFF) / 16777216.0f;
}
static inline float hash1(float n)
{
    return fractf(sinf(n * 127.1f) * 43758.5453f);
}
// smooth 1-D value noise, 0..1
static float noise1(float x)
{
    float i = floorf(x), f = x - i;
    return mixf(hash1(i), hash1(i + 1), smooth(f));
}
static void fill(SceneCtx &c, float r, float g, float b)
{
    for (int i = 0; i < NUM_LEDS; i++)
        set(c.out[i], r, g, b);
}
// how far up a column of the keypad an LED is, 0 = bottom row, 1 = top row; logo/moon by y
static inline float heightOf(int i)
{
    return 1.0f - LEDS[i].y;
}

// ------------------------------------------------------------------------------------
// Classic: the palettes of the 2025 firmware, now as smooth chases
// ------------------------------------------------------------------------------------
static void palette(SceneCtx &c, const uint32_t *pal, int n, float stepsPerSec)
{
    float base = c.t * stepsPerSec;
    for (int i = 0; i < NUM_LEDS; i++) {
        float p = base + i;
        int k = (int)floorf(p);
        float f = smooth(p - k);
        mixHex(c.out[i], pal[((k % n) + n) % n], pal[(((k + 1) % n) + n) % n], f);
    }
}
static const uint32_t PAL_RAINBOW[] = {0xFF0000, 0xFF00FF, 0xFFD700, 0x006400, 0x00FF00, 0x800080, 0xFF1493};
static const uint32_t PAL_BLUEWHITE[] = {0x0000FF, 0xFFFFFF};
static const uint32_t PAL_REDMAROON[] = {0xFF0000, 0x800000};
static const uint32_t PAL_PINKWHITE[] = {0xFF00FF, 0xFFFFFF};
static const uint32_t PAL_PINKPURPLE[] = {0xFF00FF, 0x4B0082};
static const uint32_t PAL_GREENBLUE[] = {0x00FF00, 0x0000FF};
static void scClassicRainbow(SceneCtx &c)
{
    palette(c, PAL_RAINBOW, 7, 6.67f);
}
static void scBlueWhite(SceneCtx &c)
{
    palette(c, PAL_BLUEWHITE, 2, 3);
}
static void scRedMaroon(SceneCtx &c)
{
    palette(c, PAL_REDMAROON, 2, 3);
}
static void scPinkWhite(SceneCtx &c)
{
    palette(c, PAL_PINKWHITE, 2, 3);
}
static void scPinkPurple(SceneCtx &c)
{
    palette(c, PAL_PINKPURPLE, 2, 3);
}
static void scGreenBlue(SceneCtx &c)
{
    palette(c, PAL_GREENBLUE, 2, 3);
}

// ------------------------------------------------------------------------------------
// Animated
// ------------------------------------------------------------------------------------
static void scRainbowWave(SceneCtx &c)
{
    for (int i = 0; i < NUM_LEDS; i++)
        hsv(c.out[i], c.t * 0.15f + (LEDS[i].x + LEDS[i].y) * 0.8f, 1, 1);
}
static void scBreathe(SceneCtx &c)
{
    float b = 0.08f + 0.92f * smooth(0.5f - 0.5f * cosf(c.t * TAU / 5));
    Rgb col;
    hsv(col, c.t / 60, 0.9f, b); // colour drifts once a minute
    fill(c, col[0], col[1], col[2]);
}
// a comet running along the keypad chain (a snake through the rows), logo and moon
static void scSnake(SceneCtx &c)
{
    float head = fractf(c.t * 0.35f) * NUM_LEDS;
    for (int i = 0; i < NUM_LEDS; i++) {
        float d = head - i;
        if (d < 0)
            d += NUM_LEDS;
        float k = d < 6 ? 1 - d / 6 : 0;
        hsv(c.out[i], c.t * 0.05f + d * 0.01f, 1 - k * 0.3f, k * k);
    }
}
// Cylon sweep across the keypad columns (logo and moon follow the same sweep)
static void scScanner(SceneCtx &c)
{
    float pos = 0.5f - 0.5f * cosf(c.t * TAU / 2.2f); // 0..1
    for (int i = 0; i < NUM_LEDS; i++) {
        float x = (LEDS[i].group == GROUP_KEYPAD) ? (LEDS[i].gx + 0.5f) / 1.5f : LEDS[i].x;
        float d = fabsf(x - pos);
        float k = clamp01(1 - d * 4);
        set(c.out[i], k * k, 0, 0);
    }
}
// green "digital rain" falling down the three key columns
static void scRain(SceneCtx &c)
{
    static float drop[4];
    static float speed[4];
    if (c.reset)
        for (int k = 0; k < 4; k++) {
            drop[k] = -frand() * 3;
            speed[k] = 1.2f + frand();
        }
    for (int k = 0; k < 4; k++) {
        drop[k] += c.dt * speed[k];
        if (drop[k] > 5) {
            drop[k] = -frand() * 2;
            speed[k] = 1.2f + frand();
        }
    }
    for (int i = 0; i < NUM_LEDS; i++) {
        const LedInfo &L = LEDS[i];
        int col = L.group == GROUP_KEYPAD ? (L.col < 0 ? 3 : L.col) : (i % 4);
        float row = L.group == GROUP_KEYPAD ? (float)L.row : L.y * 4;
        float d = drop[col] - row;
        float k = (d >= 0 && d < 2) ? 1 - d / 2 : 0;
        float head = (d >= 0 && d < 0.5f) ? 1 : 0;
        set(c.out[i], head * 0.6f, 0.04f + k, head * 0.4f);
    }
}
static void scFire(SceneCtx &c)
{
    for (int i = 0; i < NUM_LEDS; i++) {
        float h = heightOf(i);
        float n = noise1(c.t * 3 + i * 1.7f) * 0.6f + noise1(c.t * 7 + i * 3.1f) * 0.4f;
        heat(c.out[i], clamp01(1.1f - h * 1.4f + (n - 0.5f) * 0.9f));
    }
}
static void scPlasma(SceneCtx &c)
{
    for (int i = 0; i < NUM_LEDS; i++) {
        float x = LEDS[i].x * 6, y = LEDS[i].y * 6;
        float v = sinf(x + c.t) + sinf(y * 1.3f - c.t * 1.2f) + sinf((x + y) * 0.7f + c.t * 0.7f);
        hsv(c.out[i], v * 0.12f + c.t * 0.03f, 1, 0.8f + 0.2f * sinf(v));
    }
}
// night sky: deep blue with stars that twinkle in and out
static void scStarryNight(SceneCtx &c)
{
    static float star[NUM_LEDS];
    for (int i = 0; i < NUM_LEDS; i++) {
        star[i] -= c.dt * 0.7f;
        if (star[i] < 0 && frand() < c.dt * 0.25f)
            star[i] = 1;
        float s = star[i] > 0 ? sinf(star[i] * 3.14159f) : 0;
        set(c.out[i], 0.01f + s * 0.8f, 0.01f + s * 0.8f, 0.06f + s * 0.9f);
    }
}
static void scAurora(SceneCtx &c)
{
    for (int i = 0; i < NUM_LEDS; i++) {
        float x = LEDS[i].x;
        float band = noise1(x * 3 + c.t * 0.4f) * 0.6f + noise1(x * 7 - c.t * 0.7f) * 0.4f;
        float k = smooth((band - 0.3f) * 2.5f) * (0.4f + 0.6f * heightOf(i));
        hsv(c.out[i], 0.33f + band * 0.25f, 0.9f, 0.05f + k);
    }
}
static void scHeartbeat(SceneCtx &c)
{
    for (int i = 0; i < NUM_LEDS; i++) {
        float lag = LEDS[i].group == GROUP_KEYPAD ? LEDS[i].gy * 0.06f : 0;
        float pp = fractf((c.t - lag) / 1.1f);
        float bb = expf(-pp * 25) + 0.7f * expf(-fabsf(pp - 0.22f) * 30);
        set(c.out[i], 0.06f + clamp01(bb) * 0.94f, 0, clamp01(bb) * 0.08f);
    }
}
// keypad reacts to typing: every key press sends a ripple across the keys
static void scRipple(SceneCtx &c)
{
    static float rip[8][3]; // x, y, age
    static int next = 0;
    if (c.reset) {
        for (auto &r : rip)
            r[2] = 99;
        next = 0;
    }
    for (int p = 0; p < c.npressed; p++) {
        const LedInfo &L = LEDS[c.pressed[p]];
        rip[next][0] = L.x;
        rip[next][1] = L.y;
        rip[next][2] = 0;
        next = (next + 1) % 8;
    }
    for (auto &r : rip)
        r[2] += c.dt;
    for (int i = 0; i < NUM_LEDS; i++) {
        Rgb &o = c.out[i];
        hsv(o, 0.55f + 0.05f * sinf(c.t * 0.3f + i), 0.8f, 0.05f); // calm idle glow
        for (auto &r : rip) {
            if (r[2] > 2)
                continue;
            float d = hypotf(LEDS[i].x - r[0], LEDS[i].y - r[1]);
            float ring = r[2] * 0.6f;
            float k = clamp01(1 - fabsf(d - ring) * 10) * (1 - r[2] / 2);
            Rgb w;
            hsv(w, 0.5f + r[2] * 0.3f, 0.6f, k);
            add(o, w[0], w[1], w[2]);
        }
    }
}

// ------------------------------------------------------------------------------------
// Utah
// ------------------------------------------------------------------------------------
// red rock sunset: banded oranges and magentas, slowly sinking
static void scRedRocks(SceneCtx &c)
{
    static const uint32_t SKY[] = {0x2A0A4A, 0x8A1E5A, 0xE0402A, 0xFF8A1E, 0xFFC860};
    for (int i = 0; i < NUM_LEDS; i++) {
        float h = heightOf(i) * 4 + sinf(c.t * 0.35f) * 1.2f + noise1(c.t * 0.8f + i) * 0.7f;
        h = h < 0 ? 0 : (h > 3.999f ? 3.999f : h);
        int k = (int)h;
        mixHex(c.out[i], SKY[4 - k], SKY[3 - k], smooth(h - k));
    }
}
// summer desert shimmer: hot sand colours wobbling in the heat haze
static void scDesertHeat(SceneCtx &c)
{
    for (int i = 0; i < NUM_LEDS; i++) {
        float n = noise1(c.t * 1.3f + LEDS[i].x * 9) * 0.6f + noise1(c.t * 3.1f + LEDS[i].y * 13) * 0.4f;
        mixHex(c.out[i], 0xC24A10, 0xFFC15E, smooth(n), 0.5f + 0.5f * n);
    }
}
// monsoon storm over the canyon: dark blue rain and the odd lightning strike
static void scCanyonStorm(SceneCtx &c)
{
    static float flash = 0;
    static float flashX = 0.5f;
    if (flash <= 0 && frand() < c.dt * 0.25f) {
        flash = 1;
        flashX = frand();
    }
    flash -= c.dt * 3;
    for (int i = 0; i < NUM_LEDS; i++) {
        float rain = noise1(c.t * 4 + i * 5.3f + LEDS[i].y * 8);
        set(c.out[i], 0.0f, 0.02f + rain * 0.04f, 0.08f + rain * 0.15f);
        if (flash > 0) {
            float flick = (sinf(c.t * 60) > -0.3f) ? 1 : 0.3f;
            float k = clamp01(flash) * flick * clamp01(1 - fabsf(LEDS[i].x - flashX) * 1.5f);
            add(c.out[i], k, k, k * 1.1f);
        }
    }
}

// ------------------------------------------------------------------------------------
// Halloween (slow, ambient ones first)
// ------------------------------------------------------------------------------------
static void scCandlelight(SceneCtx &c)
{
    static float gutter[NUM_LEDS];
    for (int i = 0; i < NUM_LEDS; i++) {
        float n = noise1(c.t * 2.5f + i * 7.7f) * 0.5f + noise1(c.t * 9 + i * 3.3f) * 0.25f + 0.25f;
        if (gutter[i] <= 0 && frand() < c.dt * 0.03f)
            gutter[i] = 1;
        gutter[i] -= c.dt * 0.8f;
        float g = gutter[i] > 0 ? 1 - 0.6f * sinf(gutter[i] * 3.14159f) : 1;
        float b = (0.35f + 0.65f * n) * g;
        set(c.out[i], b, b * 0.42f, b * 0.06f);
    }
}
static void scJackOLantern(SceneCtx &c)
{
    float flick = 0.75f + 0.25f * noise1(c.t * 6);
    float squint = smooth(0.5f + 0.5f * sinf(c.t * 0.4f));
    for (int i = 0; i < NUM_LEDS; i++) {
        const LedInfo &L = LEDS[i];
        float n = noise1(c.t * 4 + i * 2.3f);
        if (L.group == GROUP_EYES) {
            set(c.out[i], flick * (0.5f + 0.5f * squint), flick * 0.35f * (0.4f + 0.6f * squint), 0);
        } else if (L.group == GROUP_LOGO) {
            set(c.out[i], 0.05f, 0.35f * flick, 0.02f); // green stem
        } else if (L.group == GROUP_KEYPAD && L.row == 3) {
            set(c.out[i], flick, 0.45f * flick, 0.02f); // jagged mouth
        } else {
            float b = (0.2f + 0.4f * n) * flick;
            set(c.out[i], b, b * 0.35f, 0); // glowing pumpkin flesh
        }
    }
}
// pairs of eyes opening in a dim violet dark
static void scLurkingEyes(SceneCtx &c)
{
    struct Eye {
        int a, b;
        float age, life;
        uint32_t col;
    };
    static Eye eyes[3];
    // pairs that sit side by side
    static const int8_t PAIRS[][2] = {{2, 1}, {1, 0}, {3, 4}, {4, 5}, {8, 7}, {7, 6}, {9, 10}, {10, 11}, {13, 14}, {15, 16}};
    static const uint32_t COLS[] = {0xFFB000, 0x40FF20, 0xB040FF, 0xFF2000};
    if (c.reset)
        for (auto &e : eyes)
            e.age = e.life = 0;
    for (int i = 0; i < NUM_LEDS; i++)
        set(c.out[i], 0.03f, 0, 0.05f);
    for (auto &e : eyes) {
        e.age += c.dt;
        if (e.age >= e.life) {
            if (frand() < c.dt * 0.6f) {
                int p = (int)(frand() * 10);
                e.a = PAIRS[p][0];
                e.b = PAIRS[p][1];
                e.age = 0;
                e.life = 3 + frand() * 4;
                e.col = COLS[(int)(frand() * 4)];
            }
            continue;
        }
        float open = smooth(e.age / 0.8f) * smooth((e.life - e.age) / 0.8f);
        float blink = fmodf(e.age, 2.3f) < 0.15f ? 0.05f : 1; // lazy blink
        Rgb col;
        setHex(col, e.col, open * blink);
        set(c.out[e.a], col[0], col[1], col[2]);
        set(c.out[e.b], col[0], col[1], col[2]);
    }
}
// pale wisps drifting up the badge
static void scGhosts(SceneCtx &c)
{
    for (int i = 0; i < NUM_LEDS; i++) {
        float h = heightOf(i);
        float w = noise1(h * 4 - c.t * 0.8f + LEDS[i].x * 3);
        float k = smooth((w - 0.45f) * 3);
        set(c.out[i], 0.01f + k * 0.55f, 0.02f + k * 0.8f, 0.03f + k * 0.65f);
    }
}
// deep red glow with an eclipse shadow creeping across
static void scBloodMoon(SceneCtx &c)
{
    float shadow = fractf(c.t / 14) * 1.6f - 0.3f;
    for (int i = 0; i < NUM_LEDS; i++) {
        float d = LEDS[i].x - shadow;
        float k = 0.25f + 0.75f * smooth(fabsf(d) * 4);
        float pulse = 0.85f + 0.15f * sinf(c.t * 1.3f + i);
        set(c.out[i], 0.75f * k * pulse, 0.03f * k, 0.02f * k);
    }
}
// purple glow with green bubbles rising and popping
static void scCauldron(SceneCtx &c)
{
    for (int i = 0; i < NUM_LEDS; i++) {
        float h = heightOf(i);
        set(c.out[i], 0.25f * (1 - h) + 0.03f, 0, 0.35f * (1 - h) + 0.05f);
    }
    // bubbles live in the keypad columns: start at the bottom row and climb
    static float by[3] = {9, 9, 9};
    for (int k = 0; k < 3; k++) {
        if (by[k] > 4 && frand() < c.dt * 0.7f)
            by[k] = -0.5f;
        by[k] += c.dt * 1.5f;
    }
    for (int i = 0; i < KEYPAD_LEDS; i++) {
        const LedInfo &L = LEDS[i];
        if (L.col < 0)
            continue;
        float rowUp = 3 - L.row; // 0 bottom .. 3 top
        float d = fabsf(by[L.col] - rowUp);
        float k = clamp01(1 - d * 1.5f);
        if (by[L.col] > 3.2f && L.row == 0)
            k = clamp01((4 - by[L.col]) * 1.5f) * 1.4f; // pop
        add(c.out[i], k * 0.25f, k * 0.9f, k * 0.1f);
    }
}
// cold violet fog rolling slowly, a pale green spirit drifting through
static void scGraveyardMist(SceneCtx &c)
{
    float sx = 0.5f + 0.45f * sinf(c.t * 0.21f), sy = 0.5f + 0.4f * sinf(c.t * 0.13f + 1);
    for (int i = 0; i < NUM_LEDS; i++) {
        float fog = noise1(LEDS[i].x * 5 + c.t * 0.25f) * 0.5f + noise1(LEDS[i].y * 6 - c.t * 0.18f) * 0.5f;
        set(c.out[i], 0.1f * fog, 0.06f * fog, 0.28f * fog + 0.02f);
        float d = hypotf(LEDS[i].x - sx, LEDS[i].y - sy);
        float k = clamp01(1 - d * 5);
        add(c.out[i], k * 0.3f, k * 0.6f, k * 0.35f);
    }
}
static void scEmbers(SceneCtx &c)
{
    static float flare[NUM_LEDS];
    for (int i = 0; i < NUM_LEDS; i++) {
        float n = noise1(c.t * 0.5f + i * 4.1f);
        if (flare[i] <= 0 && frand() < c.dt * 0.05f)
            flare[i] = 1;
        flare[i] -= c.dt * 0.6f;
        float f = flare[i] > 0 ? sinf(flare[i] * 3.14159f) : 0;
        heat(c.out[i], 0.12f + n * 0.18f + f * 0.45f);
    }
}
// one soft light wandering the badge
static void scWillOWisp(SceneCtx &c)
{
    float wx = noise1(c.t * 0.3f) * 1.1f - 0.05f, wy = noise1(c.t * 0.23f + 50) * 1.1f - 0.05f;
    float fade = 0.6f + 0.4f * sinf(c.t * 0.9f);
    for (int i = 0; i < NUM_LEDS; i++) {
        float d = hypotf(LEDS[i].x - wx, LEDS[i].y - wy);
        float k = clamp01(1 - d * 2.2f) * fade;
        set(c.out[i], k * k * 0.6f, 0.02f + k * 0.95f, k * k * 0.5f + 0.02f);
    }
}
static void scCandyCorn(SceneCtx &c)
{
    static const uint32_t BAND[] = {0xFFE040, 0xFF7A00, 0xFFFFFF};
    for (int i = 0; i < NUM_LEDS; i++) {
        float p = LEDS[i].y * 6 - c.t * 0.5f;
        int k = (int)floorf(p);
        float f = smooth((p - k - 0.7f) * 3.3f);
        mixHex(c.out[i], BAND[((k % 3) + 3) % 3], BAND[(((k + 1) % 3) + 3) % 3], f, 0.8f);
    }
}
// slow red breathing, then without warning a convulsion in purple and green
static void scPossessed(SceneCtx &c)
{
    static float fit = 0;
    if (fit <= 0 && frand() < c.dt * 0.08f)
        fit = 1.6f;
    fit -= c.dt;
    for (int i = 0; i < NUM_LEDS; i++) {
        if (fit > 0) {
            bool on = hash1(floorf(c.t * 18) + i * 3.7f) > 0.45f;
            if (on)
                (hash1(i + floorf(c.t * 9)) > 0.5f) ? set(c.out[i], 0.6f, 0, 1) : set(c.out[i], 0.2f, 1, 0);
            else
                set(c.out[i], 0, 0, 0);
        } else {
            float b = 0.1f + 0.5f * smooth(0.5f - 0.5f * cosf(c.t * TAU / 6));
            set(c.out[i], b, 0, 0);
        }
    }
}
// skeleton cowboy: bone white flicker on the keys, red eyes under the hat
static void scDeadMansHand(SceneCtx &c)
{
    for (int i = 0; i < NUM_LEDS; i++) {
        const LedInfo &L = LEDS[i];
        if (L.group == GROUP_EYES) {
            float b = 0.6f + 0.4f * noise1(c.t * 2 + i);
            set(c.out[i], b, 0, 0);
        } else {
            float n = noise1(c.t * 1.5f + i * 3.3f);
            float b = 0.1f + 0.35f * n;
            set(c.out[i], b, b * 0.95f, b * 0.8f);
        }
    }
}
// a crow on the sign: darkness, the odd glint, and its eyes catching the moonlight
static void scNevermore(SceneCtx &c)
{
    static float glint = 0;
    static int gi = 0;
    if (glint <= 0 && frand() < c.dt * 0.4f) {
        glint = 1;
        gi = (int)(frand() * NUM_LEDS);
    }
    glint -= c.dt * 1.5f;
    for (int i = 0; i < NUM_LEDS; i++) {
        set(c.out[i], 0.01f, 0.0f, 0.03f);
        if (LEDS[i].group == GROUP_MOON)
            set(c.out[i], 0.25f, 0.25f, 0.3f);
        if (LEDS[i].group == GROUP_EYES) {
            float blink = fmodf(c.t, 4.1f) < 0.12f ? 0 : 1;
            set(c.out[i], 0.5f * blink, 0.35f * blink, 0);
        }
    }
    if (glint > 0)
        add(c.out[gi], glint * 0.4f, glint * 0.4f, glint * 0.6f);
}
// the shadow of something walking past the keypad
static void scShadowWalker(SceneCtx &c)
{
    float pos = fractf(c.t / 9) * 1.8f - 0.4f;
    for (int i = 0; i < NUM_LEDS; i++) {
        float d = fabsf(LEDS[i].x - pos);
        float lit = smooth(d * 6);
        float n = noise1(c.t * 3 + i);
        set(c.out[i], (0.35f + 0.1f * n) * lit, (0.15f + 0.05f * n) * lit, 0.05f * lit);
        if (d < 0.08f && LEDS[i].group == GROUP_EYES)
            set(c.out[i], 1, 0.1f, 0);
    }
}

// ------------------------------------------------------------------------------------
// App colour: follows the Ambient Lighting colour set in the Meshtastic phone app
// ------------------------------------------------------------------------------------
static float appCol[3] = {1.0f, 0.4f, 0.0f};
void sceneSetAppColour(float r, float g, float b)
{
    if (r + g + b > 0.01f) { // all-zero means "not set": keep the default
        appCol[0] = r;
        appCol[1] = g;
        appCol[2] = b;
    }
}
static void scAppColour(SceneCtx &c)
{
    float b = 0.55f + 0.45f * sinf(c.t * TAU / 6);
    fill(c, appCol[0] * b, appCol[1] * b, appCol[2] * b);
}
static void scAppShimmer(SceneCtx &c)
{
    for (int i = 0; i < NUM_LEDS; i++) {
        float n = 0.35f + 0.65f * noise1(c.t * 1.7f + i * 2.9f);
        set(c.out[i], appCol[0] * n, appCol[1] * n, appCol[2] * n);
    }
}

static void scOff(SceneCtx &c)
{
    fill(c, 0, 0, 0);
}

// ------------------------------------------------------------------------------------
// Overlays
// ------------------------------------------------------------------------------------
void overlayKeyFlash(SceneCtx &c, const float level[KEYPAD_LEDS])
{
    for (int i = 0; i < KEYPAD_LEDS; i++) {
        float k = level[i];
        if (k <= 0)
            continue;
        for (int j = 0; j < 3; j++)
            c.out[i][j] = mixf(c.out[i][j], 1.0f, k);
    }
}
void overlayAlert(SceneCtx &c, bool dm, float in)
{
    // direct message: three cyan pulses rolling down the keys; channel message: one soft green
    int pulses = dm ? 3 : 1;
    float period = 0.6f;
    if (in >= pulses * period)
        return;
    float p = fmodf(in, period) / period;
    for (int i = 0; i < KEYPAD_LEDS; i++) {
        float row = LEDS[i].gy; // 0 top .. 1 bottom
        float k = clamp01(1 - fabsf(p * 1.6f - 0.3f - row) * 3);
        if (dm)
            for (int j = 0; j < 3; j++)
                c.out[i][j] = mixf(c.out[i][j], j == 0 ? 0.0f : 1.0f, k);
        else
            for (int j = 0; j < 3; j++)
                c.out[i][j] = mixf(c.out[i][j], j == 1 ? 0.8f : 0.1f, k * 0.7f);
    }
}
void overlayPulse(SceneCtx &c, float level, bool moon)
{
    float k = clamp01(level);
    for (int i = FIRST_LOGO; i < NUM_LEDS; i++) {
        if (LEDS[i].group == GROUP_EYES || (moon && LEDS[i].group == GROUP_MOON))
            for (int j = 0; j < 3; j++)
                c.out[i][j] = mixf(c.out[i][j], 1.0f, k * 0.8f);
    }
}

void overlayCelebrate(SceneCtx &c, float left)
{
    float fade = clamp01(left);
    for (int i = 0; i < NUM_LEDS; i++) {
        Rgb col;
        bool strobe = fmodf(c.t * 8 + i * 0.37f, 1) < 0.5f;
        hsv(col, c.t * 1.5f + i * 0.13f, 1, strobe ? 1 : 0.2f);
        for (int j = 0; j < 3; j++)
            c.out[i][j] = mixf(c.out[i][j], col[j], fade);
    }
}

// ------------------------------------------------------------------------------------
// Scene table (names show in the badge menu; keep them short, upper case)
// ------------------------------------------------------------------------------------
static const Scene SCENES[] = {
    // the 2025 patterns (same colours as before, now a smooth chase)
    {"RAINBOW", scClassicRainbow},
    {"BLUE + WHITE", scBlueWhite},
    {"RED + MAROON", scRedMaroon},
    {"PINK + WHITE", scPinkWhite},
    {"PINK + PURPLE", scPinkPurple},
    {"GREEN + BLUE", scGreenBlue},
    // animated
    {"RAINBOW WAVE", scRainbowWave},
    {"BREATHE", scBreathe},
    {"SNAKE", scSnake},
    {"SCANNER", scScanner},
    {"DIGITAL RAIN", scRain},
    {"FIRE", scFire},
    {"PLASMA", scPlasma},
    {"STARRY NIGHT", scStarryNight},
    {"AURORA", scAurora},
    {"HEARTBEAT", scHeartbeat},
    {"TYPE RIPPLE", scRipple},
    // Utah
    {"RED ROCKS SUNSET", scRedRocks},
    {"DESERT HEAT", scDesertHeat},
    {"CANYON STORM", scCanyonStorm},
    // Halloween
    {"CANDLELIGHT", scCandlelight},
    {"JACK-O-LANTERN", scJackOLantern},
    {"LURKING EYES", scLurkingEyes},
    {"GHOSTS", scGhosts},
    {"BLOOD MOON", scBloodMoon},
    {"CAULDRON", scCauldron},
    {"GRAVEYARD MIST", scGraveyardMist},
    {"EMBERS", scEmbers},
    {"WILL-O-WISP", scWillOWisp},
    {"CANDY CORN", scCandyCorn},
    {"POSSESSED", scPossessed},
    {"DEAD MAN'S HAND", scDeadMansHand},
    {"NEVERMORE", scNevermore},
    {"SHADOW WALKER", scShadowWalker},
    // colour from the phone app (Meshtastic: Module settings > Ambient Lighting)
    {"APP COLOUR", scAppColour},
    {"APP SHIMMER", scAppShimmer},
    {"OFF", scOff},
};

int sceneCount()
{
    return (int)(sizeof(SCENES) / sizeof(SCENES[0]));
}
const Scene &sceneAt(int i)
{
    if (i < 0 || i >= sceneCount())
        i = 0;
    return SCENES[i];
}
int sceneFind(const char *name)
{
    if (!name)
        return -1;
    for (int i = 0; i < sceneCount(); i++)
        if (strcmp(SCENES[i].name, name) == 0)
            return i;
    return -1;
}
int sceneFromLegacyIndex(int idx)
{
    static const char *OLD[] = {"RAINBOW", "BLUE + WHITE", "RED + MAROON", "PINK + WHITE", "PINK + PURPLE", "GREEN + BLUE", "OFF"};
    if (idx < 0 || idx > 6)
        return 0;
    int s = sceneFind(OLD[idx]);
    return s < 0 ? 0 : s;
}

} // namespace badge

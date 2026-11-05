// LED scenes for the Utah BSides badge. See BadgeScenes.h.
//
// A scene is a colour palette plus one effect for each part of the badge:
//   keypad (13 LEDs)  logo (the 4 LEDs on the B sign)  eyes (2)  moon (6)
// so every part moves in its own way while the colours stay together. Key presses always
// flash the key in the palette's highlight colour (BadgeLeds.cpp), on top of any scene.
//
// Adding a scene: add a line to SCENES[] at the bottom (name, palette, keypad, logo, eyes,
// moon effect). Adding a palette: add 4 colours to PALETTES[] (base, accent, highlight, dark).
// Adding an effect: write a function like the ones below and add it to its table.
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
static inline void scale(Rgb &o, const Rgb &c, float k)
{
    o[0] = c[0] * k;
    o[1] = c[1] * k;
    o[2] = c[2] * k;
}
static inline void add(Rgb &o, const Rgb &c, float k)
{
    o[0] += c[0] * k;
    o[1] += c[1] * k;
    o[2] += c[2] * k;
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
static float noise1(float x) // smooth value noise, 0..1
{
    float i = floorf(x), f = x - i;
    return mixf(hash1(i), hash1(i + 1), smooth(f));
}

// ------------------------------------------------------------------------------------
// Palettes: base, accent, highlight, dark
// ------------------------------------------------------------------------------------
struct Palette {
    uint32_t c[4];
};
enum PalId : uint8_t {
    P_RAINBOW, // special: hue wheel
    P_APP,     // special: built from the colour set in the phone app
    P_BLUEWHITE,
    P_REDMAROON,
    P_PINKWHITE,
    P_PINKPURPLE,
    P_GREENBLUE,
    P_SUNSET,
    P_DESERT,
    P_SYNTH,
    P_NEON,
    P_OCEAN,
    P_ICE,
    P_FIRE,
    P_LAVA,
    P_TOXIC,
    P_AURORA,
    P_FOREST,
    P_CANDY,
    P_VAPOR,
    P_GOLD,
    P_MATRIX,
    P_HALLOWEEN,
    P_PUMPKIN,
    P_BLOOD,
    P_GHOST,
    P_CAULDRON,
    P_CANDYCORN,
    P_SKINWALKER,
    P_COYOTE,
    P_CAMPFIRE,
    P_SALOON,
    P_SHERIFF,
    P_WANTED,
    P_DUST,
    P_RATTLER,
    P_BONE,
    P_THUNDER,
    P_GHOSTTOWN,
    P_MINE,
    P_NOON,
    P_COUNT
};
static Palette PALETTES[P_COUNT] = {
    {{0xFF0000, 0x00FF00, 0x0000FF, 0x101010}},  // rainbow (unused, hue wheel)
    {{0xFF6000, 0xFFA040, 0xFFFFFF, 0x201000}},  // app colour (filled in at run time)
    {{0x0030FF, 0xFFFFFF, 0x80C0FF, 0x000818}},  // blue + white
    {{0xFF0000, 0x800000, 0xFF4020, 0x100000}},  // red + maroon
    {{0xFF00C0, 0xFFFFFF, 0xFF80E0, 0x180010}},  // pink + white
    {{0xFF00C0, 0x4B0082, 0xFF60FF, 0x0C0018}},  // pink + purple
    {{0x00FF40, 0x0040FF, 0x80FFC0, 0x000C10}},  // green + blue
    {{0xFF6010, 0xC01060, 0xFFC040, 0x200828}},  // red rocks sunset
    {{0x1030A0, 0x10A0A0, 0xFFD890, 0x020414}},  // desert night
    {{0xFF00A0, 0x6020FF, 0x00E0FF, 0x10001C}},  // synthwave
    {{0xFF0080, 0x00FFFF, 0xA000FF, 0x080010}},  // neon
    {{0x0040FF, 0x00C0C0, 0x80FFE0, 0x000818}},  // ocean
    {{0x80E0FF, 0x2060FF, 0xFFFFFF, 0x000818}},  // ice
    {{0xFF2000, 0xFF8000, 0xFFE040, 0x180000}},  // fire
    {{0xC00000, 0xFF5000, 0xFFC000, 0x100000}},  // lava
    {{0x60FF00, 0x00C020, 0xE0FF40, 0x041000}},  // toxic
    {{0x00FF60, 0x00A0A0, 0xC040FF, 0x000C10}},  // aurora
    {{0x10A020, 0x80A000, 0xFFC040, 0x020800}},  // forest
    {{0xFF60B0, 0x60FFC0, 0xFFF080, 0x180818}},  // candy
    {{0xFF70C0, 0x9070FF, 0x60F0FF, 0x100818}},  // vaporwave
    {{0xFFB000, 0xFFFFFF, 0xFFD860, 0x140800}},  // BSides gold
    {{0x00FF30, 0x008010, 0xC0FFC0, 0x000800}},  // matrix
    {{0xFF6000, 0x8000FF, 0x40FF00, 0x0C0010}},  // halloween
    {{0xFF5000, 0xFF9000, 0xFFD040, 0x100400}},  // jack-o-lantern
    {{0xC00000, 0x600000, 0xFF2010, 0x080000}},  // blood moon
    {{0x90FFC0, 0xC0C0FF, 0xFFFFFF, 0x040810}},  // ghost
    {{0x8000FF, 0x40FF20, 0xC060FF, 0x080010}},  // cauldron
    {{0xFFD000, 0xFF6000, 0xFFFFFF, 0x140800}},  // candy corn
    // Old West
    {{0x801010, 0x301040, 0xFF2008, 0x060104}},  // skinwalker: dried blood, bruise, glowing eyes
    {{0x2030A0, 0x6020A0, 0xFFF0C0, 0x02020C}},  // coyote moon: dusk indigo, pale moon
    {{0xFF5000, 0xFF9020, 0xFFE0A0, 0x140400}},  // campfire
    {{0xC06010, 0x900020, 0xFFC860, 0x100400}},  // saloon: whiskey, red velvet, brass bulbs
    {{0xFFA800, 0xA0A0B8, 0xFFFFE0, 0x0C0600}},  // sheriff's star: gold, silver, glint
    {{0xE0A050, 0x804010, 0xFFE0B0, 0x100600}},  // wanted poster: parchment, sepia
    {{0xC08040, 0xA04010, 0xFFD090, 0x100800}},  // dust: tan, rust, sand
    {{0x80A020, 0xA07830, 0xFFE070, 0x040600}},  // rattlesnake: olive, tan diamonds
    {{0xE0D8C0, 0x505868, 0xFFFFFF, 0x040404}},  // bone orchard: bone, ash
    {{0x0050C0, 0x00B0A0, 0xFFFFFF, 0x02040C}},  // thunderbird: storm blue, turquoise, lightning
    {{0x50A080, 0x705030, 0xC0FFE0, 0x020604}},  // ghost town: haunt green, weathered wood
    {{0xFF9020, 0x506078, 0xFFD080, 0x060406}},  // silver mine: lantern, rock, ore glint
    {{0xFFC000, 0x40A0FF, 0xFFFFE0, 0x201000}},  // high noon: sun, sky
};

struct Pal {
    Rgb c[4];
    bool rainbow;
};
static void loadPal(int id, Pal &p)
{
    p.rainbow = id == P_RAINBOW;
    for (int i = 0; i < 4; i++) {
        uint32_t v = PALETTES[id].c[i];
        set(p.c[i], ((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f);
    }
}
// colour f (any number; wraps) along base -> accent -> highlight -> accent -> base
static void palAt(const Pal &p, float f, Rgb &o)
{
    if (p.rainbow) {
        hsv(o, f, 1, 1);
        return;
    }
    static const int8_t order[4] = {0, 1, 2, 1};
    f = fractf(f) * 4;
    int i = (int)f;
    float k = smooth(f - i);
    const Rgb &a = p.c[order[i & 3]], &b = p.c[order[(i + 1) & 3]];
    for (int j = 0; j < 3; j++)
        o[j] = mixf(a[j], b[j], k);
}

// ------------------------------------------------------------------------------------
// Zones
// ------------------------------------------------------------------------------------
struct Zone {
    int first, count;
};
static const Zone Z_KEYPAD = {0, KEYPAD_LEDS};
static const Zone Z_EYES = {FIRST_LOGO, 2};
static const Zone Z_LOGO = {FIRST_LOGO + 2, 4};
static const Zone Z_MOON = {FIRST_MOON, MOON_LEDS};
static const Zone Z_SKULL = {SKULL_LED, 1}; // the skull left of *: its own glow, like the eyes

struct Fx {
    SceneCtx &c;
    const Pal &p;
    Zone z;
    float t;
};
typedef void (*FxFn)(Fx &f);

// per-LED scratch state for the effects that need memory (sparkles, drops, ...)
static float mem[NUM_LEDS];
static float mem2[NUM_LEDS];

// moon LED x position across the moon, 0 (left) .. 1 (right)
static inline float moonX(int i)
{
    return clamp01((LEDS[i].x - 0.30f) / 0.27f);
}

// ------------------------------------------------------------------------------------
// Keypad effects
// ------------------------------------------------------------------------------------
static void kWave(Fx &f) // the palette flowing diagonally across the keys
{
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        Rgb col;
        palAt(f.p, f.t * 0.12f + (LEDS[i].gx * 0.5f + LEDS[i].gy * 0.8f) * 0.5f, col);
        scale(f.c.out[i], col, 0.75f + 0.25f * sinf(f.t * 1.3f + i));
    }
}
static void kRain(Fx &f) // drops falling down the three columns
{
    static float drop[4], speed[4];
    if (f.c.reset)
        for (int k = 0; k < 4; k++)
            drop[k] = -frand() * 3, speed[k] = 1.2f + frand();
    for (int k = 0; k < 4; k++) {
        drop[k] += f.c.dt * speed[k];
        if (drop[k] > 5.5f)
            drop[k] = -frand() * 2, speed[k] = 1.2f + frand();
    }
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        int col = LEDS[i].col < 0 ? 3 : LEDS[i].col;
        float d = drop[col] - LEDS[i].row;
        float k = (d >= 0 && d < 2.2f) ? 1 - d / 2.2f : 0;
        Rgb trail;
        palAt(f.p, 0.15f + col * 0.12f, trail);
        scale(f.c.out[i], f.p.c[3], 0.6f);
        add(f.c.out[i], trail, k);
        if (d >= 0 && d < 0.5f)
            add(f.c.out[i], f.p.c[2], 0.8f);
    }
}
static void kSparkle(Fx &f) // dim glow with keys twinkling on and off
{
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        mem[i] -= f.c.dt * 0.9f;
        if (mem[i] < -0.2f && frand() < f.c.dt * 0.35f)
            mem[i] = 1, mem2[i] = frand();
        Rgb base, sp;
        palAt(f.p, f.t * 0.04f + i * 0.05f, base);
        scale(f.c.out[i], base, 0.12f);
        if (mem[i] > 0) {
            palAt(f.p, mem2[i], sp);
            add(f.c.out[i], sp, sinf(mem[i] * 3.14159f));
        }
    }
}
static void kFire(Fx &f) // flames rising from the bottom row
{
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        float h = 1 - LEDS[i].gy; // 0 bottom .. 1 top
        float n = noise1(f.t * 3 + i * 1.7f) * 0.6f + noise1(f.t * 7 + i * 3.1f) * 0.4f;
        float heat = clamp01(1.05f - h * 1.2f + (n - 0.5f) * 0.9f);
        Rgb a;
        if (heat < 0.33f)
            for (int j = 0; j < 3; j++)
                a[j] = mixf(f.p.c[3][j], f.p.c[0][j], heat * 3);
        else if (heat < 0.66f)
            for (int j = 0; j < 3; j++)
                a[j] = mixf(f.p.c[0][j], f.p.c[1][j], (heat - 0.33f) * 3);
        else
            for (int j = 0; j < 3; j++)
                a[j] = mixf(f.p.c[1][j], f.p.c[2][j], (heat - 0.66f) * 3);
        scale(f.c.out[i], a, 0.3f + 0.7f * heat);
    }
}
static void kSnake(Fx &f) // a glowing snake winding through the keys (they are wired in a zigzag)
{
    float head = fractf(f.t * 0.45f) * (f.z.count + 6);
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        float d = head - (i - f.z.first);
        float k = (d >= 0 && d < 6) ? 1 - d / 6 : 0;
        Rgb col;
        palAt(f.p, f.t * 0.05f + d * 0.06f, col);
        scale(f.c.out[i], f.p.c[3], 0.5f);
        add(f.c.out[i], col, k * k);
    }
}
static void kRipple(Fx &f) // rings spreading from keys: random ones, and every key you press
{
    static float rx[6], ry[6], age[6];
    static int nextR = 0;
    static float timer = 0;
    if (f.c.reset)
        for (int r = 0; r < 6; r++)
            age[r] = 99;
    timer -= f.c.dt;
    auto spawn = [&](int led) {
        rx[nextR] = LEDS[led].gx;
        ry[nextR] = LEDS[led].gy * 1.5f;
        age[nextR] = 0;
        nextR = (nextR + 1) % 6;
    };
    for (int k = 0; k < f.c.npressed; k++)
        spawn(f.c.pressed[k]);
    if (timer <= 0) {
        timer = 1.2f + frand() * 1.5f;
        spawn((int)(frand() * 12));
    }
    for (int r = 0; r < 6; r++)
        age[r] += f.c.dt;
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        Rgb base;
        palAt(f.p, f.t * 0.03f, base);
        scale(f.c.out[i], base, 0.08f);
        for (int r = 0; r < 6; r++) {
            if (age[r] > 2)
                continue;
            float d = hypotf(LEDS[i].gx - rx[r], LEDS[i].gy * 1.5f - ry[r]);
            float k = clamp01(1 - fabsf(d - age[r] * 1.1f) * 3) * (1 - age[r] / 2);
            Rgb col;
            palAt(f.p, 0.25f + age[r] * 0.3f, col);
            add(f.c.out[i], col, k);
        }
    }
}
static void kScan(Fx &f) // a bar sweeping across the columns, changing colour each pass
{
    float s = f.t * 0.55f;
    float pos = 0.5f - 0.5f * cosf(s * TAU); // 0..1..0
    Rgb col;
    palAt(f.p, floorf(s * 2) * 0.25f, col);
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        float x = (LEDS[i].gx + 0.5f) / 1.5f;
        float k = clamp01(1 - fabsf(x - pos) * 3.5f);
        scale(f.c.out[i], f.p.c[3], 0.5f);
        add(f.c.out[i], col, k * k);
    }
}
static void kBreathe(Fx &f) // rows breathing in turn, each its own colour
{
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        float row = LEDS[i].gy;
        float b = 0.15f + 0.85f * smooth(0.5f + 0.5f * sinf(f.t * 1.4f - row * 3));
        Rgb col;
        palAt(f.p, row * 0.5f + f.t * 0.02f, col);
        scale(f.c.out[i], col, b);
    }
}
static void kPlasma(Fx &f) // swirling plasma
{
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        float x = LEDS[i].gx * 3, y = LEDS[i].gy * 4;
        float v = sinf(x + f.t) + sinf(y * 1.3f - f.t * 1.2f) + sinf((x + y) * 0.7f + f.t * 0.7f);
        Rgb col;
        palAt(f.p, v * 0.15f + f.t * 0.03f, col);
        scale(f.c.out[i], col, 0.7f + 0.3f * sinf(v));
    }
}
static void kChecker(Fx &f) // checkerboard trading colours
{
    float ph = smooth(0.5f + 0.5f * sinf(f.t * 1.6f));
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        bool odd = ((LEDS[i].col + LEDS[i].row) & 1) != 0;
        float k = odd ? ph : 1 - ph;
        for (int j = 0; j < 3; j++)
            f.c.out[i][j] = mixf(f.p.c[0][j], f.p.c[1][j], k);
    }
}
static void kSpiral(Fx &f) // a beam turning around the middle of the keypad
{
    float a0 = f.t * 1.4f;
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        float dx = LEDS[i].gx - 0.5f, dy = (LEDS[i].gy - 0.5f) * 1.5f;
        float a = atan2f(dy, dx);
        float d = fmodf(a - a0, TAU);
        if (d < 0)
            d += TAU;
        float k = clamp01(1 - d / 2.2f);
        Rgb col;
        palAt(f.p, a / TAU + f.t * 0.05f, col);
        scale(f.c.out[i], f.p.c[3], 0.5f);
        add(f.c.out[i], col, k * k);
    }
}
static void kEq(Fx &f) // equaliser bars bouncing in each column
{
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        int col = LEDS[i].col < 0 ? 3 : LEDS[i].col;
        float level = 0.15f + 0.85f * noise1(f.t * 2.2f + col * 13.7f);
        float h = 1 - LEDS[i].gy; // 0 bottom .. 1 top
        float k = clamp01((level - h) * 4);
        Rgb col2;
        palAt(f.p, h * 0.5f, col2);
        scale(f.c.out[i], f.p.c[3], 0.5f);
        add(f.c.out[i], col2, k);
    }
}

// ------------------------------------------------------------------------------------
// Logo effects (the 4 LEDs on the B sign)
// ------------------------------------------------------------------------------------
static void lFlow(Fx &f)
{
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        Rgb col;
        palAt(f.p, f.t * 0.18f + LEDS[i].gx * 0.4f, col);
        scale(f.c.out[i], col, 1);
    }
}
static void lChase(Fx &f) // marquee lights running round the sign
{
    float pos = fractf(f.t * 0.9f) * f.z.count;
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        float d = pos - (i - f.z.first);
        if (d < 0)
            d += f.z.count;
        float k = clamp01(1 - d / 1.6f);
        scale(f.c.out[i], f.p.c[0], 0.2f);
        add(f.c.out[i], f.p.c[2], k);
    }
}
static void lNeon(Fx &f) // an old neon sign: steady, with the odd flicker and dropout
{
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        mem[i] -= f.c.dt;
        if (mem[i] < -1 && frand() < f.c.dt * 0.15f)
            mem[i] = 0.5f + frand() * 0.6f;
        float on = 1;
        if (mem[i] > 0)
            on = hash1(floorf(f.t * 25) + i) > 0.5f ? 1 : 0.1f;
        scale(f.c.out[i], f.p.c[0], on);
        add(f.c.out[i], f.p.c[2], 0.15f * on);
    }
}
static void lBreathe(Fx &f)
{
    float b = 0.1f + 0.9f * smooth(0.5f + 0.5f * sinf(f.t * TAU / 4.5f));
    for (int i = f.z.first; i < f.z.first + f.z.count; i++)
        scale(f.c.out[i], f.p.c[1], b);
}
static void lSpark(Fx &f) // pops of colour, like fireflies
{
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        mem[i] -= f.c.dt * 1.3f;
        if (mem[i] < -0.3f && frand() < f.c.dt * 0.6f)
            mem[i] = 1, mem2[i] = frand();
        scale(f.c.out[i], f.p.c[3], 1);
        if (mem[i] > 0) {
            Rgb col;
            palAt(f.p, mem2[i], col);
            add(f.c.out[i], col, sinf(mem[i] * 3.14159f));
        }
    }
}
static void lHeart(Fx &f) // heartbeat
{
    float p = fractf(f.t / 1.1f);
    float b = clamp01(expf(-p * 25) + 0.7f * expf(-fabsf(p - 0.22f) * 30));
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        scale(f.c.out[i], f.p.c[3], 1);
        add(f.c.out[i], f.p.c[0], b);
    }
}
static void lStep(Fx &f) // the whole sign stepping through the palette
{
    float s = f.t / 1.6f;
    int k = (int)floorf(s);
    float m = smooth((s - k) * 3);
    Rgb a, b;
    palAt(f.p, k * 0.25f, a);
    palAt(f.p, (k + 1) * 0.25f, b);
    for (int i = f.z.first; i < f.z.first + f.z.count; i++)
        for (int j = 0; j < 3; j++)
            f.c.out[i][j] = mixf(a[j], b[j], m);
}

// ------------------------------------------------------------------------------------
// Eye effects (the pair of LEDs)
// ------------------------------------------------------------------------------------
static float blinkAt(float t, float every, float seed) // 0 while blinking, else 1
{
    float p = fmodf(t + seed, every);
    return p < 0.13f ? 0.03f : 1;
}
static void eGlow(Fx &f) // steady glow, slow pulse, a blink every few seconds
{
    float b = (0.75f + 0.25f * sinf(f.t * 1.2f)) * blinkAt(f.t, 4.7f, 0);
    for (int i = f.z.first; i < f.z.first + f.z.count; i++)
        scale(f.c.out[i], f.p.c[0], b);
}
static void eLook(Fx &f) // glancing left and right
{
    float look = sinf(f.t * 0.7f) + 0.4f * sinf(f.t * 1.9f);
    float bl = blinkAt(f.t, 5.3f, 1);
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        float side = (i == f.z.first) ? 1.0f : -1.0f;
        float b = 0.35f + 0.65f * clamp01(0.5f + 0.5f * look * side);
        scale(f.c.out[i], f.p.c[1], b * bl);
    }
}
static void eFlicker(Fx &f) // candle / fire flicker
{
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        float n = noise1(f.t * 6 + i * 9.1f) * 0.6f + noise1(f.t * 13 + i * 4.3f) * 0.4f;
        Rgb col;
        palAt(f.p, n * 0.4f, col);
        scale(f.c.out[i], col, 0.35f + 0.65f * n);
    }
}
static void eDemon(Fx &f) // dim and smouldering, then flaring bright
{
    float p = fmodf(f.t, 6.0f);
    float flare = p > 5.2f ? sinf((p - 5.2f) / 0.8f * 3.14159f) : 0;
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        scale(f.c.out[i], f.p.c[0], 0.25f + 0.1f * sinf(f.t * 2 + i));
        add(f.c.out[i], f.p.c[2], flare);
    }
}
static void eWink(Fx &f) // steady, and now and then one eye winks
{
    float p = fmodf(f.t, 7.0f);
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        bool winking = (i == f.z.first + 1) && p > 6.0f && p < 6.35f;
        scale(f.c.out[i], f.p.c[2], winking ? 0.02f : 0.85f);
    }
}
static void eCycle(Fx &f) // both eyes cycling through the palette, a little apart
{
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        Rgb col;
        palAt(f.p, f.t * 0.15f + (i - f.z.first) * 0.12f, col);
        scale(f.c.out[i], col, blinkAt(f.t, 6.1f, 2));
    }
}
static void eScan(Fx &f) // a light flicking from eye to eye
{
    float s = 0.5f + 0.5f * sinf(f.t * 3.2f);
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        float k = (i == f.z.first) ? s : 1 - s;
        scale(f.c.out[i], f.p.c[3], 1);
        add(f.c.out[i], f.p.c[2], k * k);
    }
}

// ------------------------------------------------------------------------------------
// Moon effects
// ------------------------------------------------------------------------------------
static void mPhase(Fx &f) // moon phases: the lit part grows and shrinks across the moon
{
    float ph = fractf(f.t / 12); // one full cycle
    float edge = ph < 0.5f ? ph * 2 : (ph - 0.5f) * 2;
    bool waxing = ph < 0.5f;
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        float x = moonX(i);
        float lit = waxing ? smooth((edge - (1 - x)) * 6 + 0.5f) : smooth((x - edge) * 6 + 0.5f);
        scale(f.c.out[i], f.p.c[3], 1);
        add(f.c.out[i], f.p.c[2], 0.85f * lit);
    }
}
static void mGlow(Fx &f) // slow breathing glow
{
    float b = 0.35f + 0.65f * smooth(0.5f + 0.5f * sinf(f.t * TAU / 6));
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        Rgb col;
        palAt(f.p, 0.5f + 0.1f * sinf(f.t * 0.3f + i), col);
        scale(f.c.out[i], col, b);
    }
}
static void mHalo(Fx &f) // a bright spot circling the rim
{
    float a = fractf(f.t * 0.25f);
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        float d = fabsf(LEDS[i].gx - a);
        if (d > 0.5f)
            d = 1 - d;
        float k = clamp01(1 - d * 3.5f) * clamp01(LEDS[i].gy);
        Rgb col;
        palAt(f.p, a + 0.25f, col);
        scale(f.c.out[i], f.p.c[1], 0.15f);
        add(f.c.out[i], col, k);
    }
}
static void mEclipse(Fx &f) // a shadow crossing the moon, the rim glowing red
{
    float pos = fractf(f.t / 8) * 1.8f - 0.4f;
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        float d = fabsf(moonX(i) - pos);
        float shadow = 1 - smooth(1 - d * 2.5f);
        scale(f.c.out[i], f.p.c[2], 0.15f + 0.75f * shadow);
        add(f.c.out[i], f.p.c[0], (1 - shadow) * 0.5f * clamp01(LEDS[i].gy));
    }
}
static void mTwinkle(Fx &f) // pale moon with stars glinting around it
{
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        mem[i] -= f.c.dt * 0.8f;
        if (mem[i] < -0.5f && frand() < f.c.dt * 0.4f)
            mem[i] = 1;
        scale(f.c.out[i], f.p.c[2], 0.25f);
        if (mem[i] > 0)
            add(f.c.out[i], f.p.c[2], sinf(mem[i] * 3.14159f) * 0.8f);
    }
}
static void mShimmer(Fx &f) // aurora-like shimmer
{
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        float n = noise1(f.t * 0.8f + LEDS[i].gx * 5) * 0.6f + noise1(f.t * 1.7f + i * 3.3f) * 0.4f;
        Rgb col;
        palAt(f.p, n * 0.8f + f.t * 0.02f, col);
        scale(f.c.out[i], col, 0.3f + 0.7f * n);
    }
}
static void mFull(Fx &f) // full moon: pale light with clouds drifting across it
{
    float cloud = fractf(f.t / 9) * 2.0f - 0.5f; // cloud centre moving left to right
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        float x = moonX(i);
        float d = fabsf(x - cloud - 0.15f * sinf(f.t * 0.7f + i));
        float shade = 1 - 0.7f * smooth(1 - d * 3);
        float n = 0.9f + 0.1f * noise1(f.t * 0.7f + i * 5);
        scale(f.c.out[i], f.p.c[2], 0.75f * n * shade);
        add(f.c.out[i], f.p.c[1], 0.12f * shade);
    }
}

// ------------------------------------------------------------------------------------
// Old West effects
// ------------------------------------------------------------------------------------
// A storm shared by every part, worked out from the time alone so the keypad, sign and
// moon flash together (and badges in a light show flash together).
static float lightning(float t, int *boltCol = nullptr)
{
    // one chance of a strike a minute, taken half the time: one every couple of minutes.
    // The first minute always strikes, a few seconds in, so you see it when picking the scene.
    float slot = floorf(t / 60.0f), in = t - slot * 60.0f;
    if (boltCol)
        *boltCol = (int)(hash1(slot * 5.3f + 1.7f) * 3) % 3;
    if (slot > 0 && hash1(slot * 1.37f + 0.5f) > 0.5f)
        return 0; // no strike this time
    float x = in - (slot > 0 ? hash1(slot * 2.11f + 3.1f) * 55.0f : 3.0f);
    if (x < 0)
        return 0;
    float v = 0;
    if (x < 0.07f)
        v = 1;
    else if (x > 0.14f && x < 0.19f)
        v = 0.6f;
    else if (x > 0.3f)
        v = expf(-(x - 0.3f) * 5) * (x < 0.36f ? 1.0f : 0.45f);
    return clamp01(v);
}
static int keyAt(int col, int row)
{
    for (int i = 0; i < KEYPAD_LEDS; i++)
        if (LEDS[i].col == col && LEDS[i].row == row)
            return i;
    return -1;
}

static void kStalk(Fx &f) // the dark keypad, and a pair of eyes that opens somewhere, watches, and is gone
{
    static int stage = 0, ea = -1, eb = -1;
    static float st = 0, dur = 1.5f;
    if (f.c.reset)
        stage = 0, st = 0, dur = 1.5f, ea = eb = -1;
    st += f.c.dt;
    if (st >= dur) {
        st = 0;
        stage = (stage + 1) % 4;
        if (stage == 1) { // somewhere new
            int row = (int)(frand() * 4) % 4, col = (int)(frand() * 2) % 2;
            ea = keyAt(col, row);
            eb = keyAt(col + 1, row);
            dur = 0.9f;
        } else if (stage == 2) {
            dur = 2.0f + frand() * 2.5f;
        } else if (stage == 3) {
            dur = 0.12f; // gone in a blink
        } else {
            dur = 2.0f + frand() * 4.0f;
        }
    }
    float lv = 0;
    if (stage == 1)
        lv = smooth(st / dur);
    else if (stage == 2)
        lv = fmodf(st, 1.9f) < 0.12f ? 0.04f : 1; // a blink now and then
    else if (stage == 3)
        lv = 1 - st / dur;
    lv *= 0.85f + 0.15f * noise1(f.t * 4);
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        // something moving in the dark: a faint shadow drifting over the keys
        float sh = noise1(f.t * 0.6f + LEDS[i].gx * 2 + LEDS[i].gy * 3);
        scale(f.c.out[i], f.p.c[3], 0.6f);
        add(f.c.out[i], f.p.c[1], 0.07f * sh * sh);
        if (i == ea || i == eb)
            add(f.c.out[i], f.p.c[2], lv);
    }
}
static void kDust(Fx &f) // gusts of dust blowing across, and a dust devil wandering through
{
    float px = 0.5f + 0.55f * sinf(f.t * 0.45f) * cosf(f.t * 0.17f);
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        float x = LEDS[i].gx + LEDS[i].gy * 0.3f;
        float d = noise1(x * 2.5f - f.t * 1.8f) * 0.6f + noise1(x * 5 - f.t * 3.1f + LEDS[i].gy * 4) * 0.4f;
        Rgb col;
        palAt(f.p, d * 0.5f, col);
        scale(f.c.out[i], col, 0.12f + 0.75f * d * d);
        float dx = LEDS[i].gx - px;
        float k = expf(-dx * dx * 14) * (0.55f + 0.45f * sinf(f.t * 7 + LEDS[i].gy * 9));
        add(f.c.out[i], f.p.c[2], 0.6f * k);
    }
}
static void kLightning(Fx &f) // storm clouds rolling, then lightning down one column
{
    int bolt = 0;
    float L = lightning(f.t, &bolt);
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        float cl = noise1(f.t * 0.5f + LEDS[i].gx * 1.5f + LEDS[i].gy * 0.8f);
        scale(f.c.out[i], f.p.c[3], 0.8f);
        add(f.c.out[i], f.p.c[0], 0.2f * cl);
        bool inBolt = LEDS[i].col == bolt;
        add(f.c.out[i], f.p.c[1], L * 0.45f);
        if (inBolt)
            add(f.c.out[i], f.p.c[2], L);
    }
}
static void kEmbers(Fx &f) // a campfire on the bottom row, sparks drifting up
{
    static float sy[4], sc[4], sl[4];
    if (f.c.reset)
        for (int k = 0; k < 4; k++)
            sl[k] = 0;
    for (int k = 0; k < 4; k++) {
        if (sl[k] > 0) {
            sy[k] += f.c.dt * (1.1f + 0.4f * k / 3.0f);
            sl[k] -= f.c.dt * 0.5f;
        } else if (frand() < f.c.dt * 0.8f) {
            sy[k] = 0, sl[k] = 1, sc[k] = (float)((int)(frand() * 3) % 3);
        }
    }
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        float h = 1 - LEDS[i].gy; // 0 bottom .. 1 top
        float n = noise1(f.t * 4 + i * 2.3f) * 0.6f + noise1(f.t * 9 + i * 5.1f) * 0.4f;
        float heat = clamp01(0.95f - h * 1.5f + (n - 0.5f) * 0.5f);
        Rgb a;
        for (int j = 0; j < 3; j++)
            a[j] = heat < 0.5f ? mixf(f.p.c[3][j], f.p.c[0][j], heat * 2) : mixf(f.p.c[0][j], f.p.c[1][j], (heat - 0.5f) * 2);
        scale(f.c.out[i], a, 0.25f + 0.75f * heat);
        for (int k = 0; k < 4; k++) {
            if (sl[k] <= 0 || LEDS[i].col != (int)sc[k])
                continue;
            float rowUp = 3 - LEDS[i].row; // 0 bottom row .. 3 top
            float kk = clamp01(1 - fabsf(rowUp - sy[k]) * 1.4f) * sl[k];
            add(f.c.out[i], f.p.c[2], kk);
        }
    }
}
static void kMarquee(Fx &f) // saloon marquee: every third bulb lit, stepping round, bulbs warming and cooling
{
    int step = (int)floorf(f.t * 4);
    float rate = f.c.dt * 14 > 1 ? 1 : f.c.dt * 14;
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        float target = ((i + step) % 3 == 0) ? 1.0f : 0.0f;
        mem[i] += (target - mem[i]) * rate; // a filament doesn't switch instantly
        scale(f.c.out[i], f.p.c[1], 0.12f);
        add(f.c.out[i], f.p.c[2], mem[i]);
    }
}
static void kTumble(Fx &f) // a tumbleweed bouncing across while the wind blows
{
    float x = fractf(f.t * 0.22f) * 2.2f - 0.6f;
    float y = 3 - fabsf(sinf(f.t * 2.4f)) * 1.6f; // rows from the top
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        float wind = noise1(LEDS[i].gx * 3 - f.t * 2.2f + LEDS[i].gy * 7);
        scale(f.c.out[i], f.p.c[3], 0.7f);
        add(f.c.out[i], f.p.c[0], 0.18f * wind * wind);
        float dx = (LEDS[i].gx - x) * 2, dy = LEDS[i].row - y;
        float k = clamp01(1 - hypotf(dx, dy) / 1.3f);
        add(f.c.out[i], f.p.c[1], k * (0.75f + 0.25f * sinf(f.t * 9 + i)));
    }
}
static void kRattle(Fx &f) // a diamondback winding through the keys; now and then it stops and rattles
{
    static float head = 0, rattle = 0;
    if (f.c.reset)
        head = 0, rattle = 0;
    if (rattle > 0) {
        rattle -= f.c.dt;
    } else {
        head += f.c.dt * 2.4f;
        if (head > 4 && frand() < f.c.dt * 0.25f)
            rattle = 1.3f;
        if (head > f.z.count + 7)
            head = 0;
    }
    float shake = rattle > 0 ? 0.5f + 0.5f * sinf(f.t * TAU * 11) : 1;
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        float d = head - (i - f.z.first); // 0 = head
        scale(f.c.out[i], f.p.c[3], 0.6f);
        if (d < 0 || d >= 6)
            continue;
        const Rgb &skin = ((int)floorf(d) & 1) ? f.p.c[1] : f.p.c[0];
        float k = d < 1 ? 1 : 1 - (d - 1) / 6;
        if (d >= 4.5f)
            k *= shake; // the rattle
        add(f.c.out[i], skin, k);
        if (d < 1)
            add(f.c.out[i], f.p.c[2], 0.35f);
    }
}

static void lLantern(Fx &f) // a lantern swinging on a hook, its flame flickering
{
    float pos = 0.5f + 0.5f * sinf(f.t * 1.5f);
    float flame = 0.75f + 0.25f * (noise1(f.t * 8) * 0.6f + noise1(f.t * 17) * 0.4f);
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        float d = LEDS[i].gx - pos;
        float k = expf(-d * d * 5) * flame;
        scale(f.c.out[i], f.p.c[3], 1);
        add(f.c.out[i], f.p.c[0], k);
        add(f.c.out[i], f.p.c[2], 0.3f * k * k * k);
    }
}
static void lStar(Fx &f) // a polished sheriff's star: steady gold, a glint sweeping across now and then
{
    float p = fmodf(f.t, 3.5f);
    float g = p < 0.6f ? p / 0.6f * 1.4f - 0.2f : -9;
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        scale(f.c.out[i], f.p.c[0], 0.7f);
        add(f.c.out[i], f.p.c[2], clamp01(1 - fabsf(LEDS[i].gx - g) * 4));
    }
}
static void lBulbs(Fx &f) // saloon sign bulbs, trading places, warming up and cooling down
{
    int step = (int)floorf(f.t * 1.5f);
    float rate = f.c.dt * 9 > 1 ? 1 : f.c.dt * 9;
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        float target = ((i + step) & 1) ? 1.0f : 0.06f;
        mem[i] += (target - mem[i]) * rate;
        scale(f.c.out[i], f.p.c[1], 0.1f);
        add(f.c.out[i], f.p.c[2], mem[i]);
    }
}
static void lStorm(Fx &f) // the sign in a storm: dim, lit up by the lightning
{
    float L = lightning(f.t - 0.03f);
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        scale(f.c.out[i], f.p.c[0], 0.18f + 0.1f * noise1(f.t + i));
        add(f.c.out[i], f.p.c[2], L);
    }
}

static void eStalker(Fx &f) // eyes in the dark: slowly open, stare, blink, and snap shut
{
    float slot = floorf(f.t / 9), in = f.t - slot * 9;
    float start = hash1(slot * 3.7f + 0.3f) * 3.5f;
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        float x = in - start - (i - f.z.first) * 0.15f; // one eye opens a moment after the other
        float lv = 0;
        if (x > 0 && x < 1.2f)
            lv = smooth(x / 1.2f);
        else if (x >= 1.2f && x < 4.2f)
            lv = (x > 2.6f && x < 2.75f) ? 0.03f : 1;
        else if (x >= 4.2f && x < 4.3f)
            lv = 1 - (x - 4.2f) / 0.1f;
        lv *= 0.85f + 0.15f * sinf(f.t * 3 + i);
        scale(f.c.out[i], f.p.c[3], 0.5f);
        add(f.c.out[i], f.p.c[2], lv);
    }
}
static void eCoyote(Fx &f) // eyeshine: a pair of eyes caught in the lantern light, then gone
{
    float slot = floorf(f.t / 4.5f), in = f.t - slot * 4.5f;
    bool seen = hash1(slot * 1.9f + 7.1f) < 0.7f;
    float x = in - hash1(slot * 4.3f + 2.2f) * 3.0f;
    float k = (seen && x > 0 && x < 1.1f) ? sinf(x / 1.1f * 3.14159f) : 0;
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        scale(f.c.out[i], f.p.c[3], 0.4f);
        add(f.c.out[i], f.p.c[2], k * k);
    }
}

static void mHowl(Fx &f) // a big moon that swells brighter, as if something is howling at it
{
    float p = fmodf(f.t, 10) / 10;
    float swell = p < 0.35f ? smooth(p / 0.35f) : (p < 0.6f ? 1 : 1 - smooth((p - 0.6f) / 0.4f));
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        float rim = clamp01(LEDS[i].gy);
        float b = 0.3f + 0.6f * swell * (0.6f + 0.4f * rim) + 0.06f * swell * sinf(f.t * 23 + i);
        scale(f.c.out[i], f.p.c[2], b);
        add(f.c.out[i], f.p.c[0], 0.15f * (1 - swell));
    }
}
static void mStorm(Fx &f) // the moon behind storm clouds, the lightning lighting them up
{
    float L = lightning(f.t - 0.05f);
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        float cl = noise1(f.t * 0.4f + moonX(i) * 2);
        scale(f.c.out[i], f.p.c[2], 0.08f + 0.3f * cl * cl);
        add(f.c.out[i], f.p.c[1], 0.1f);
        add(f.c.out[i], f.p.c[2], 0.9f * L * (0.5f + 0.5f * clamp01(LEDS[i].gy)));
    }
}
static void mDust(Fx &f) // a hazy moon seen through blowing dust
{
    for (int i = f.z.first; i < f.z.first + f.z.count; i++) {
        float haze = noise1(f.t * 0.5f + moonX(i) * 2) * 0.6f + noise1(f.t * 1.3f + i * 2) * 0.4f;
        scale(f.c.out[i], f.p.c[2], 0.2f + 0.3f * (1 - haze));
        add(f.c.out[i], f.p.c[0], 0.3f * haze);
    }
}

static void zOff(Fx &f)
{
    for (int i = f.z.first; i < f.z.first + f.z.count; i++)
        set(f.c.out[i], 0, 0, 0);
}

enum KFx : uint8_t { K_WAVE, K_RAIN, K_SPARKLE, K_FIRE, K_SNAKE, K_RIPPLE, K_SCAN, K_BREATHE, K_PLASMA, K_CHECKER, K_SPIRAL, K_EQ,
                     K_STALK, K_DUST, K_LIGHTNING, K_EMBERS, K_MARQUEE, K_TUMBLE, K_RATTLE, K_OFF };
enum LFx : uint8_t { L_FLOW, L_CHASE, L_NEON, L_BREATHE, L_SPARK, L_HEART, L_STEP, L_LANTERN, L_STAR, L_BULBS, L_STORM, L_OFF };
enum EFx : uint8_t { E_GLOW, E_LOOK, E_FLICKER, E_DEMON, E_WINK, E_CYCLE, E_SCAN, E_STALKER, E_COYOTE, E_OFF };
enum MFx : uint8_t { M_PHASE, M_GLOW, M_HALO, M_ECLIPSE, M_TWINKLE, M_SHIMMER, M_FULL, M_HOWL, M_STORM, M_DUST, M_OFF };
static const FxFn KEYPAD_FX[] = {kWave, kRain, kSparkle, kFire, kSnake, kRipple, kScan, kBreathe, kPlasma, kChecker, kSpiral, kEq,
                                  kStalk, kDust, kLightning, kEmbers, kMarquee, kTumble, kRattle, zOff};
static const FxFn LOGO_FX[] = {lFlow, lChase, lNeon, lBreathe, lSpark, lHeart, lStep, lLantern, lStar, lBulbs, lStorm, zOff};
static const FxFn EYES_FX[] = {eGlow, eLook, eFlicker, eDemon, eWink, eCycle, eScan, eStalker, eCoyote, zOff};
static const FxFn MOON_FX[] = {mPhase, mGlow, mHalo, mEclipse, mTwinkle, mShimmer, mFull, mHowl, mStorm, mDust, zOff};

// ------------------------------------------------------------------------------------
// Scenes: name, palette, keypad, logo, eyes, moon
// ------------------------------------------------------------------------------------
static const Scene SCENES[] = {
    // the 2025 colours (renamed for the theme), now with an effect on every part
    {"PAINTED DESERT", P_RAINBOW, K_WAVE, L_FLOW, E_CYCLE, M_HALO},
    // Old West: the badge's theme
    {"SKINWALKER", P_SKINWALKER, K_STALK, L_NEON, E_STALKER, M_ECLIPSE},
    {"SHAPESHIFTER", P_SKINWALKER, K_PLASMA, L_STEP, E_STALKER, M_PHASE},
    {"COYOTE MOON", P_COYOTE, K_SPARKLE, L_BREATHE, E_COYOTE, M_HOWL},
    {"CAMPFIRE", P_CAMPFIRE, K_EMBERS, L_LANTERN, E_FLICKER, M_FULL},
    {"SALOON", P_SALOON, K_MARQUEE, L_BULBS, E_WINK, M_GLOW},
    {"SHERIFF'S STAR", P_SHERIFF, K_SCAN, L_STAR, E_GLOW, M_FULL},
    {"WANTED POSTER", P_WANTED, K_BREATHE, L_LANTERN, E_LOOK, M_PHASE},
    {"DUST DEVIL", P_DUST, K_DUST, L_FLOW, E_FLICKER, M_DUST},
    {"RATTLESNAKE", P_RATTLER, K_RATTLE, L_CHASE, E_COYOTE, M_SHIMMER},
    {"BONE ORCHARD", P_BONE, K_SPARKLE, L_NEON, E_WINK, M_FULL},
    {"DEAD MAN'S HAND", P_BONE, K_CHECKER, L_BULBS, E_DEMON, M_ECLIPSE},
    {"THUNDERBIRD", P_THUNDER, K_LIGHTNING, L_STORM, E_GLOW, M_STORM},
    {"GHOST TOWN", P_GHOSTTOWN, K_TUMBLE, L_NEON, E_LOOK, M_DUST},
    {"SILVER MINE", P_MINE, K_SPARKLE, L_LANTERN, E_COYOTE, M_GLOW},
    {"HIGH NOON", P_NOON, K_PLASMA, L_STAR, E_GLOW, M_FULL},
    {"CAVALRY BLUE", P_BLUEWHITE, K_CHECKER, L_CHASE, E_GLOW, M_FULL},
    {"BLOOD ON THE SADDLE", P_REDMAROON, K_BREATHE, L_HEART, E_DEMON, M_ECLIPSE},
    {"DESERT ROSE", P_PINKWHITE, K_SPARKLE, L_FLOW, E_WINK, M_TWINKLE},
    {"PRICKLY PEAR", P_PINKPURPLE, K_PLASMA, L_BREATHE, E_LOOK, M_SHIMMER},
    {"TURQUOISE TRAIL", P_GREENBLUE, K_RAIN, L_CHASE, E_SCAN, M_GLOW},
    // Utah
    {"RED ROCKS SUNSET", P_SUNSET, K_WAVE, L_FLOW, E_GLOW, M_PHASE},
    {"DESERT NIGHT", P_DESERT, K_SPARKLE, L_NEON, E_LOOK, M_FULL},
    {"GOLD RUSH", P_GOLD, K_SCAN, L_CHASE, E_GLOW, M_FULL},
    // colourful
    {"MIDNIGHT RODEO", P_SYNTH, K_EQ, L_CHASE, E_SCAN, M_HALO},
    {"BOOMTOWN", P_NEON, K_SNAKE, L_NEON, E_CYCLE, M_SHIMMER},
    {"FLASH FLOOD", P_OCEAN, K_RIPPLE, L_FLOW, E_GLOW, M_SHIMMER},
    {"FROZEN PASS", P_ICE, K_SPARKLE, L_BREATHE, E_WINK, M_TWINKLE},
    {"PRAIRIE FIRE", P_FIRE, K_FIRE, L_SPARK, E_FLICKER, M_GLOW},
    {"BRANDING IRON", P_LAVA, K_PLASMA, L_HEART, E_DEMON, M_ECLIPSE},
    {"SNAKE OIL", P_TOXIC, K_RAIN, L_NEON, E_FLICKER, M_SHIMMER},
    {"SPIRIT LIGHTS", P_AURORA, K_WAVE, L_FLOW, E_LOOK, M_SHIMMER},
    {"JUNIPER FIREFLIES", P_FOREST, K_BREATHE, L_SPARK, E_GLOW, M_TWINKLE},
    {"GENERAL STORE", P_CANDY, K_CHECKER, L_STEP, E_WINK, M_HALO},
    {"MIRAGE", P_VAPOR, K_SPIRAL, L_FLOW, E_CYCLE, M_GLOW},
    {"MOONSHINE", P_MATRIX, K_RAIN, L_NEON, E_SCAN, M_TWINKLE},
    // Halloween
    {"WITCHING HOUR", P_HALLOWEEN, K_PLASMA, L_STEP, E_DEMON, M_ECLIPSE},
    {"HARVEST LANTERN", P_PUMPKIN, K_FIRE, L_NEON, E_FLICKER, M_FULL},
    {"BLOOD MOON", P_BLOOD, K_BREATHE, L_HEART, E_DEMON, M_ECLIPSE},
    {"GHOST RIDERS", P_GHOST, K_RIPPLE, L_BREATHE, E_LOOK, M_PHASE},
    {"HEXED", P_CAULDRON, K_EQ, L_SPARK, E_FLICKER, M_SHIMMER},
    {"BOOT HILL", P_GHOST, K_SPARKLE, L_NEON, E_WINK, M_PHASE},
    {"POSSESSED", P_CAULDRON, K_SPIRAL, L_NEON, E_DEMON, M_ECLIPSE},
    {"CORNFIELD", P_CANDYCORN, K_WAVE, L_STEP, E_GLOW, M_FULL},
    // calm keys that light up as you type
    {"WATERING HOLE", P_OCEAN, K_RIPPLE, L_BREATHE, E_GLOW, M_GLOW},
    // colour from the phone app (Meshtastic: Module settings > Ambient Lighting)
    {"YOUR BRAND", P_APP, K_BREATHE, L_FLOW, E_GLOW, M_GLOW},
    {"YOUR BRAND GLITTER", P_APP, K_SPARKLE, L_SPARK, E_CYCLE, M_SHIMMER},
    {"LIGHTS OUT", P_BLUEWHITE, K_OFF, L_OFF, E_OFF, M_OFF},
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
// earlier names, so a saved scene, an older badge's light show or "!lights rainbow" still work
static const char *const RENAMED[][2] = {
    {"RAINBOW", "PAINTED DESERT"},
    {"BLUE + WHITE", "CAVALRY BLUE"},
    {"RED + MAROON", "BLOOD ON THE SADDLE"},
    {"PINK + WHITE", "DESERT ROSE"},
    {"PINK + PURPLE", "PRICKLY PEAR"},
    {"GREEN + BLUE", "TURQUOISE TRAIL"},
    {"BSIDES GOLD", "GOLD RUSH"},
    {"SYNTHWAVE", "MIDNIGHT RODEO"},
    {"NEON CITY", "BOOMTOWN"},
    {"OCEAN", "FLASH FLOOD"},
    {"ICE CAVE", "FROZEN PASS"},
    {"FIRE", "PRAIRIE FIRE"},
    {"LAVA", "BRANDING IRON"},
    {"TOXIC", "SNAKE OIL"},
    {"AURORA", "SPIRIT LIGHTS"},
    {"FIREFLY FOREST", "JUNIPER FIREFLIES"},
    {"CANDY", "GENERAL STORE"},
    {"VAPORWAVE", "MIRAGE"},
    {"THE MATRIX", "MOONSHINE"},
    {"HALLOWEEN", "WITCHING HOUR"},
    {"JACK-O-LANTERN", "HARVEST LANTERN"},
    {"GHOSTS", "GHOST RIDERS"},
    {"CAULDRON", "HEXED"},
    {"GRAVEYARD", "BOOT HILL"},
    {"CANDY CORN", "CORNFIELD"},
    {"TYPE RIPPLE", "WATERING HOLE"},
    {"APP COLOUR", "YOUR BRAND"},
    {"APP SHIMMER", "YOUR BRAND GLITTER"},
    {"OFF", "LIGHTS OUT"},
};

static bool sameName(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'a' && x <= 'z')
            x -= 32;
        if (y >= 'a' && y <= 'z')
            y -= 32;
        if (x != y)
            return false;
    }
    return *a == *b;
}

int sceneFind(const char *name) // any case; old names too
{
    if (!name)
        return -1;
    for (int i = 0; i < sceneCount(); i++)
        if (sameName(SCENES[i].name, name))
            return i;
    for (const auto &r : RENAMED)
        if (sameName(r[0], name))
            return sceneFind(r[1]);
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

void sceneSetAppColour(float r, float g, float b)
{
    if (r + g + b < 0.01f) // not set in the app: keep the default
        return;
    auto pack = [](float r2, float g2, float b2) -> uint32_t {
        return ((uint32_t)(clamp01(r2) * 255) << 16) | ((uint32_t)(clamp01(g2) * 255) << 8) | (uint32_t)(clamp01(b2) * 255);
    };
    PALETTES[P_APP].c[0] = pack(r, g, b);
    PALETTES[P_APP].c[1] = pack(r * 0.55f, g * 0.55f, b * 0.55f);
    PALETTES[P_APP].c[2] = pack(r * 0.6f + 0.4f, g * 0.6f + 0.4f, b * 0.6f + 0.4f);
    PALETTES[P_APP].c[3] = pack(r * 0.08f, g * 0.08f, b * 0.08f);
}

void sceneRender(int i, SceneCtx &c)
{
    const Scene &s = sceneAt(i);
    if (c.reset) {
        memset(mem, 0, sizeof(mem));
        memset(mem2, 0, sizeof(mem2));
    }
    Pal p;
    loadPal(s.palette, p);
    Fx k = {c, p, Z_KEYPAD, c.t};
    KEYPAD_FX[s.keypad](k);
    Fx l = {c, p, Z_LOGO, c.t};
    LOGO_FX[s.logo](l);
    Fx e = {c, p, Z_EYES, c.t};
    EYES_FX[s.eyes](e);
    Fx m = {c, p, Z_MOON, c.t};
    MOON_FX[s.moon](m);
    // the skull glows with the eyes' effect (a little behind them), not with the keys
    Fx sk = {c, p, Z_SKULL, c.t - 0.4f};
    EYES_FX[s.eyes](sk);
}

void sceneAccent(int i, Rgb &out)
{
    const Scene &s = sceneAt(i);
    if (s.palette == P_RAINBOW) {
        set(out, 1, 1, 1);
        return;
    }
    Pal p;
    loadPal(s.palette, p);
    // the highlight colour, or white if the palette is very dark
    float lum = 0.3f * p.c[2][0] + 0.55f * p.c[2][1] + 0.15f * p.c[2][2];
    if (lum < 0.25f)
        set(out, 1, 1, 1);
    else
        scale(out, p.c[2], 1);
}

// ------------------------------------------------------------------------------------
// Overlays
// ------------------------------------------------------------------------------------
void overlayKeyFlash(SceneCtx &c, const float level[KEYPAD_LEDS], const Rgb &col)
{
    for (int i = 0; i < KEYPAD_LEDS; i++) {
        float k = level[i];
        if (k <= 0)
            continue;
        for (int j = 0; j < 3; j++)
            c.out[i][j] = mixf(c.out[i][j], col[j], k);
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
void overlayPulse(SceneCtx &c, float level, bool moon, const Rgb &col)
{
    float k = clamp01(level);
    for (int i = FIRST_LOGO; i < NUM_LEDS; i++) {
        if (LEDS[i].group == GROUP_EYES || (moon && LEDS[i].group == GROUP_MOON))
            for (int j = 0; j < 3; j++)
                c.out[i][j] = mixf(c.out[i][j], col[j], k * 0.7f);
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

} // namespace badge

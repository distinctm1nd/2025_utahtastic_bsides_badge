// Skinwalker pet: a creature you raise over the conference. It feeds on what the badge does --
// new badges met, scavenger-hunt flags, achievements, fox-hunt sightings -- and grows from a
// pup to an elder. Left hungry and ignored it turns feral. State lives in NVS (namespace
// "utahwalk") so it keeps growing between power-offs.
//
//   5 feed it   # play with it   * done
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeStats.h"
#include "BadgeTime.h"
#include "mesh/Throttle.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

namespace badge
{

// one "activity point" of food = a badge met, a scavenger flag, or an achievement
static int activityScore()
{
    return statsBadgeCount() * 3 + statsFlagCount() * 2 + statsAchievedCount() * 2;
}

struct PetState {
    uint8_t valid;    // 42 once set up
    uint8_t xp;       // 0..255, growth
    uint8_t fullness; // 0..100
    uint8_t energy;   // 0..100
    uint8_t bond;     // 0..100
    uint8_t feral;    // 1 once it turns feral (needs care to tame back)
    uint32_t lastSec; // epoch (or uptime seconds) of the last decay step
    uint32_t lastAct; // activityScore() last turned into food
    uint32_t bornSec;
};
static PetState pet;
static bool loaded = false;

static uint32_t nowSec()
{
    if (timeKnown())
        return (uint32_t)time(nullptr);
    return millis() / 1000; // no clock yet: uptime, so decay still moves
}

static void save()
{
    Preferences p;
    if (p.begin("utahwalk", false)) {
        p.putBytes("pet", &pet, sizeof(pet));
        p.end();
    }
}

static void load()
{
    if (loaded)
        return;
    loaded = true;
    Preferences p;
    memset(&pet, 0, sizeof(pet));
    if (p.begin("utahwalk", true)) {
        p.getBytes("pet", &pet, sizeof(pet));
        p.end();
    }
    if (pet.valid != 42) {
        memset(&pet, 0, sizeof(pet));
        pet.valid = 42;
        pet.fullness = 70;
        pet.energy = 80;
        pet.bond = 40;
        pet.lastSec = nowSec();
        pet.bornSec = pet.lastSec;
        pet.lastAct = activityScore();
        save();
    }
}

static uint8_t clamp8(int v)
{
    return v < 0 ? 0 : v > 100 ? 100 : (uint8_t)v;
}

// bring the pet up to date: decay over elapsed time, then feed it any new badge activity
static void catchUp()
{
    uint32_t now = nowSec();
    uint32_t elapsed = now > pet.lastSec ? now - pet.lastSec : 0;
    pet.lastSec = now;
    if (elapsed > 7 * 24 * 3600)
        elapsed = 7 * 24 * 3600; // a week away, no more
    int steps = elapsed / 900;    // one decay step every 15 min
    if (steps > 0) {
        pet.fullness = clamp8(pet.fullness - steps * 4);
        pet.energy = clamp8(pet.energy + steps * 2); // rests while you're away
        if (pet.fullness < 15)
            pet.bond = clamp8(pet.bond - steps * 3); // starving, it loses trust
    }
    uint32_t act = activityScore();
    if (act > pet.lastAct) {
        int food = (act - pet.lastAct);
        pet.fullness = clamp8(pet.fullness + food * 6);
        pet.bond = clamp8(pet.bond + food * 2);
        pet.xp = (uint8_t)(pet.xp + food * 4 > 255 ? 255 : pet.xp + food * 4);
    }
    pet.lastAct = act;
    if (pet.fullness == 0 && pet.bond < 10)
        pet.feral = 1;
    if (pet.feral && pet.bond > 60 && pet.fullness > 50)
        pet.feral = 0; // tamed back
    save();
}

// 0 pup, 1 young, 2 adult, 3 elder
static int stage()
{
    return pet.xp < 40 ? 0 : pet.xp < 110 ? 1 : pet.xp < 200 ? 2 : 3;
}
static const char *stageName()
{
    static const char *N[] = {"Pup", "Yearling", "Adult", "Elder"};
    return N[stage()];
}
static const char *moodName()
{
    if (pet.feral)
        return "Feral";
    if (pet.fullness < 20)
        return "Starving";
    if (pet.energy < 20)
        return "Tired";
    if (pet.bond > 75 && pet.fullness > 60)
        return "Content";
    if (pet.bond < 25)
        return "Wary";
    return "Watchful";
}

// ---- pet interaction as free functions, so the Home screen can embed the pet too ----------
static char note[32] = "";
static uint32_t noteAt = 0, lastCatch = 0, lastFeed = 0, lastPlay = 0;

static void say(const char *m)
{
    snprintf(note, sizeof(note), "%s", m);
    noteAt = millis();
}

void skinPetTick(uint32_t now)
{
    load();
    if (!Throttle::isWithinTimespanMs(lastCatch, 20000)) {
        lastCatch = now;
        catchUp();
    }
    if (note[0] && !Throttle::isWithinTimespanMs(noteAt, 2500))
        note[0] = 0;
}

// feed (5) / play (#); returns true if it acted. '*' and navigation are left to the caller.
bool skinPetKey(int k, KeyAction a)
{
    if (a != KA_PRESS)
        return false;
    if (k == AK_5) {
        if (Throttle::isWithinTimespanMs(lastFeed, 60000)) {
            say("It's not hungry yet");
        } else {
            lastFeed = millis();
            pet.fullness = clamp8(pet.fullness + 20);
            pet.bond = clamp8(pet.bond + 4);
            pet.xp = (uint8_t)(pet.xp + 2 > 255 ? 255 : pet.xp + 2);
            say(pet.feral ? "It eats, watching you" : "It eats from your hand");
            save();
        }
        return true;
    }
    if (k == AK_HASH) {
        if (pet.energy < 15) {
            say("Too tired to play");
        } else if (Throttle::isWithinTimespanMs(lastPlay, 45000)) {
            say("It's had enough for now");
        } else {
            lastPlay = millis();
            pet.energy = clamp8(pet.energy - 15);
            pet.bond = clamp8(pet.bond + 8);
            pet.xp = (uint8_t)(pet.xp + 3 > 255 ? 255 : pet.xp + 3);
            say(pet.feral ? "It circles, but stays" : "It plays, tail low");
            save();
        }
        return true;
    }
    return false;
}

static void petBar(OLEDDisplay *d, int x, int y, const char *label, int v)
{
    d->setFont(FONT_SMALL);
    d->setColor(WHITE);
    d->setTextAlignment(TEXT_ALIGN_LEFT);
    d->drawString(x, y - 3, label);
    int bx = x + 22, bw = 30;
    d->drawRect(bx, y, bw, 7);
    d->fillRect(bx, y, bw * v / 100, 7);
}

// a lean four-legged silhouette; bigger with each stage, red eyes and raised back when feral
static void petCreature(OLEDDisplay *d, int cx, int cy)
{
    int s = 1 + stage();
    float sway = sinf(millis() / 700.0f) * (pet.feral ? 2.5f : 1.0f);
    cx += (int)sway;
    d->setColor(WHITE);
    int bodyW = 14 + s * 6, bodyH = 6 + s * 2;
    int by = cy - bodyH / 2;
    d->fillRect(cx - bodyW / 2, by, bodyW, bodyH);
    int legH = 8 + s * 3;
    for (int i = 0; i < 4; i++) {
        int lx = cx - bodyW / 2 + 2 + i * (bodyW - 4) / 3;
        d->drawVerticalLine(lx, by + bodyH, legH);
    }
    int hx = cx + bodyW / 2, hy = by + (pet.feral ? -4 - s : 0);
    d->drawLine(cx + bodyW / 2 - 2, by + 1, hx + 4, hy - 2);
    int headR = 2 + s;
    d->fillCircle(hx + 5 + headR, hy - 2 - headR, headR);
    d->drawLine(hx + 5 + headR, hy - 2 - 2 * headR, hx + 4 + headR, hy - 4 - 2 * headR);
    d->drawLine(hx + 5 + 2 * headR, hy - 2 - 2 * headR, hx + 6 + 2 * headR, hy - 4 - 2 * headR);
    d->drawLine(cx - bodyW / 2, by + 1, cx - bodyW / 2 - 4 - s, by - 2 - s);
    int ex = hx + 5 + headR, ey = hy - 2 - headR;
    d->setColor(BLACK);
    d->fillRect(ex - 1, ey - 1, headR, 2);
    d->setColor(WHITE);
    if (pet.feral || (millis() / 400) % 6) {
        d->setPixel(ex + headR / 2, ey);
        d->setPixel(ex + headR / 2 - 2, ey);
    }
}

void skinPetDraw(OLEDDisplay *d, int16_t x, int16_t y)
{
    load();
    int lh = fontHeight(FONT_SMALL), W = d->width(), H = d->height();
    char title[40];
    snprintf(title, sizeof(title), "%s (%s)", stageName(), moodName());
    drawTitle(d, x, y, title, "5 feed #play");
    petCreature(d, x + W / 2, y + H / 2);
    petBar(d, x + 2, y + lh + 2, "Fed", pet.fullness);
    petBar(d, x + 2, y + lh + 2 + 10, "Nrg", pet.energy);
    petBar(d, x + 2, y + lh + 2 + 20, "Bnd", pet.bond);
    d->setFont(FONT_SMALL);
    d->setTextAlignment(TEXT_ALIGN_RIGHT);
    char age[20];
    uint32_t days = (nowSec() - pet.bornSec) / 86400;
    snprintf(age, sizeof(age), "%u%s old", (unsigned)(days ? days : (nowSec() - pet.bornSec) / 3600), days ? "d" : "h");
    d->drawString(x + W - 2, y + lh + 2, age);
    if (note[0]) {
        d->setColor(BLACK);
        d->fillRect(x, y + H - lh - 1, W, lh + 1);
        d->setColor(WHITE);
        drawCentered(d, x, y + H - lh - 1, note, FONT_SMALL);
    } else {
        drawCentered(d, x, y + H - lh - 1, "feeds on badges met & finds", FONT_SMALL);
    }
}

class SkinwalkerApp : public App
{
  public:
    bool numberShortcuts() const override { return true; }
    const char *name() const override { return "Skinwalker"; }
    uint32_t frameMs() const override { return 120; }
    void begin() override
    {
        load();
        catchUp();
        note[0] = 0;
    }
    void end() override { save(); }
    void tick(uint32_t now) override { skinPetTick(now); }
    void key(int k, KeyAction a) override
    {
        if (k == AK_STAR && a == KA_RELEASE) {
            appClose();
            return;
        }
        if (skinPetKey(k, a))
            appRedraw();
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override { skinPetDraw(d, x, y); }
};

static SkinwalkerApp &skinApp()
{
    static SkinwalkerApp *p = psramNew<SkinwalkerApp>();
    return *p;
}

void openSkinwalkerPet()
{
    appOpen(&skinApp());
}

} // namespace badge

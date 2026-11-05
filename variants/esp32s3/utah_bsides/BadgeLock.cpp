#include "configuration.h"

#include "BadgeLock.h"

#include "BadgeApp.h"
#include "BadgePortal.h"
#include "BadgeApps.h"
#include "BadgeHome.h"
#include "BadgeTime.h"
#include "BadgeMenu.h"
#include "gps/RTC.h"
#include "NodeDB.h"
#include <time.h>

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "graphics/draw/NotificationRenderer.h"
#include "mesh/Throttle.h"
#include <Arduino.h>
#include <Preferences.h>
#include <SHA256.h>
#include <esp_random.h>
#include <stdio.h>
#include <string.h>

extern uint32_t serialSinceMsec;
namespace graphics
{
extern uint32_t logo_timeout;
extern bool hasUnreadMessage;
}
namespace badge
{

static const uint32_t IDLE_MS = 5 * 60 * 1000;
static const int MIN_PIN = 4, MAX_PIN = 8;

// ---- the PIN: only a salted SHA-256 of it is kept (NVS "utah": pinsalt, pinhash, pinlen) ----
static int pinLen = -1; // -1 not read yet, 0 no PIN
static uint8_t salt[16], hash[32];
static uint8_t fails = 0;
static bool locked = false; // only the right PIN clears it
static bool attract = false; // no PIN: the lock screen is just an attract loop, any key -> home
static bool bootLockHeld = true; // hold off the auto-lock until the boot intro has played once
class App;
static App *beforeLock = nullptr; // the screen up when we locked, to return to on unlock

static void load()
{
    if (pinLen >= 0)
        return;
    pinLen = 0;
    Preferences p;
    if (!p.begin("utah", true))
        return;
    if (p.getBytes("pinsalt", salt, 16) == 16 && p.getBytes("pinhash", hash, 32) == 32)
        pinLen = p.getUChar("pinlen", 0);
    fails = p.getUChar("pinfail", 0);
    p.end();
}

static void digest(const char *pin, uint8_t out[32])
{
    SHA256 h;
    h.update(salt, sizeof(salt));
    h.update(pin, strlen(pin));
    h.finalize(out, 32);
}

static void saveFails()
{
    Preferences p;
    if (p.begin("utah", false)) {
        p.putUChar("pinfail", fails);
        p.end();
    }
}

static void setPin(const char *pin)
{
    Preferences p;
    if (!p.begin("utah", false))
        return;
    if (!pin || !*pin) {
        p.remove("pinsalt");
        p.remove("pinhash");
        p.remove("pinlen");
        pinLen = 0;
    } else {
        esp_fill_random(salt, sizeof(salt));
        digest(pin, hash);
        p.putBytes("pinsalt", salt, 16);
        p.putBytes("pinhash", hash, 32);
        p.putUChar("pinlen", (uint8_t)strlen(pin));
        pinLen = strlen(pin);
    }
    fails = 0;
    p.putUChar("pinfail", 0);
    p.end();
}

static bool checkPin(const char *pin)
{
    uint8_t h[32];
    digest(pin, h);
    uint8_t diff = 0;
    for (int i = 0; i < 32; i++)
        diff |= h[i] ^ hash[i];
    return !diff;
}

bool lockEnabled()
{
    load();
    return pinLen > 0;
}

// wrong PINs: free tries, then a growing wait (kept across restarts)
static uint32_t waitMsFor(int f)
{
    if (f < 5)
        return 0;
    uint32_t s = 30u << (f - 5 < 4 ? f - 5 : 4); // 30 s, 1, 2, 4, 8 min
    return s * 1000;
}

// ---- what the lock screen shows: the name tag's name ("@tag") or one of its pictures ----
const char *lockPicture()
{
    static char what[25];
    Preferences p;
    strcpy(what, "@cycle"); // default: cycle the name (if set) and every picture/GIF
    if (p.begin("utah", true)) {
        if (p.isKey("lockpic"))
            p.getString("lockpic", what, sizeof(what));
        p.end();
    }
    return what;
}

void lockSetPicture(const char *what)
{
    Preferences p;
    if (p.begin("utah", false)) {
        p.putString("lockpic", what && *what ? what : "@tag");
        p.end();
    }
}

// ---- the lock screen: the name tag (name or picture), and a PIN line once a key is pressed ----
class LockApp : public App
{
  public:
    const char *name() const override { return "Locked"; }
    bool reclaimScreen() const override { return false; } // lockPoll handles reclaim at a safe cadence
    uint32_t frameMs() const override
    {
        uint32_t f = view ? view->frameMs() : 1000;
        return f < 1000 ? f : 1000;
    }
    bool keepAwake() const override { return clock ? menuTagAwake() : view && view->keepAwake(); } // "Name tag stays on"
    void begin() override
    {
        entry[0] = 0;
        msg = nullptr;
        barUntil = 0;
        waitFrom = millis();
        clock = !strcmp(lockPicture(), "@clock");
        view = clock ? nullptr : nametagView(lockPicture());
        if (view)
            view->begin();
    }
    void end() override
    {
        if (view)
            view->end();
        view = nullptr;
    }
    void tick(uint32_t now) override
    {
        if (view)
            view->tick(now);
    }
    void key(int k, KeyAction a) override
    {
        if (a != KA_PRESS && a != KA_RELEASE)
            return;
        if (attract) { // no PIN: the first key press leaves the attract loop for home
            if (a == KA_PRESS) {
                attract = false;
                appClose();
                homeGo();
            }
            return;
        }
        bool asking = entry[0] || (int32_t)(millis() - barUntil) < 0;
        if (!asking) { // the first key only brings up the PIN request; it isn't a digit
            if (a == KA_PRESS)
                barUntil = millis() + 6000;
            return;
        }
        barUntil = millis() + 6000;
        int dg = appKeyDigit(k);
        uint32_t wait = waitMsFor(fails);
        if (wait && Throttle::isWithinTimespanMs(waitFrom, wait))
            return; // still waiting after too many wrong PINs
        if (dg >= 0 && a == KA_PRESS) {
            size_t n = strlen(entry);
            if (n < (size_t)MAX_PIN) {
                entry[n] = (char)('0' + dg);
                entry[n + 1] = 0;
                msg = nullptr;
            }
            if ((int)strlen(entry) == pinLen)
                attempt();
        } else if (k == AK_STAR && a == KA_RELEASE) {
            size_t n = strlen(entry);
            if (n)
                entry[n - 1] = 0;
        } else if (k == AK_HASH && a == KA_RELEASE && entry[0]) {
            attempt();
        }
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        if (view)
            view->draw(d, x, y);
        else
            drawClock(d, x, y);
        if (portalRunning()) // Badge WiFi is still sharing while locked
            drawWifiChip(d, x, y);
        if (!entry[0] && (int32_t)(millis() - barUntil) >= 0)
            return;
        char b[40];
        uint32_t wait = waitMsFor(fails), gone = millis() - waitFrom;
        if (wait && gone < wait)
            snprintf(b, sizeof(b), "Wrong PIN: wait %us", (unsigned)((wait - gone + 999) / 1000));
        else if (msg)
            snprintf(b, sizeof(b), "%s", msg);
        else if (entry[0]) {
            size_t n = strlen(entry), i = 0;
            strcpy(b, "PIN: ");
            for (; i < n; i++)
                strcat(b, "* ");
        } else
            snprintf(b, sizeof(b), "Locked: type PIN");
        int lh = fontHeight(FONT_SMALL), by = y + d->height() - lh - 3;
        d->setColor(BLACK);
        d->fillRect(x, by - 1, d->width(), lh + 4);
        d->setColor(WHITE);
        d->drawHorizontalLine(x, by - 2, d->width());
        d->setFont(FONT_SMALL);
        d->setTextAlignment(TEXT_ALIGN_CENTER);
        d->drawString(x + d->width() / 2, by, b);
        d->setTextAlignment(TEXT_ALIGN_LEFT);
    }

  private:
    App *view = nullptr;
    bool clock = false;

    // the clock lock screen: the time big in the name font, the date, your name, new message
    void drawClock(OLEDDisplay *d, int16_t x, int16_t y)
    {
        int W = d->width(), H = d->height(), lh = fontHeight(FONT_SMALL);
        char t[16], date[32];
        bool known = timeKnown();
        if (known) {
            time_t now = getValidTime(RTCQualityFromNet, true); // local time
            struct tm tm;
            gmtime_r(&now, &tm);
            if (config.display.use_12h_clock) {
                int h = tm.tm_hour % 12;
                snprintf(t, sizeof(t), "%d:%02d", h ? h : 12, tm.tm_min);
            } else {
                strftime(t, sizeof(t), "%H:%M", &tm);
            }
            strftime(date, sizeof(date), "%A %e %B", &tm);
        } else {
            strcpy(t, "--:--");
            strcpy(date, "Time unknown");
        }
        drawCentered(d, x, y + 2, nametagName(), FONT_SMALL);
        const uint8_t *big = nametagStyleFont(0);
        d->setFont(big);
        if (d->getStringWidth(t) > W - 4)
            big = nametagStyleFont(1);
        int bh = fontHeight(big), ty = y + (H - bh) / 2 - lh / 2;
        drawCentered(d, x, ty, t, big);
        d->setFont(FONT_SMALL);
        if (d->getStringWidth(date) > W - 4 && known) { // short form for long day names
            time_t now = getValidTime(RTCQualityFromNet, true);
            struct tm tm;
            gmtime_r(&now, &tm);
            strftime(date, sizeof(date), "%a %e %b", &tm);
        }
        drawCentered(d, x, ty + bh + 2, date, FONT_SMALL);
        if (graphics::hasUnreadMessage)
            drawCentered(d, x, y + H - 2 * lh - 2, "New message", FONT_SMALL);
    }
    char entry[MAX_PIN + 1] = {};
    const char *msg = nullptr;
    uint32_t waitFrom = 0, barUntil = 0;

    void attempt()
    {
        bool ok = checkPin(entry);
        entry[0] = 0;
        if (ok) {
            if (fails) {
                fails = 0;
                saveFails();
            }
            locked = false;
            App *restore = beforeLock;
            beforeLock = nullptr;
            appClose();
            if (restore)
                appOpen(restore); // back to the page you were on when it locked
            else
                homeGo(); // unlocked at boot (nothing to return to): the tile home
            return;
        }
        if (fails < 255)
            fails++;
        saveFails();
        waitFrom = millis();
        msg = "Wrong PIN";
    }
};

static LockApp lockApp;

bool lockShowing()
{
    return appActive() == &lockApp;
}

bool lockLocked()
{
    return locked;
}

bool lockBlocks(const App *app)
{
    return locked && app != &lockApp;
}

bool lockBootReady()
{
    return millis() + 300 >= graphics::logo_timeout + serialSinceMsec;
}

void lockBoot()
{
    // Start unlocked so the boot intro (an animated raven, nothing private) can take the screen;
    // BadgeStartup re-locks the moment the intro ends, so the badge is still locked at rest.
    locked = false;
}

void lockNow()
{
    if (!lockEnabled())
        return;
    bootLockHeld = false;
    attract = false;
    locked = true;
    if (lockShowing())
        return;
    App *a = appActive();
    beforeLock = (a && a != &lockApp) ? a : nullptr; // remember where to come back to
    graphics::NotificationRenderer::resetBanner(); // no menu or pop-up left over the lock screen
    appOpen(&lockApp);
}

// no PIN set: still rest on the lock screen (cycling its picture) as an attract loop; any key -> home
void lockAttract()
{
    bootLockHeld = false;
    if (lockEnabled()) {
        lockNow();
        return;
    }
    attract = true;
    locked = false;
    graphics::NotificationRenderer::resetBanner();
    appOpen(&lockApp);
}

// ---- setting the PIN: type it twice -----------------------------------------------------------
class LockSetupApp : public App
{
  public:
    const char *name() const override { return "Screen lock"; }
    void begin() override
    {
        first[0] = entry[0] = 0;
        step = 0;
        msg = nullptr;
    }
    void key(int k, KeyAction a) override
    {
        int dg = appKeyDigit(k);
        if (dg >= 0 && a == KA_PRESS) {
            size_t n = strlen(entry);
            if (n < (size_t)MAX_PIN) {
                entry[n] = (char)('0' + dg);
                entry[n + 1] = 0;
            }
            msg = nullptr;
        } else if (k == AK_STAR && a == KA_RELEASE) {
            size_t n = strlen(entry);
            if (n)
                entry[n - 1] = 0;
            else
                appClose(); // * with nothing typed: leave without changes
        } else if (k == AK_HASH && a == KA_RELEASE) {
            next();
        }
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        drawTitle(d, x, y, "Screen lock PIN", "*del #ok");
        int lh = fontHeight(FONT_SMALL), W = d->width();
        d->setTextAlignment(TEXT_ALIGN_CENTER);
        d->drawString(x + W / 2, y + lh + 8, step ? "Type it again" : "New PIN: 4 to 8 digits");
        d->setFont(FONT_MEDIUM);
        char b[2 * MAX_PIN + 1];
        size_t n = strlen(entry), i = 0;
        for (; i < n; i++)
            b[i * 2] = '*', b[i * 2 + 1] = ' ';
        b[i * 2] = 0;
        d->drawString(x + W / 2, y + 2 * lh + 14, n ? b : "_");
        d->setFont(FONT_SMALL);
        d->drawString(x + W / 2, y + d->height() - 3 * lh - 2, msg ? msg : "Forgot it? Only a full");
        if (!msg)
            d->drawString(x + W / 2, y + d->height() - 2 * lh - 2, "erase and reinstall clears it.");
        d->setTextAlignment(TEXT_ALIGN_LEFT);
    }

  private:
    char first[MAX_PIN + 1] = {}, entry[MAX_PIN + 1] = {};
    int step = 0;
    const char *msg = nullptr;

    void next()
    {
        if ((int)strlen(entry) < MIN_PIN) {
            msg = "At least 4 digits";
            return;
        }
        if (!step) {
            strcpy(first, entry);
            entry[0] = 0;
            step = 1;
            return;
        }
        if (strcmp(first, entry)) {
            msg = "Didn't match: start over";
            first[0] = entry[0] = 0;
            step = 0;
            return;
        }
        setPin(entry);
        first[0] = entry[0] = 0;
        appClose();
        if (screen)
            screen->showSimpleBanner("Screen lock on", 2000);
    }
};

static LockSetupApp lockSetupApp;

void openLockSetup()
{
    appOpen(&lockSetupApp);
}

void lockTurnOff()
{
    setPin("");
    if (screen)
        screen->showSimpleBanner("Screen lock off", 2000);
}

// ---- when to lock ---------------------------------------------------------------------------
void lockPoll(uint32_t keys, uint32_t now)
{
    static uint32_t lastKeys = 0, lastActivity = 0;
    static bool wasOn = true;
    if (keys != lastKeys) {
        lastKeys = keys;
        lastActivity = now;
    }
    bool on = screen && screen->isScreenOn();
    bool slept = wasOn && !on;
    wasOn = on;
    if (!lockEnabled())
        return;
    if (locked) {
        if (!screen || !lockBootReady())
            return; // Meshtastic's boot screens first; the keys are held back meanwhile
        if (bootLockHeld && !lockShowing())
            return; // let the boot intro (BadgeStartup) play once before the lock takes over
        if (!lockShowing()) {
            lockNow();
        } else if (on && now - appLastPaintMs() > 1500) {
            appOpen(&lockApp); // something else (e.g. Bluetooth pairing) took the screen: take it back
        }
        return;
    }
    App *a = appActive();
    bool idle = lastActivity && !Throttle::isWithinTimespanMs(lastActivity, IDLE_MS);
    if (slept || (idle && !(a && !strcmp(a->name(), "Flashlight"))))
        lockNow();
}

} // namespace badge

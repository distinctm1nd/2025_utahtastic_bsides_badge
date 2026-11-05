// Badge home: a tile launcher of the badge's apps, shown on login instead of Meshtastic's page
// carousel. The joystick moves around the grid (left / right and up / down, wrapping top<->bottom),
// # opens a tile.
// Hold the centre press or * for the settings menu. The tile order is set here or on the Badge
// WiFi page. "Mesh pages" opens Meshtastic's own screens; "Clock" shows the time.
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeHome.h"
#include "BadgeLock.h"
#include "BadgeMenu.h"
#include "BadgeTime.h"

#include "gps/RTC.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "mesh/Throttle.h"
#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>


namespace badge
{

static bool homeOn = true;     // the tile home takes over (Settings); else stock Meshtastic
static bool homeWanted = true; // home should reclaim the screen right now
static bool loaded = false;

static void loadPref()
{
    if (loaded)
        return;
    loaded = true;
    Preferences p;
    if (p.begin("utah", true)) {
        homeOn = p.getBool("home", true);
        p.end();
    }
    homeWanted = homeOn;
}

enum Glyph { G_MSG, G_GAME, G_SIGNAL, G_STAR, G_CHIP, G_REMOTE, G_QR, G_TIMER, G_LIGHT, G_DOTS, G_GRID, G_CLOCK, G_GEAR };

void openClock();

struct Tile {
    const char *id;
    const char *label;
    Glyph glyph;
    void (*open)();
};
static const Tile TILES[] = {
    {"messages", "Messages", G_MSG, openMessages},   {"games", "Games", G_GAME, menuOpenGames},
    {"fox", "Fox hunt", G_SIGNAL, menuOpenFox},      {"badges", "Badges", G_STAR, openBadgesApp},
    {"minibadges", "Minibadges", G_CHIP, openMinibadgesApp}, {"remote", "BT remote", G_REMOTE, openBleRemote},
    {"qr", "QR code", G_QR, openQrCode},             {"timer", "Timer", G_TIMER, openTimer},
    {"flashlight", "Flashlight", G_LIGHT, openFlashlight}, {"nodes", "Nodes", G_DOTS, openNodeList},
    {"pages", "Mesh pages", G_GRID, openPagesMenu},  {"clock", "Clock", G_CLOCK, openClock},
    {"settings", "Settings", G_GEAR, menuOpen},
};
static const int NTILES = sizeof(TILES) / sizeof(TILES[0]);

// Display order + show/hide. Saved in NVS ("utah"/"tiles") as "shownIds | hiddenIds" (space
// separated). Tiles not mentioned default to shown (so new tiles appear). The launcher shows
// only the shown tiles, in order; the Badge WiFi page sees all of them with their state.
static uint8_t vis[NTILES], hid[NTILES];
static int nVis = 0, nHid = 0;
static bool orderLoaded = false;

static int tileIndexById(const char *id)
{
    for (int i = 0; i < NTILES; i++)
        if (!strcmp(TILES[i].id, id))
            return i;
    return -1;
}
static void loadOrder()
{
    if (orderLoaded)
        return;
    orderLoaded = true;
    char buf[220] = "";
    Preferences p;
    if (p.begin("utah", true)) {
        p.getString("tiles", buf, sizeof(buf));
        p.end();
    }
    char *bar = strchr(buf, '|'); // everything after '|' is hidden
    if (bar)
        *bar = 0;
    bool placed[NTILES] = {false};
    nVis = nHid = 0;
    for (char *t = strtok(buf, " "); t && nVis < NTILES; t = strtok(nullptr, " ")) {
        int idx = tileIndexById(t);
        if (idx >= 0 && !placed[idx]) {
            vis[nVis++] = (uint8_t)idx;
            placed[idx] = true;
        }
    }
    if (bar)
        for (char *t = strtok(bar + 1, " "); t && nHid < NTILES; t = strtok(nullptr, " ")) {
            int idx = tileIndexById(t);
            if (idx >= 0 && !placed[idx]) {
                hid[nHid++] = (uint8_t)idx;
                placed[idx] = true;
            }
        }
    for (int i = 0; i < NTILES; i++)
        if (!placed[i])
            vis[nVis++] = (uint8_t)i; // unmentioned (e.g. new) tiles: shown
}

// the Badge WiFi page enumerates all tiles: shown ones first (in order), then hidden ones
int tileCount()
{
    loadOrder();
    return NTILES;
}
const char *tileIdAt(int i)
{
    loadOrder();
    if (i < 0 || i >= NTILES)
        return "";
    return i < nVis ? TILES[vis[i]].id : TILES[hid[i - nVis]].id;
}
const char *tileLabelAt(int i)
{
    loadOrder();
    if (i < 0 || i >= NTILES)
        return "";
    return i < nVis ? TILES[vis[i]].label : TILES[hid[i - nVis]].label;
}
bool tileShownAt(int i)
{
    loadOrder();
    return i >= 0 && i < nVis;
}
void tileSetOrder(const char *shownIds, const char *hiddenIds)
{
    char combined[220];
    snprintf(combined, sizeof(combined), "%s | %s", shownIds ? shownIds : "", hiddenIds ? hiddenIds : "");
    Preferences p;
    if (p.begin("utah", false)) {
        p.putString("tiles", combined);
        p.end();
    }
    orderLoaded = false;
    loadOrder();
}

class HomeApp : public App
{
  public:
    const char *name() const override { return "Home"; }
    uint32_t frameMs() const override { return 200; } // repaint often so a stray Meshtastic frame is overwritten fast
    void begin() override { loadOrder(); }
    void key(int k, KeyAction a) override
    {
        if (editing) {
            editKey(k, a);
            return;
        }
        if (a == KA_HOLD && (k == AK_STAR || k == AK_HASH)) { // hold * / centre / # : settings menu
            menuOpen();
            if (k == AK_HASH)
                hashHeld = true; // its release must not then also open a tile
            return;
        }
        // Open the selected tile on the release, not the press: opening on the press lets the
        // matching release leak into the app we just opened (Messages read it as "New chat").
        if (a == KA_RELEASE && (k == AK_HASH || k == AK_5)) {
            if (k == AK_HASH && hashHeld) {
                hashHeld = false;
                return;
            }
            if (nVis) {
                menuClearReturn(); // opened from home: closing the app returns to home, not a menu
                if (TILES[vis[sel]].open)
                    TILES[vis[sel]].open();
            }
            return;
        }
        if (a != KA_PRESS && a != KA_REPEAT)
            return;
        if (sel >= nVis)
            sel = nVis ? nVis - 1 : 0;
        if ((k == AK_RIGHT || k == AK_6) && sel + 1 < nVis)
            sel++;
        else if ((k == AK_LEFT || k == AK_4) && sel > 0)
            sel--;
        else if (k == AK_DOWN || k == AK_8) {
            if (sel + COLS < nVis)
                sel += COLS;
            else
                sel = sel % COLS; // past the bottom: back to the top of this column
        } else if (k == AK_UP || k == AK_2) {
            if (sel - COLS >= 0)
                sel -= COLS;
            else { // above the top: down to the bottom of this column
                sel = sel % COLS;
                while (sel + COLS < nVis)
                    sel += COLS;
            }
        }
        else if (a == KA_PRESS && k == AK_0) // long-press 0 is the shortcut; a tap enters edit
            startEdit();
        appRedraw();
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        if (editing) {
            drawEdit(d, x, y);
            return;
        }
        int lh = fontHeight(FONT_SMALL), W = d->width(), H = d->height();
        drawTitle(d, x, y, "UTAHtastic", "#open  0 arrange");
        int top = y + lh + 3, tileW = (W - 6) / COLS, tileH = lh * 2 + 4, gap = 2;
        int rowsFit = (H - (top - y)) / (tileH + gap);
        if (rowsFit < 1)
            rowsFit = 1;
        if (sel >= nVis)
            sel = nVis ? nVis - 1 : 0;
        if (!nVis) {
            drawCentered(d, x, y + 3 * lh, "No tiles shown", FONT_SMALL);
            drawCentered(d, x, y + 4 * lh + 2, "tap 0 to arrange", FONT_SMALL);
            return;
        }
        int selRow = sel / COLS, firstRow = selRow >= rowsFit ? selRow - rowsFit + 1 : 0;
        for (int i = firstRow * COLS; i < nVis; i++) {
            int row = i / COLS - firstRow, col = i % COLS;
            if (row >= rowsFit)
                break;
            drawCell(d, x + 2 + col * (tileW + 1), top + row * (tileH + gap), tileW, tileH, vis[i], i == sel ? 1 : 0, false);
        }
    }

    // ---- edit mode: arrange + show/hide on the badge ----
    void startEdit()
    {
        loadOrder();
        int n = 0;
        for (int i = 0; i < nVis; i++, n++) {
            ed[n] = vis[i];
            eshown[n] = true;
        }
        for (int i = 0; i < nHid; i++, n++) {
            ed[n] = hid[i];
            eshown[n] = false;
        }
        eSel = 0;
        carrying = false;
        editing = true;
        appRedraw();
    }
    void saveEdit()
    {
        char shown[180] = "", hidden[180] = "";
        for (int i = 0; i < NTILES; i++) {
            char *b = eshown[i] ? shown : hidden;
            size_t used = strlen(b);
            snprintf(b + used, 180 - used, "%s%s", used ? " " : "", TILES[ed[i]].id);
        }
        editing = false;
        tileSetOrder(shown, hidden);
        sel = 0;
        appRedraw();
    }
    void eswap(int a2, int b2)
    {
        uint8_t t = ed[a2];
        ed[a2] = ed[b2];
        ed[b2] = t;
        bool s2 = eshown[a2];
        eshown[a2] = eshown[b2];
        eshown[b2] = s2;
    }
    void editKey(int k, KeyAction a)
    {
        if (k == AK_STAR && a == KA_RELEASE) {
            if (carrying)
                carrying = false;
            else
                saveEdit();
            return;
        }
        if (a != KA_PRESS && a != KA_REPEAT)
            return;
        int target = -1;
        if (k == AK_RIGHT || k == AK_6)
            target = eSel + 1 < NTILES ? eSel + 1 : -1;
        else if (k == AK_LEFT || k == AK_4)
            target = eSel > 0 ? eSel - 1 : -1;
        else if (k == AK_DOWN || k == AK_8)
            target = eSel + COLS < NTILES ? eSel + COLS : -1;
        else if (k == AK_UP || k == AK_2)
            target = eSel - COLS >= 0 ? eSel - COLS : -1;
        if (target >= 0) {
            if (carrying)
                eswap(eSel, target);
            eSel = target;
        } else if (a == KA_PRESS && (k == AK_HASH || k == AK_5)) {
            carrying = !carrying; // pick up / drop
        } else if (a == KA_PRESS && k == AK_0) {
            eshown[eSel] = !eshown[eSel]; // show / hide
        }
        appRedraw();
    }
    void drawEdit(OLEDDisplay *d, int16_t x, int16_t y)
    {
        int lh = fontHeight(FONT_SMALL), W = d->width(), H = d->height();
        drawTitle(d, x, y, "Arrange tiles", carrying ? "move #drop" : "#move 0hide *done");
        int top = y + lh + 3, tileW = (W - 6) / COLS, tileH = lh * 2 + 4, gap = 2;
        int rowsFit = (H - (top - y)) / (tileH + gap);
        if (rowsFit < 1)
            rowsFit = 1;
        int selRow = eSel / COLS, firstRow = selRow >= rowsFit ? selRow - rowsFit + 1 : 0;
        for (int i = firstRow * COLS; i < NTILES; i++) {
            int row = i / COLS - firstRow, col = i % COLS;
            if (row >= rowsFit)
                break;
            int state = i == eSel ? (carrying ? 2 : 1) : 0;
            drawCell(d, x + 2 + col * (tileW + 1), top + row * (tileH + gap), tileW, tileH, ed[i], state, !eshown[i]);
        }
    }

    // one tile cell. state: 0 normal, 1 selected, 2 carried. hidden: struck through.
    void drawCell(OLEDDisplay *d, int tx, int ty, int tileW, int tileH, int tileIdx, int state, bool hidden)
    {
        int lh = fontHeight(FONT_SMALL);
        const Tile &t = TILES[tileIdx];
        bool fill = state == 1;
        d->setColor(WHITE);
        if (fill)
            d->fillRect(tx, ty, tileW, tileH);
        else
            d->drawRect(tx, ty, tileW, tileH);
        if (state == 2) { // carried: double border
            d->drawRect(tx + 1, ty + 1, tileW - 2, tileH - 2);
            d->drawRect(tx + 2, ty + 2, tileW - 4, tileH - 4);
        }
        d->setColor(fill ? BLACK : WHITE);
        drawGlyph(d, t.glyph, tx + 3, ty + tileH / 2 - 4);
        d->setTextAlignment(TEXT_ALIGN_LEFT);
        char lab[16];
        snprintf(lab, sizeof(lab), "%s", t.label);
        int n = strlen(lab), lw = tileW - 16;
        while (n > 1 && d->getStringWidth(lab, n) > lw)
            lab[--n] = 0;
        d->drawString(tx + 14, ty + tileH / 2 - lh / 2, lab);
        if (hidden) // struck through = hidden
            d->drawLine(tx + 2, ty + tileH / 2, tx + tileW - 3, ty + tileH / 2);
        d->setColor(WHITE);
    }

  private:
    int sel = 0;
    static const int COLS = 2;
    bool editing = false, carrying = false;
    bool hashHeld = false;
    uint8_t ed[NTILES];
    bool eshown[NTILES];
    int eSel = 0;

    void drawGlyph(OLEDDisplay *d, Glyph g, int gx, int gy)
    {
        switch (g) {
        case G_MSG:
            d->drawRect(gx, gy, 9, 7);
            d->drawLine(gx, gy, gx + 4, gy + 3);
            d->drawLine(gx + 8, gy, gx + 4, gy + 3);
            break;
        case G_GAME:
            d->drawRect(gx + 2, gy, 2, 8);
            d->drawRect(gx, gy + 3, 8, 2);
            break;
        case G_SIGNAL: // signal waves radiating up-right from the corner dot
            d->fillCircle(gx, gy + 7, 1);
            d->drawCircleQuads(gx, gy + 7, 4, 0b0001);
            d->drawCircleQuads(gx, gy + 7, 7, 0b0001);
            break;
        case G_STAR:
            d->drawLine(gx + 4, gy, gx + 4, gy + 7);
            d->drawLine(gx, gy + 3, gx + 8, gy + 3);
            d->drawLine(gx + 1, gy + 1, gx + 7, gy + 6);
            d->drawLine(gx + 7, gy + 1, gx + 1, gy + 6);
            break;
        case G_CHIP:
            d->drawRect(gx + 1, gy + 1, 6, 6);
            for (int i = 0; i < 3; i++) {
                d->setPixel(gx, gy + 2 + i * 2);
                d->setPixel(gx + 7, gy + 2 + i * 2);
            }
            break;
        case G_REMOTE:
            d->drawRect(gx + 2, gy, 5, 8);
            d->fillCircle(gx + 4, gy + 2, 1);
            break;
        case G_QR:
            d->drawRect(gx, gy, 3, 3);
            d->drawRect(gx + 5, gy, 3, 3);
            d->drawRect(gx, gy + 5, 3, 3);
            d->fillRect(gx + 5, gy + 5, 3, 3);
            break;
        case G_TIMER:
            d->drawCircle(gx + 4, gy + 4, 3);
            d->drawLine(gx + 4, gy, gx + 4, gy + 1);
            d->drawLine(gx + 4, gy + 4, gx + 4, gy + 2);
            break;
        case G_LIGHT:
            d->fillCircle(gx + 4, gy + 4, 2);
            for (int i = 0; i < 8; i++) {
                float an = i * 0.785f;
                d->setPixel(gx + 4 + (int)(4 * cosf(an)), gy + 4 + (int)(4 * sinf(an)));
            }
            break;
        case G_DOTS:
            d->fillCircle(gx + 1, gy + 5, 1);
            d->fillCircle(gx + 4, gy + 2, 1);
            d->fillCircle(gx + 7, gy + 5, 1);
            break;
        case G_GRID:
            d->drawRect(gx, gy, 3, 3);
            d->drawRect(gx + 5, gy, 3, 3);
            d->drawRect(gx, gy + 5, 3, 3);
            d->drawRect(gx + 5, gy + 5, 3, 3);
            break;
        case G_CLOCK:
            d->drawCircle(gx + 4, gy + 4, 3);
            d->drawLine(gx + 4, gy + 4, gx + 4, gy + 2);
            d->drawLine(gx + 4, gy + 4, gx + 6, gy + 4);
            break;
        case G_GEAR:
            d->drawCircle(gx + 4, gy + 4, 3);
            d->fillCircle(gx + 4, gy + 4, 1);
            d->drawLine(gx + 4, gy, gx + 4, gy + 1);
            d->drawLine(gx + 4, gy + 7, gx + 4, gy + 8);
            d->drawLine(gx, gy + 4, gx + 1, gy + 4);
            d->drawLine(gx + 7, gy + 4, gx + 8, gy + 4);
            break;
        }
    }
};

static HomeApp homeApp;

// ---- the Clock tile opens this ------------------------------------------------------------
class ClockApp : public App
{
  public:
    const char *name() const override { return "Clock"; }
    uint32_t frameMs() const override { return 1000; }
    bool keepAwake() const override { return true; }
    void key(int k, KeyAction a) override
    {
        if (k == AK_STAR && a == KA_RELEASE)
            appClose();
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        int lh = fontHeight(FONT_SMALL), H = d->height();
        drawTitle(d, x, y, "Clock", "*back");
        if (!timeKnown()) {
            drawCentered(d, x, y + H / 2 - 8, "Time unknown", FONT_MEDIUM);
            drawCentered(d, x, y + H / 2 + 10, "(set from the app / WiFi)", FONT_SMALL);
            return;
        }
        time_t t = getValidTime(RTCQualityFromNet, true);
        struct tm tm;
        gmtime_r(&t, &tm);
        char hhmm[8], date[24];
        strftime(hhmm, sizeof(hhmm), "%H:%M", &tm);
        strftime(date, sizeof(date), "%a %e %b", &tm);
        drawCentered(d, x, y + lh + 8, hhmm, FONT_LARGE);
        drawCentered(d, x, y + lh + 12 + fontHeight(FONT_LARGE), date, FONT_SMALL);
    }
};
static ClockApp clockApp;
void openClock() { appOpen(&clockApp); }

// ---- home lifecycle -----------------------------------------------------------------------
void openHomeNow()
{
    homeWanted = true;
    appOpen(&homeApp);
}
void openHomeEdit()
{
    homeWanted = true;
    appOpen(&homeApp);
    homeApp.startEdit();
}
void homeGo()
{
    loadPref();
    if (homeOn)
        openHomeNow();
}
void homeYield() { homeWanted = false; }
bool homeActive() { return appActive() == &homeApp; }
bool homeEnabled()
{
    loadPref();
    return homeOn;
}
void homeSetEnabled(bool on)
{
    loadPref();
    homeOn = on;
    Preferences p;
    if (p.begin("utah", false)) {
        p.putBool("home", on);
        p.end();
    }
    homeWanted = on;
    if (on)
        openHomeNow();
}
void homeBoot()
{
    loadPref();
    homeWanted = false; // BadgeStartup shows the intro then the lock/attract; home opens on the first key
}

void homePoll(uint32_t now)
{
    loadPref();
    if (!homeOn || !homeWanted || lockLocked() || !lockBootReady())
        return;
    if (!screen || !screen->isScreenOn() || screen->isOverlayBannerShowing() || menuBusy())
        return;
    (void)now;
    App *a = appActive();
    if (a) // already on a badge screen (appTick re-asserts it if Meshtastic steals the frame)
        return;
    appOpen(&homeApp);
}

} // namespace badge

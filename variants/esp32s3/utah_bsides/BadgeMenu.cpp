// Badge menu (hold *), shortcuts (hold a number) and the boot welcome screen.
//
// Menus use Meshtastic's own option banners (BannerOverlayOptions), so they look like the
// rest of the 2.7 UI and are driven by the same keys: joystick up/down to move, # to choose,
// * to close. A banner callback can't open another banner directly (the renderer clears
// the banner right after the callback), so callbacks queue the next step and menuPoll()
// carries it out on the keypad thread's next tick.
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeCapture.h"
#include "BadgeFollow.h"
#include "BadgeHome.h"
#include "BadgeKeypad.h"
#include "BadgeLeds.h"
#include "BadgeLock.h"
#include "BadgeMenu.h"
#include "BadgeMesh.h"
#include "BadgeMeshCore.h"
#include "BadgeMeshSettings.h"
#include "BadgeScenes.h"
#include "BadgeTime.h"

#include "NodeDB.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "graphics/draw/NotificationRenderer.h"
#include "main.h"
#include "modules/CannedMessageModule.h"
#include <Preferences.h>
#include <stdio.h>
#include <string.h>

extern uint16_t TFT_MESH; // the screen's drawing colour (src/graphics/TFTDisplay.cpp)

namespace badge
{

enum Step {
    S_NONE,
    S_MAIN,
    S_GAMES,
    S_SETTINGS,
    S_COLOUR,
    S_LEDOFF,
    S_RESET,
    S_MCRADIO,
    S_HELP,
    S_FOX_MENU,
    S_FOX_PICK,
    S_FOX_WIFI,
    S_FOX_BLE,
    S_FOX_RF,
    S_FOX_LOG,
    S_PORTAL,
    S_CHIP8,
    S_RADAR,
    S_FLASHLIGHT,
    S_QR,
    S_TIMER,
    S_CAPTURE,
    S_SCANSTATS,
    S_PAGES,
    S_HOME,
    S_MESSAGES,
    S_FLAGGED,
    S_FOX_ALL,
    S_ALERTSET,
    S_LOCKMENU,
    S_LOCKPIC,
    S_TZ,
    S_SHORTCUTS,
    S_SHORTCUT_PICK,
    S_TTT_PICK,
    S_QUICK,
    // apps
    S_NAMETAG,
    S_COMPOSE,
    S_LIGHTS,
    S_SIMON,
    S_WHACK,
    S_SNAKE,
    S_BLOCKS,
    S_PET,
    S_TTT_CPU,
    S_TTT_MESH,
    S_FOX,
    S_NODES,
    S_BADGES,
    S_MINIBADGES,
    S_TAGLINE,
    S_NAME,
    S_JOYSTICK,
    S_TAGFONT,
    S_TAGSIZE,
    S_CHESS,
    S_MINE,
    S_SOLITAIRE,
    S_MAHJONG,
    S_MESHSET,
};
static volatile Step queued = S_NONE;
static bool fromShortcut = false; // the queued step came from a shortcut: no menu to return to
static uint32_t pickedNode = 0;

// going back with *: the badge menu on screen, the last menu (not a node picker) shown, the
// menu an open app returns to, and that return once the app has closed
static const uint32_t MENU_MS = 60UL * 60 * 1000; // menus stay up until closed (an hour)
static Step shownStep = S_NONE;
static const char *shownTitle = nullptr;
static Step lastMenu = S_MAIN;
static Step appParent = S_NONE;
static volatile Step returnTo = S_NONE;
static int mainSel = 1, gamesSel = 1, foxSel = 1, setSel = 1, lockSel = 1; // where each menu reopens

static Step parentOf(Step s)
{
    switch (s) {
    case S_SETTINGS:
    case S_HELP:
    case S_MCRADIO:
        return S_MAIN;
    case S_GAMES:
    case S_FOX_MENU:
        return S_NONE; // reached from the home tiles now: back closes to home
    case S_COLOUR:
    case S_LEDOFF:
    case S_RESET:
    case S_TAGFONT:
    case S_TAGSIZE:
    case S_LOCKMENU:
        return S_SETTINGS;
    case S_LOCKPIC:
        return S_LOCKMENU;
    case S_TZ:
    case S_SHORTCUTS:
        return S_SETTINGS;
    case S_ALERTSET:
        return S_FOX_MENU;
    case S_SHORTCUT_PICK:
        return S_SHORTCUTS;
    case S_FOX_PICK:
        return S_FOX_MENU;
    case S_TTT_PICK:
        return S_GAMES;
    default:
        return S_NONE;
    }
}
static int quickIndex = 0;

static void queue(Step s)
{
    queued = s;
}

// ---- small persistent settings ---------------------------------------------------------------
static int getInt(const char *key, int def)
{
    Preferences p;
    int v = def;
    if (p.begin("utah", true)) {
        v = p.getInt(key, def);
        p.end();
    }
    return v;
}
static void putInt(const char *key, int v)
{
    Preferences p;
    if (p.begin("utah", false)) {
        p.putInt(key, v);
        p.end();
    }
}
static bool welcomeEnabled()
{
    return getInt("welcome", 1) != 0; // same key as the 2025 firmware
}
static bool tagAwake()
{
    Preferences p;
    bool on = true;
    if (p.begin("utah", true)) {
        on = p.getBool("tagawake", true);
        p.end();
    }
    return on;
}
static void setTagAwake(bool on)
{
    Preferences p;
    if (p.begin("utah", false)) {
        p.putBool("tagawake", on);
        p.end();
    }
}

// ---- screen colour ------------------------------------------------------------------------
static const char *COLOUR_NAMES[] = {"Meshtastic green", "White", "Amber", "Red", "Cyan", "Pink", "Purple"};
static const uint32_t COLOUR_RGB[] = {0x67EA94, 0xFFFFFF, 0xFFB000, 0xFF2000, 0x00E0FF, 0xFF60C0, 0xB070FF};
static const int NCOLOURS = sizeof(COLOUR_RGB) / sizeof(COLOUR_RGB[0]);

// redraw=false only sets the colour (for startup, before anything has been drawn in another one)
static void applyColour(int i, bool redraw = true)
{
    if (i < 0 || i >= NCOLOURS)
        i = 0;
    uint32_t c = COLOUR_RGB[i];
    uint8_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    // this ST7735S panel takes its colours blue-first (BGR): send red in the blue bits and
    // blue in the red bits, or red shows as blue and amber as light blue
    TFT_MESH = (uint16_t)(((b & 0xF8) << 8) | ((g & 0xFC) << 3) | (r >> 3));
    // the driver only sends pixels that changed: flash the whole screen once so every lit
    // pixel is sent again in the new colour
    if (redraw && screen && screen->getDisplayDevice()) {
        OLEDDisplay *d = screen->getDisplayDevice();
        d->setColor(WHITE);
        d->fillRect(0, 0, d->width(), d->height());
        d->display();
        d->setColor(BLACK);
        d->fillRect(0, 0, d->width(), d->height());
        d->display();
        d->setColor(WHITE);
        screen->forceDisplay(true);
    }
}
static int colourSetting()
{
    return getInt("scrcol", 0);
}
static int activeColour()
{
    return ledsNightMode() ? 3 : colourSetting(); // night mode: red
}

int menuColourCount()
{
    return NCOLOURS;
}
const char *menuColourName(int i)
{
    return i >= 0 && i < NCOLOURS ? COLOUR_NAMES[i] : "";
}
int menuColour()
{
    return colourSetting();
}
void menuSetColour(int i)
{
    putInt("scrcol", i);
    applyColour(activeColour());
}
bool menuWelcome()
{
    return welcomeEnabled();
}
bool menuTagAwake()
{
    return tagAwake();
}
void menuSetTagAwake(bool on)
{
    setTagAwake(on);
}
void menuSetWelcome(bool on)
{
    putInt("welcome", on ? 1 : 0);
}

// Before Meshtastic draws its first picture (the raven): set the saved screen colour, so that
// picture is already in it and menuInit() has nothing to redraw. Night mode isn't loaded yet
// (BadgeLeds does that later), so read its saved setting directly.
static int earlyColour = -1;
void menuInitEarly()
{
    bool night = false;
    Preferences p;
    if (p.begin("utah", true)) {
        night = p.getBool("night", false);
        p.end();
    }
    earlyColour = night ? 3 : colourSetting();
    applyColour(earlyColour, false);
}

void menuInit()
{
    // wiping and redrawing the screen here would show as the startup raven flashing twice
    applyColour(activeColour(), activeColour() != earlyColour);
}

// ---- option-banner helper ---------------------------------------------------------------------
static void banner(const char *title, const char **items, int count, int selected, void (*cb)(int))
{
    graphics::BannerOverlayOptions o;
    o.message = title;
    o.optionsArrayPtr = items;
    o.optionsCount = (uint8_t)count;
    o.InitialSelected = (int8_t)selected;
    o.bannerCallback = cb;
    o.durationMs = MENU_MS;
    shownTitle = title;
    screen->showOverlayBanner(o);
}

// ---- main menu --------------------------------------------------------------------------------
static void showMain()
{
    // last, in MeshCore mode only: MeshCore's radio setting
    static char mcLabel[40];
    snprintf(mcLabel, sizeof(mcLabel), "MeshCore radio: %g kHz", (double)meshcoreBandwidth());
    static const char *items[] = {
        "Back", "Name tag & pictures", "Lights", "Mesh settings", "Badge WiFi (phone)", "Settings", "Key help", mcLabel};
    static const Step go[] = {S_NONE, S_NAMETAG, S_LIGHTS, S_MESHSET, S_PORTAL, S_SETTINGS, S_HELP, S_MCRADIO};
    banner("Settings & setup", items, meshcoreMode() ? 8 : 7, mainSel, [](int sel) -> void {
        if (sel > 0 && sel < 8) {
            mainSel = sel;
            setSel = 1; // Settings opened from here starts at the top
            queue(go[sel]);
        }
    });
}

static void showGames()
{
    static const char *items[] = {
        "Back",  "Skinwalker Mine (3D)", "CHIP-8 arcade", "Chess",  "Solitaire",          "Mahjong",
        "Simon", "Whack-a-mole",         "Snake",         "Blocks", "Tic-tac-toe: badge", "Tic-tac-toe: vs CPU"};
    static const Step go[] = {S_MAIN,  S_MINE,  S_CHIP8, S_CHESS,  S_SOLITAIRE, S_MAHJONG,
                              S_SIMON, S_WHACK, S_SNAKE, S_BLOCKS, S_TTT_PICK,  S_TTT_CPU};
    banner("Games", items, 12, gamesSel, [](int sel) -> void {
        if (sel < 0 || sel >= 12)
            return;
        if (sel > 0)
            gamesSel = sel;
        queue(go[sel]);
    });
}

static void showFox()
{
    // Short labels on purpose: the overlay box grows to its widest option, so long labels make it
    // span the whole screen and stop looking like a popup over the tiles.
    static const char *BG[] = {"off", "Bluetooth", "BT+WiFi"};
    static char background[40], fox[40], cap[40];
    snprintf(background, sizeof(background), "Background: %s", BG[watchBackground() % 3]);
    snprintf(fox, sizeof(fox), "Be the fox: %s", meshBeingFox() ? "ON" : "off");
    snprintf(cap, sizeof(cap), "Packet capture%s", captureOn() ? " (on)" : "");
    // Grouped: scanners first, then logs / watch / lists, then settings last. background stays at
    // index 11 and fox at 12 so the toggle handling in the callback below still lines up.
    static const char *items[] = {"Back",
                                  // scanners
                                  "LoRa node", "WiFi network", "Bluetooth device", "WiFi + Bluetooth", "Sub-GHz scanner",
                                  "Node radar", cap,
                                  // logs / watch / lists
                                  "Flagged devices", "Sightings log", "Scan stats", background,
                                  // settings
                                  fox, "Alert settings"};
    static const Step go[] = {S_MAIN,    S_NODES,   S_FOX_WIFI, S_FOX_BLE,   S_FOX_ALL,  S_FOX_RF,   S_RADAR,
                              S_CAPTURE, S_FLAGGED, S_FOX_LOG,  S_SCANSTATS, S_FOX_MENU, S_FOX_MENU, S_ALERTSET};
    banner("Fox hunt", items, 14, foxSel, [](int sel) -> void {
        if (sel < 0 || sel >= 14)
            return;
        if (sel == 11)
            watchSetBackground((watchBackground() + 1) % 3);
        else if (sel == 12)
            meshSetBeingFox(!meshBeingFox());
        if (sel > 0)
            foxSel = sel;
        queue(go[sel]);
    });
}

enum SetItem {
    SI_BACK,
    SI_JOYSTICK,
    SI_SHORTCUTS,
    SI_LOCK,
    SI_TZ,
    SI_NAME,
    SI_TAGFONT,
    SI_TAGSIZE,
    SI_TAGSCROLL,
    SI_TAGLINE,
    SI_TAGAWAKE,
    SI_COLOUR,
    SI_LEDOFF,
    SI_NIGHT,
    SI_REMOTE,
    SI_WELCOME,
    SI_HOME,
    SI_TILES,
    SI_RESET,
    SI_COUNT
};
static char lbl[SI_COUNT][40];
static const char *setItems[SI_COUNT];

static void showSettings()
{
    static const char *OFFMODE[] = {"stay on", "dim", "off"};
    snprintf(lbl[SI_BACK], 40, "Back");
    snprintf(lbl[SI_JOYSTICK], 40, "Joystick setup");
    snprintf(lbl[SI_SHORTCUTS], 40, "Shortcuts (hold a number)");
    snprintf(lbl[SI_LOCK], 40, "Screen lock (PIN): %s", lockEnabled() ? "on" : "off");
    snprintf(lbl[SI_TZ], 40, "Time zone: %s", timeZoneCurrentLabel());
    snprintf(lbl[SI_NAME], 40, "Name tag: your name");
    snprintf(lbl[SI_TAGFONT], 40, "Name font: %s", nametagFontName(nametagFont()));
    snprintf(lbl[SI_TAGSIZE], 40, "Name size: %s", nametagSizeName(nametagSize()));
    snprintf(lbl[SI_TAGSCROLL], 40, "Long names: %s", nametagScroll() ? "scroll" : "two lines");
    snprintf(lbl[SI_COLOUR], 40, "Screen: %s", COLOUR_NAMES[colourSetting() % NCOLOURS]);
    snprintf(lbl[SI_LEDOFF], 40, "LEDs, screen asleep: %s", OFFMODE[ledsOffMode() % 3]);
    snprintf(lbl[SI_NIGHT], 40, "Night mode: %s", ledsNightMode() ? "on" : "off");
    snprintf(lbl[SI_TAGAWAKE], 40, "Name tag stays on: %s", tagAwake() ? "yes" : "no");
    snprintf(lbl[SI_TAGLINE], 40, "Name tag: tagline");
    snprintf(lbl[SI_REMOTE], 40, "!commands by DM: %s", meshRemoteCommands() ? "on" : "off");
    snprintf(lbl[SI_WELCOME], 40, "Welcome tips: %s", welcomeEnabled() ? "on" : "off");
    snprintf(lbl[SI_HOME], 40, "Home screen: %s", homeEnabled() ? "badge" : "Meshtastic");
    snprintf(lbl[SI_TILES], 40, "Arrange home tiles");
    snprintf(lbl[SI_RESET], 40, "Factory reset");
    for (int i = 0; i < SI_COUNT; i++)
        setItems[i] = lbl[i];
    banner("Settings", setItems, SI_COUNT, setSel, [](int sel) -> void {
        if (sel > 0)
            setSel = sel; // after a change it reopens on the same item
        switch (sel) {
        case SI_BACK:
            queue(S_MAIN);
            break;
        case SI_COLOUR:
            queue(S_COLOUR);
            break;
        case SI_LEDOFF:
            queue(S_LEDOFF);
            break;
        case SI_NIGHT:
            ledsSetNightMode(!ledsNightMode());
            applyColour(activeColour());
            queue(S_SETTINGS);
            break;
        case SI_TAGAWAKE:
            setTagAwake(!tagAwake());
            queue(S_SETTINGS);
            break;
        case SI_TAGLINE:
            queue(S_TAGLINE);
            break;
        case SI_NAME:
            queue(S_NAME);
            break;
        case SI_TAGFONT:
            queue(S_TAGFONT);
            break;
        case SI_TAGSIZE:
            queue(S_TAGSIZE);
            break;
        case SI_TAGSCROLL:
            nametagSetScroll(!nametagScroll());
            queue(S_SETTINGS);
            break;
        case SI_JOYSTICK:
            queue(S_JOYSTICK);
            break;
        case SI_TZ:
            queue(S_TZ);
            break;
        case SI_SHORTCUTS:
            queue(S_SHORTCUTS);
            break;
        case SI_LOCK:
            if (lockEnabled())
                queue(S_LOCKMENU);
            else
                openLockSetup();
            break;
        case SI_REMOTE:
            meshSetRemoteCommands(!meshRemoteCommands());
            queue(S_SETTINGS);
            break;
        case SI_HOME:
            homeSetEnabled(!homeEnabled());
            queue(S_SETTINGS);
            break;
        case SI_TILES:
            openHomeEdit();
            break;
        case SI_WELCOME:
            putInt("welcome", welcomeEnabled() ? 0 : 1);
            queue(S_SETTINGS);
            break;
        case SI_RESET:
            queue(S_RESET);
            break;
        default:
            break;
        }
    });
}

// "05_skinwalker" -> "skinwalker", "@tag" -> "Your name"
static void choiceLabel(const char *what, char *out, size_t n)
{
    if (!strcmp(what, "@tag")) {
        snprintf(out, n, "Your name");
        return;
    }
    if (!strcmp(what, "@clock")) {
        snprintf(out, n, "Clock and date");
        return;
    }
    if (!strcmp(what, "@cycle")) {
        snprintf(out, n, "Cycle: name + pictures");
        return;
    }
    if (!strcmp(what, "@cyclepics")) {
        snprintf(out, n, "Cycle: pictures only");
        return;
    }
    if (!strcmp(what, "@cyclefavs")) {
        int f = nametagFavoriteCount();
        if (f)
            snprintf(out, n, "Cycle: favorites (%d)", f);
        else
            snprintf(out, n, "Cycle: favorites (none)");
        return;
    }
    const char *p = what;
    if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9' && p[2] == '_')
        p += 3;
    snprintf(out, n, "%s", p);
    for (char *c = out; *c; c++)
        if (*c == '_')
            *c = ' ';
}

// what the lock screen can show: cycling, then the name, then each picture
static const char *lockChoice(int i)
{
    return i == 0 ? "@clock" : i == 1 ? "@cycle" : i == 2 ? "@cyclepics" : i == 3 ? "@cyclefavs" : nametagChoice(i - 4);
}

static void showLockPicture()
{
    static char labels[67][26];
    static const char *items[67];
    int n = 4 + nametagChoiceCount();
    if (n > 66)
        n = 66;
    int cur = 0;
    snprintf(labels[0], 26, "Back");
    for (int i = 0; i < n; i++) {
        choiceLabel(lockChoice(i), labels[i + 1], 26);
        if (i >= 5 && nametagIsFavorite(lockChoice(i))) // a picture you marked
            snprintf(labels[i + 1] + strlen(labels[i + 1]), 26 - strlen(labels[i + 1]), " *");
        if (!strcmp(lockChoice(i), lockPicture()))
            cur = i;
    }
    for (int i = 0; i <= n; i++)
        items[i] = labels[i];
    banner("Lock screen shows", items, n + 1, cur + 1, [](int sel) -> void {
        if (sel > 0)
            lockSetPicture(lockChoice(sel - 1));
        queue(S_LOCKMENU);
    });
}

// ---- shortcuts: hold # and tap 1-4 on Meshtastic's screens ----------------------------------
enum ScKind { SC_STEP, SC_TOGGLE };
struct Shortcut {
    const char *id; // kept in NVS ("sc1".."sc4"), so the list can be reordered safely
    const char *label;
    Step step;    // SC_STEP: open this
    void (*fn)(); // SC_TOGGLE: do this (and say what happened)
};
static void scSay(const char *what, bool on)
{
    static char b[48];
    snprintf(b, sizeof(b), "%s: %s", what, on ? "on" : "off");
    screen->showSimpleBanner(b, 1500);
}
static const char *const WATCH_MODES[] = {"off", "Bluetooth", "Bluetooth + WiFi"};
static const Shortcut SHORTCUTS[] = {
    {"none", "(nothing)", S_NONE, nullptr},
    {"log", "Sightings log (marked)", S_FOX_LOG, nullptr},
    {"flagged", "Flagged devices", S_FLAGGED, nullptr},
    {"ble", "Bluetooth scan", S_FOX_BLE, nullptr},
    {"wifi", "WiFi scan", S_FOX_WIFI, nullptr},
    {"both", "WiFi + Bluetooth scan", S_FOX_ALL, nullptr},
    {"lora", "LoRa fox hunt", S_NODES, nullptr},
    {"radar", "Node radar", S_RADAR, nullptr},
    {"rf", "Sub-GHz scanner", S_FOX_RF, nullptr},
    {"fox", "Fox hunt menu", S_FOX_MENU, nullptr},
    {"watch", "Background watch: next", S_NONE,
     [] {
         watchSetBackground((watchBackground() + 1) % 3);
         static char b[48];
         snprintf(b, sizeof(b), "Background watch:\n%s", WATCH_MODES[watchBackground() % 3]);
         screen->showSimpleBanner(b, 1500);
     }},
    {"alerts", "Scan alerts on/off", S_NONE,
     [] {
         watchSetAlerts(!watchAlerts());
         scSay("Scan alerts", watchAlerts());
     }},
    {"cap", "Packet capture", S_CAPTURE, nullptr},
    {"stats", "Scan stats", S_SCANSTATS, nullptr},
    {"befox", "Be the fox on/off", S_NONE,
     [] {
         meshSetBeingFox(!meshBeingFox());
         scSay("Be the fox", meshBeingFox());
     }},
    {"msgs", "Messages", S_MESSAGES, nullptr},
    {"msg", "New message", S_COMPOSE, nullptr},
    {"tag", "Name tag", S_NAMETAG, nullptr},
    {"web", "Badge WiFi", S_PORTAL, nullptr},
    {"lights", "Lights", S_LIGHTS, nullptr},
    {"scene", "Next LED scene", S_NONE, [] { ledsNextScene(); }},
    {"night", "Night mode on/off", S_NONE,
     [] {
         ledsSetNightMode(!ledsNightMode());
         applyColour(activeColour());
         scSay("Night mode", ledsNightMode());
     }},
    {"flash", "Flashlight", S_FLASHLIGHT, nullptr},
    {"qr", "QR code", S_QR, nullptr},
    {"timer", "Stopwatch & timer", S_TIMER, nullptr},
    {"pages", "Meshtastic pages", S_PAGES, nullptr},
    {"home", "Home (tiles)", S_HOME, nullptr},
    {"lock", "Lock now", S_NONE, [] { lockEnabled() ? lockNow() : lockAttract(); }},
    {"games", "Games", S_GAMES, nullptr},
    {"chip8", "CHIP-8 arcade", S_CHIP8, nullptr},
    {"mine", "Skinwalker Mine", S_MINE, nullptr},
    {"badges", "Badges & achievements", S_BADGES, nullptr},
    {"meshset", "Mesh settings", S_MESHSET, nullptr},
    {"settings", "Settings", S_SETTINGS, nullptr},
    {"menu", "Badge menu", S_MAIN, nullptr},
    {"qm1", "Quick message 1", S_NONE, [] { menuQuickSend(1); }},
    {"qm2", "Quick message 2", S_NONE, [] { menuQuickSend(2); }},
    {"qm3", "Quick message 3", S_NONE, [] { menuQuickSend(3); }},
    {"qm4", "Quick message 4", S_NONE, [] { menuQuickSend(4); }},
    {"qm5", "Quick message 5", S_NONE, [] { menuQuickSend(5); }},
    {"qm6", "Quick message 6", S_NONE, [] { menuQuickSend(6); }},
    {"qm7", "Quick message 7", S_NONE, [] { menuQuickSend(7); }},
    {"qm8", "Quick message 8", S_NONE, [] { menuQuickSend(8); }},
    {"qm9", "Quick message 9", S_NONE, [] { menuQuickSend(9); }},
};
static const int NSHORTCUTS = sizeof(SHORTCUTS) / sizeof(SHORTCUTS[0]);
// hold a number key on Meshtastic's pages: that number's shortcut (NVS "sc0".."sc9")
// Indexed by the digit held (SC_DEFAULTS[n] is the default for "hold n"), so slot 0 is hold-0 and
// slots 1..9 are hold-1..hold-9: 1 flash, 2 log, 3 ble, 4 wifi, 5 radar, 6 msgs, 7 web, 8 lights,
// 9 lock, 0 night. (Kept 1-aligned here on purpose; a 1-based list would make hold-3 open WiFi.)
static const char *const SC_DEFAULTS[10] = {"night", "flash", "log", "ble", "wifi", "radar", "msgs", "web", "lights", "lock"};
static int scSlot = 1; // the number being changed

static int shortcutAt(int n) // n 0..9 -> index in SHORTCUTS
{
    char key[4] = {'s', 'c', (char)('0' + n), 0}, id[12];
    strcpy(id, SC_DEFAULTS[n]);
    Preferences p;
    if (p.begin("utah", true)) {
        if (p.isKey(key))
            p.getString(key, id, sizeof(id));
        p.end();
    }
    for (int i = 0; i < NSHORTCUTS; i++)
        if (!strcmp(SHORTCUTS[i].id, id))
            return i;
    return 0;
}

static void setShortcut(int n, int i)
{
    char key[4] = {'s', 'c', (char)('0' + n), 0};
    Preferences p;
    if (p.begin("utah", false)) {
        p.putString(key, SHORTCUTS[i].id);
        p.end();
    }
}

static bool stepOpensApp(Step s);

void menuShortcut(int n)
{
    if (n < 0 || n > 9 || !screen)
        return;
    const Shortcut &sc = SHORTCUTS[shortcutAt(n)];
    if (sc.fn) {
        sc.fn();
    } else if (sc.step != S_NONE) {
        if (sc.step == S_MAIN)
            mainSel = 1;
        // a menu can't show over a badge screen (Meshtastic pauses pop-ups then): close it first
        if (appActive() && !stepOpensApp(sc.step))
            appClose();
        fromShortcut = true;
        queue(sc.step);
    } else {
        static char b[48];
        snprintf(b, sizeof(b), "Nothing on %d:\nSettings > Shortcuts", n);
        screen->showSimpleBanner(b, 2000);
    }
}

static void showShortcuts()
{
    static char l[10][48];
    static const char *items[11];
    items[0] = "Back";
    for (int i = 1; i <= 10; i++) { // 1..9, then 0
        int n = i % 10;
        snprintf(l[i - 1], sizeof(l[0]), "Hold %d: %s", n, SHORTCUTS[shortcutAt(n)].label);
        items[i] = l[i - 1];
    }
    banner("Shortcuts: hold a number", items, 11, scSlot == 0 ? 10 : scSlot, [](int sel) -> void {
        if (sel >= 1 && sel <= 10) {
            scSlot = sel % 10;
            queue(S_SHORTCUT_PICK);
        } else
            queue(S_SETTINGS);
    });
}

static void showShortcutPick()
{
    static const char *items[NSHORTCUTS + 1];
    static char title[24];
    items[0] = "Back";
    for (int i = 0; i < NSHORTCUTS; i++)
        items[i + 1] = SHORTCUTS[i].label;
    snprintf(title, sizeof(title), "Hold %d does", scSlot);
    banner(title, items, NSHORTCUTS + 1, shortcutAt(scSlot) + 1, [](int sel) -> void {
        if (sel >= 1)
            setShortcut(scSlot, sel - 1);
        queue(S_SHORTCUTS);
    });
}

// ---- Fox hunt > Alert settings ------------------------------------------------------------------
static int alertSel = 1;
static void showAlertSettings()
{
    static const struct {
        int bit;
        const char *name;
    } CATS[] = {{AC_TRACKERS, "Trackers"},  {AC_CAMERAS, "Cameras"},   {AC_SURVEILLANCE, "Surveillance"},
                {AC_POLICE, "Police gear"}, {AC_SKIMMERS, "Skimmers"}, {AC_DRONES, "Drones"},
                {AC_ATTACKS, "Attacks"}};
    static const int NCATS = sizeof(CATS) / sizeof(CATS[0]);
    static char l[7 + NCATS][44];
    static const char *items[7 + NCATS];
    int k = 0;
    snprintf(l[k++], 44, "Back");
    snprintf(l[k++], 44, "Alerts: %s", watchAlerts() ? "on" : "off");
    snprintf(l[k++], 44, "With you after: %d min", watchFollowMin());
    int mr = watchAlertMinRssi();
    if (mr)
        snprintf(l[k++], 44, "Only alert stronger than %d dBm", mr);
    else
        snprintf(l[k++], 44, "Alert at any signal strength");
    int cats = watchAlertCats();
    for (int i = 0; i < NCATS; i++)
        snprintf(l[k++], 44, "%s: %s", CATS[i].name, cats & CATS[i].bit ? "alert" : "don't alert");
    int qs = watchQuietStart();
    if (qs < 0)
        snprintf(l[k++], 44, "Quiet hours: off");
    else
        snprintf(l[k++], 44, "Quiet hours: %02d:00-%02d:00", qs, watchQuietEnd());
    snprintf(l[k++], 44, "Lists show faint devices: %s", watchListFaint() ? "yes" : "no");
    snprintf(l[k++], 44, "Attack scan (bg): %s", watchAttackScan() ? "on" : "off");
    for (int i = 0; i < k; i++)
        items[i] = l[i];
    static int total;
    total = k;
    banner("Alert settings", items, k, alertSel, [](int sel) -> void {
        if (sel <= 0 || sel >= total) {
            queue(S_FOX_MENU);
            return;
        }
        alertSel = sel;
        if (sel == 1) {
            watchSetAlerts(!watchAlerts());
        } else if (sel == 2) {
            int m = watchFollowMin();
            watchSetFollowMin(m == 5 ? 10 : m == 10 ? 20 : 5);
        } else if (sel == 3) {
            int m = watchAlertMinRssi();
            watchSetAlertMinRssi(m == 0 ? -90 : m == -90 ? -80 : m == -80 ? -70 : 0);
        } else if (sel >= 4 && sel < 4 + NCATS) {
            watchSetAlertCats(watchAlertCats() ^ CATS[sel - 4].bit);
        } else if (sel == 4 + NCATS) { // off, 22-7, 23-7, 21-6
            int s = watchQuietStart();
            if (s < 0)
                watchSetQuiet(22, 7);
            else if (s == 22)
                watchSetQuiet(23, 7);
            else if (s == 23)
                watchSetQuiet(21, 6);
            else
                watchSetQuiet(-1, 7);
        } else if (sel == 5 + NCATS) {
            watchSetListFaint(!watchListFaint());
        } else {
            watchSetAttackScan(!watchAttackScan()); // light background WiFi attack sweeps
        }
        queue(S_ALERTSET);
    });
}

static void showTimeZones()
{
    static const char *items[32];
    int n = timeZoneCount(), cur = 0;
    items[0] = "Back";
    for (int i = 0; i < n && i < 31; i++) {
        items[i + 1] = timeZoneLabel(i);
        if (!strcmp(timeZoneLabel(i), timeZoneCurrentLabel()))
            cur = i + 1;
    }
    banner("Time zone", items, n + 1, cur ? cur : 1, [](int sel) -> void {
        if (sel > 0)
            timeSetZone(timeZoneValue(sel - 1));
        queue(S_SETTINGS);
    });
}

static void showLockMenu()
{
    static char shows[40];
    char l[26];
    choiceLabel(lockPicture(), l, sizeof(l));
    snprintf(shows, sizeof(shows), "Shows: %s", l);
    static const char *items[] = {"Back", "Lock now", shows, "Change PIN", "Turn screen lock off"};
    banner("Screen lock", items, 5, lockSel, [](int sel) -> void {
        if (sel > 0)
            lockSel = sel;
        if (sel == 1)
            lockNow();
        else if (sel == 2)
            queue(S_LOCKPIC);
        else if (sel == 3)
            openLockSetup();
        else if (sel == 4) {
            lockTurnOff();
            queue(S_SETTINGS);
        } else
            queue(S_SETTINGS);
    });
}

static void showColours()
{
    static const char *items[NCOLOURS + 1];
    items[0] = "Back";
    for (int i = 0; i < NCOLOURS; i++)
        items[i + 1] = COLOUR_NAMES[i];
    banner("Screen colour", items, NCOLOURS + 1, colourSetting() + 1, [](int sel) -> void {
        if (sel > 0) {
            putInt("scrcol", sel - 1);
            applyColour(activeColour());
        }
        queue(S_SETTINGS);
    });
}

// the name tag opens after a choice so you can see it
static void showTagFont()
{
    openFontPicker(); // shows your name in each font
}

void menuShowSettings()
{
    queue(S_SETTINGS);
}
static void showTagSize()
{
    static const char *items[5];
    items[0] = "Back";
    for (int i = 0; i < 4; i++)
        items[i + 1] = nametagSizeName(i);
    banner("Name size", items, 5, nametagSize() + 1, [](int sel) -> void {
        if (sel > 0) {
            nametagSetSize(sel - 1);
            queue(S_NAMETAG);
        } else {
            queue(S_SETTINGS);
        }
    });
}

static void showLedOff()
{
    static const char *items[] = {"Back", "Stay on", "Dim to 30%", "Off"};
    banner("LEDs while screen sleeps", items, 4, ledsOffMode() + 1, [](int sel) -> void {
        if (sel > 0)
            ledsSetOffMode(sel - 1);
        queue(S_SETTINGS);
    });
}

static void showReset()
{
    static const char *items[] = {"No, keep my settings", "Yes, erase everything"};
    banner("Factory reset?\nErases name, keys,\nchannels, messages", items, 2, 0, [](int sel) -> void {
        if (sel != 1) {
            queue(S_SETTINGS);
            return;
        }
        LOG_INFO("Badge menu: factory reset");
        nodeDB->factoryReset(true);
        rebootAtMsec = (DEFAULT_REBOOT_SECONDS < 0) ? 0 : (millis() + DEFAULT_REBOOT_SECONDS * 1000);
        if (screen)
            screen->showSimpleBanner("Rebooting...", 0);
    });
}

// MeshCore's radio setting: 910.525 MHz, SF7, and one of two bandwidths. Applied at once and saved.
static void showMcRadio()
{
    static const char *items[] = {"Back", "Badges: 125 kHz", "US public: 62.5 kHz"};
    banner("MeshCore radio\n125: any badge\n62.5: public mesh,\nneeds the SX1262 mod", items, 3,
           meshcoreBandwidth() < 100 ? 2 : 1, [](int sel) -> void {
               if (sel != 1 && sel != 2) {
                   queue(S_MAIN);
                   return;
               }
               bool ok = meshcoreSetBandwidth(sel == 2 ? 62.5f : 125.0f);
               if (screen)
                   screen->showSimpleBanner(ok ? (sel == 2 ? "MeshCore: 62.5 kHz" : "MeshCore: 125 kHz")
                                               : "This badge's radio\ncan't do 62.5 kHz\n(stock LLCC68)",
                                            3000);
           });
}

static void showHelp()
{
    shownTitle = "Joystick: move, centre: select\n"
                 "* back, hold centre: menu\n"
                 "Tap a digit: new message\n"
                 "Hold #: next lights";
    screen->showSimpleBanner(shownTitle, 12000);
}

static void pickNode(const char *title, Step next)
{
    static Step after;
    after = next;
    shownTitle = title;
    screen->showNodePicker(title, MENU_MS, [](uint32_t node) -> void {
        if (node && node != meshMyNode()) {
            pickedNode = node;
            queue(after);
        }
    });
}

// a "Quick message N" shortcut: send canned message N (Meshtastic app: Canned Message
// module settings). Asks first, since a slip of the thumb would broadcast it.
void menuQuickSend(int digit)
{
    quickIndex = digit == 0 ? 9 : digit - 1;
    queue(S_QUICK);
}

static void showQuick()
{
    const char *msg = cannedMessageModule ? cannedMessageModule->getMessageByIndex(quickIndex) : "";
    static char title[96];
    if (!msg || !*msg) {
        screen->showSimpleBanner("No canned message there.\nSet them in the app:\nCanned Message module", 4000);
        return;
    }
    static char text[80];
    strncpy(text, msg, sizeof(text) - 1);
    text[sizeof(text) - 1] = 0;
    snprintf(title, sizeof(title), "Send to #%s?\n\"%s\"", meshSendChannelName(), text);
    static const char *items[] = {"Send", "Cancel"};
    banner(title, items, 2, 0, [](int sel) -> void {
        if (sel == 0) {
            meshSendTextDefault(text); // the "Send messages to" channel; no on-screen keyboard
        }
    });
}

void menuOpenGames()
{
    menuClearReturn();
    queue(S_GAMES);
}
void menuOpenFox()
{
    menuClearReturn();
    queue(S_FOX_MENU);
}

void menuOpen()
{
    mainSel = 1;
    queue(S_MAIN);
}

void menuBack()
{
    if (shownStep == S_NONE || !shownTitle || !screen || !screen->isOverlayBannerShowing())
        return;
    if (strcmp(graphics::NotificationRenderer::alertBannerMessage, shownTitle) != 0)
        return; // a Meshtastic menu, not ours
    Step p = parentOf(shownStep);
    shownStep = S_NONE;
    if (p != S_NONE)
        queue(p);
}

void menuAppClosed()
{
    if (appParent != S_NONE)
        returnTo = appParent;
    appParent = S_NONE;
}

void menuClearReturn() // close an app without reopening the menu it came from (e.g. jumping to a page)
{
    appParent = S_NONE;
    returnTo = S_NONE;
}

static bool isAppStep(Step s)
{
    return s >= S_NAMETAG && s != S_TAGFONT && s != S_TAGSIZE && s != S_MESHSET;
}

static bool stepOpensApp(Step s)
{
    return isAppStep(s) || s == S_FOX_WIFI || s == S_FOX_BLE || s == S_FOX_RF || s == S_FOX_LOG || s == S_PORTAL ||
           s == S_CHIP8 || s == S_RADAR || s == S_FLASHLIGHT || s == S_MESSAGES || s == S_FLAGGED || s == S_FOX_ALL ||
           s == S_QR || s == S_TIMER || s == S_CAPTURE || s == S_SCANSTATS || s == S_HOME;
}

bool menuBusy()
{
    return (screen && screen->isOverlayBannerShowing()) || queued != S_NONE || returnTo != S_NONE || meshSettingsBusy();
}

void menuPoll()
{
    meshSettingsPoll();
    if (queued == S_NONE && returnTo != S_NONE && screen && !appActive() && !screen->isOverlayBannerShowing()) {
        queued = returnTo;
        returnTo = S_NONE;
    }
    Step s = queued;
    if (s == S_NONE || !screen)
        return;
    if (screen->isOverlayBannerShowing())
        return; // wait until the previous banner has gone
    queued = S_NONE;
    returnTo = S_NONE;
    bool shortcut = fromShortcut;
    fromShortcut = false;
    if (stepOpensApp(s)) {
        appParent = shortcut ? S_NONE : lastMenu;
    } else if (s != S_QUICK && s != S_MESHSET) {
        shownStep = s;
        if (s != S_FOX_PICK && s != S_TTT_PICK && s != S_HELP)
            lastMenu = s;
    }
    switch (s) {
    case S_MAIN:
        showMain();
        break;
    case S_GAMES:
        showGames();
        break;
    case S_SETTINGS:
        showSettings();
        break;
    case S_COLOUR:
        showColours();
        break;
    case S_LEDOFF:
        showLedOff();
        break;
    case S_RESET:
        showReset();
        break;
    case S_MCRADIO:
        showMcRadio();
        break;
    case S_HELP:
        showHelp();
        break;
    case S_QUICK:
        showQuick();
        break;
    case S_FOX_MENU:
        showFox();
        break;
    case S_FOX_PICK:
        pickNode("Fox: pick a node", S_FOX);
        break;
    case S_FOX_WIFI:
        openRadioHunt(false);
        break;
    case S_FOX_BLE:
        openRadioHunt(true);
        break;
    case S_FOX_ALL:
        openEverythingHunt();
        break;
    case S_FOX_RF:
        openRfScanner();
        break;
    case S_FOX_LOG:
        openSightingsLog();
        break;
    case S_PORTAL:
        openBadgeWifi();
        break;
    case S_CHIP8:
        openChip8();
        break;
    case S_RADAR:
        openRadar();
        break;
    case S_FLASHLIGHT:
        openFlashlight();
        break;
    case S_QR:
        openQrCode();
        break;
    case S_TIMER:
        openTimer();
        break;
    case S_CAPTURE:
        openCaptureApp();
        break;
    case S_SCANSTATS:
        openScanStats();
        break;
    case S_PAGES:
        openPagesMenu();
        break;
    case S_HOME:
        homeGo();
        break;
    case S_MESSAGES:
        openMessages();
        break;
    case S_FLAGGED:
        openFlagged();
        break;
    case S_ALERTSET:
        showAlertSettings();
        break;
    case S_LOCKMENU:
        showLockMenu();
        break;
    case S_LOCKPIC:
        showLockPicture();
        break;
    case S_TZ:
        showTimeZones();
        break;
    case S_SHORTCUTS:
        showShortcuts();
        break;
    case S_SHORTCUT_PICK:
        showShortcutPick();
        break;
    case S_TTT_PICK:
        pickNode("Play who?", S_TTT_MESH);
        break;
    case S_NAMETAG:
        openNameTag();
        break;
    case S_COMPOSE:
        openCompose(-1);
        break;
    case S_LIGHTS:
        openLightsApp();
        break;
    case S_SIMON:
        openSimon();
        break;
    case S_WHACK:
        openWhack();
        break;
    case S_SNAKE:
        openSnake();
        break;
    case S_BLOCKS:
        openTetris();
        break;
    case S_TTT_CPU:
        openTicTacToeCpu();
        break;
    case S_TTT_MESH:
        openTicTacToeMesh(pickedNode, true);
        break;
    case S_FOX:
        openFoxHunt(pickedNode);
        break;
    case S_NODES:
        openNodeList();
        break;
    case S_BADGES:
        openBadgesApp();
        break;
    case S_MINIBADGES:
        openMinibadgesApp();
        break;
    case S_TAGLINE:
        openTaglineEditor();
        break;
    case S_NAME:
        openNameEditor();
        break;
    case S_JOYSTICK:
        openJoystickSetup();
        break;
    case S_TAGFONT:
        showTagFont();
        break;
    case S_CHESS:
        openChess();
        break;
    case S_SOLITAIRE:
        openSolitaire();
        break;
    case S_MAHJONG:
        openMahjong();
        break;
    case S_MINE:
        openMine();
        break;
    case S_PET:
        openSkinwalkerPet();
        break;
    case S_MESHSET:
        meshSettingsOpen();
        break;
    case S_TAGSIZE:
        showTagSize();
        break;
    default:
        break;
    }
}

void menuShowSceneName()
{
    static char msg[48];
    snprintf(msg, sizeof(msg), "Lights:\n%s", sceneAt(ledsScene()).name);
    if (screen && !appActive())
        screen->showSimpleBanner(msg, 1500);
}

// ---- welcome screen -----------------------------------------------------------------------
static bool welcomeUp = false;

static void drawWelcome(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    (void)state;
    static const char *lines[] = {
        "Welcome to BSides!",   "Joystick: move",      "Centre press: select",    "* = back",        "Hold centre: menu",
        "Tap a digit: message", "Hold #: next lights", "Hold a number: shortcut", "(press any key)",
    };
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    int h = FONT_HEIGHT_SMALL;
    int n = sizeof(lines) / sizeof(lines[0]);
    int maxLines = display->height() / h;
    if (n > maxLines)
        n = maxLines;
    for (int i = 0; i < n; i++)
        display->drawString(x + 2, y + i * h, lines[i]);
}

void welcomeShowIfEnabled()
{
    if (!screen || !welcomeEnabled())
        return;
    screen->startAlert(drawWelcome);
    welcomeUp = true;
}

bool welcomeActive()
{
    return welcomeUp;
}

void welcomeClose()
{
    if (welcomeUp && screen)
        screen->endAlert();
    welcomeUp = false;
}

// ---- shortcut registry exposed for the Badge WiFi page ------------------------------------
int shortcutOptionCount()
{
    return NSHORTCUTS;
}
const char *shortcutOptionId(int i)
{
    return (i >= 0 && i < NSHORTCUTS) ? SHORTCUTS[i].id : "";
}
const char *shortcutOptionLabel(int i)
{
    return (i >= 0 && i < NSHORTCUTS) ? SHORTCUTS[i].label : "";
}
const char *shortcutSlotId(int n)
{
    return (n >= 0 && n <= 9) ? SHORTCUTS[shortcutAt(n)].id : "";
}
void shortcutSetSlot(int n, const char *id)
{
    if (n < 0 || n > 9)
        return;
    for (int i = 0; i < NSHORTCUTS; i++)
        if (!strcmp(SHORTCUTS[i].id, id)) {
            setShortcut(n, i);
            return;
        }
}

} // namespace badge

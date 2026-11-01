// Badge menu (hold *), quick-send (hold a digit) and the boot welcome screen.
//
// Menus use Meshtastic's own option banners (BannerOverlayOptions), so they look like the
// rest of the 2.7 UI and are driven by the same keys: joystick up/down to move, # to choose,
// * to close. A banner callback can't open another banner directly (the renderer clears
// the banner right after the callback), so callbacks queue the next step and menuPoll()
// carries it out on the keypad thread's next tick.
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeKeypad.h"
#include "BadgeLeds.h"
#include "BadgeMenu.h"
#include "BadgeMesh.h"
#include "BadgeScenes.h"

#include "NodeDB.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "main.h"
#include "modules/CannedMessageModule.h"
#include <Preferences.h>
#include <stdio.h>
#include <string.h>

extern graphics::Screen *screen;
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
    S_HELP,
    S_FOX_PICK,
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
    S_TTT_CPU,
    S_TTT_MESH,
    S_FOX,
    S_BADGES,
    S_MINIBADGES,
    S_TAGLINE,
    S_NAME,
    S_JOYSTICK,
};
static volatile Step queued = S_NONE;
static uint32_t pickedNode = 0;
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

static void applyColour(int i)
{
    if (i < 0 || i >= NCOLOURS)
        i = 0;
    uint32_t c = COLOUR_RGB[i];
    TFT_MESH = (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c & 0xFF) >> 3)); // RGB565
    // the driver only sends pixels that changed: flash the whole screen once so every lit
    // pixel is sent again in the new colour
    if (screen && screen->getDisplayDevice()) {
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

void menuInit()
{
    applyColour(activeColour());
}

// ---- option-banner helper ---------------------------------------------------------------------
static void banner(const char *title, const char **items, int count, int selected, std::function<void(int)> cb)
{
    graphics::BannerOverlayOptions o;
    o.message = title;
    o.optionsArrayPtr = items;
    o.optionsCount = (uint8_t)count;
    o.InitialSelected = (int8_t)selected;
    o.bannerCallback = cb;
    screen->showOverlayBanner(o);
}

// ---- main menu --------------------------------------------------------------------------------
static void showMain()
{
    static const char *items[] = {"Back",   "Name tag & pictures", "New message", "Lights", "Games",
                                  "Fox hunt", "Badges & achievements", "Minibadges", "Settings", "Key help"};
    static const Step go[] = {S_NONE, S_NAMETAG, S_COMPOSE, S_LIGHTS, S_GAMES, S_FOX_PICK, S_BADGES, S_MINIBADGES, S_SETTINGS, S_HELP};
    banner("BSides Badge", items, 10, 1, [](int sel) -> void {
        if (sel > 0 && sel < 10)
            queue(go[sel]);
    });
}

static void showGames()
{
    static const char *items[] = {"Back", "Simon", "Whack-a-mole", "Snake", "Blocks", "Tic-tac-toe: badge", "Tic-tac-toe: vs CPU"};
    static const Step go[] = {S_MAIN, S_SIMON, S_WHACK, S_SNAKE, S_BLOCKS, S_TTT_PICK, S_TTT_CPU};
    banner("Games", items, 7, 1, [](int sel) -> void { queue(go[sel]); });
}

enum SetItem { SI_BACK, SI_JOYSTICK, SI_NAME, SI_TAGLINE, SI_TAGAWAKE, SI_COLOUR, SI_LEDOFF, SI_NIGHT, SI_REMOTE, SI_WELCOME, SI_RESET, SI_COUNT };
static char lbl[SI_COUNT][40];
static const char *setItems[SI_COUNT];

static void showSettings(int selected = 1)
{
    static const char *OFFMODE[] = {"stay on", "dim", "off"};
    snprintf(lbl[SI_BACK], 40, "Back");
    snprintf(lbl[SI_JOYSTICK], 40, "Joystick setup");
    snprintf(lbl[SI_NAME], 40, "Name tag: your name");
    snprintf(lbl[SI_COLOUR], 40, "Screen: %s", COLOUR_NAMES[colourSetting() % NCOLOURS]);
    snprintf(lbl[SI_LEDOFF], 40, "LEDs, screen asleep: %s", OFFMODE[ledsOffMode() % 3]);
    snprintf(lbl[SI_NIGHT], 40, "Night mode: %s", ledsNightMode() ? "on" : "off");
    snprintf(lbl[SI_TAGAWAKE], 40, "Name tag stays on: %s", tagAwake() ? "yes" : "no");
    snprintf(lbl[SI_TAGLINE], 40, "Name tag: tagline");
    snprintf(lbl[SI_REMOTE], 40, "!commands by DM: %s", meshRemoteCommands() ? "on" : "off");
    snprintf(lbl[SI_WELCOME], 40, "Welcome tips: %s", welcomeEnabled() ? "on" : "off");
    snprintf(lbl[SI_RESET], 40, "Factory reset");
    for (int i = 0; i < SI_COUNT; i++)
        setItems[i] = lbl[i];
    banner("Settings", setItems, SI_COUNT, selected, [](int sel) -> void {
        switch (sel) {
        case SI_BACK: queue(S_MAIN); break;
        case SI_COLOUR: queue(S_COLOUR); break;
        case SI_LEDOFF: queue(S_LEDOFF); break;
        case SI_NIGHT:
            ledsSetNightMode(!ledsNightMode());
            applyColour(activeColour());
            queue(S_SETTINGS);
            break;
        case SI_TAGAWAKE:
            setTagAwake(!tagAwake());
            queue(S_SETTINGS);
            break;
        case SI_TAGLINE: queue(S_TAGLINE); break;
        case SI_NAME: queue(S_NAME); break;
        case SI_JOYSTICK: queue(S_JOYSTICK); break;
        case SI_REMOTE:
            meshSetRemoteCommands(!meshRemoteCommands());
            queue(S_SETTINGS);
            break;
        case SI_WELCOME:
            putInt("welcome", welcomeEnabled() ? 0 : 1);
            queue(S_SETTINGS);
            break;
        case SI_RESET: queue(S_RESET); break;
        default: break;
        }
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

static void showHelp()
{
    screen->showSimpleBanner("Joystick: move, centre: select\n"
                             "* back, hold centre: menu\n"
                             "Tap a digit: new message\n"
                             "Hold #: next lights",
                             12000);
}

static void pickNode(const char *title, Step next)
{
    static Step after;
    after = next;
    screen->showNodePicker(title, 30000, [](uint32_t node) -> void {
        if (node && node != meshMyNode()) {
            pickedNode = node;
            queue(after);
        }
    });
}

// hold a digit on a Meshtastic screen: send canned message N (Meshtastic app: Canned Message
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
    snprintf(title, sizeof(title), "Send to everyone?\n\"%s\"", text);
    static const char *items[] = {"Send", "Cancel"};
    banner(title, items, 2, 0, [](int sel) -> void {
        if (sel == 0)
            keypadInjectText(text, true);
    });
}

void menuOpen()
{
    queue(S_MAIN);
}

void menuPoll()
{
    Step s = queued;
    if (s == S_NONE || !screen)
        return;
    if (screen->isOverlayBannerShowing())
        return; // wait until the previous banner has gone
    queued = S_NONE;
    switch (s) {
    case S_MAIN: showMain(); break;
    case S_GAMES: showGames(); break;
    case S_SETTINGS: showSettings(); break;
    case S_COLOUR: showColours(); break;
    case S_LEDOFF: showLedOff(); break;
    case S_RESET: showReset(); break;
    case S_HELP: showHelp(); break;
    case S_QUICK: showQuick(); break;
    case S_FOX_PICK: pickNode("Fox: pick a node", S_FOX); break;
    case S_TTT_PICK: pickNode("Play who?", S_TTT_MESH); break;
    case S_NAMETAG: openNameTag(); break;
    case S_COMPOSE: openCompose(-1); break;
    case S_LIGHTS: openLightsApp(); break;
    case S_SIMON: openSimon(); break;
    case S_WHACK: openWhack(); break;
    case S_SNAKE: openSnake(); break;
    case S_BLOCKS: openTetris(); break;
    case S_TTT_CPU: openTicTacToeCpu(); break;
    case S_TTT_MESH: openTicTacToeMesh(pickedNode, true); break;
    case S_FOX: openFoxHunt(pickedNode); break;
    case S_BADGES: openBadgesApp(); break;
    case S_MINIBADGES: openMinibadgesApp(); break;
    case S_TAGLINE: openTaglineEditor(); break;
    case S_NAME: openNameEditor(); break;
    case S_JOYSTICK: openJoystickSetup(); break;
    default: break;
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
        "Welcome to BSides!",   "Joystick: move",      "Centre press: select", "* = back",
        "Hold centre: menu",    "Tap a digit: message", "Hold #: next lights",  "",
        "(press any key)",
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

} // namespace badge

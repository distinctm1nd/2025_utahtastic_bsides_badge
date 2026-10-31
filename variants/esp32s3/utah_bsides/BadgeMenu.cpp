// Badge menu (hold *) and the boot welcome screen.
//
// Menus use Meshtastic's own option banners (BannerOverlayOptions), so they look like the
// rest of the 2.7 UI and are driven by the same keys: joystick up/down to move, # to choose,
// * to close. A banner callback can't open another banner directly (the renderer clears
// the banner right after the callback), so callbacks queue the next menu and menuPoll()
// opens it on the keypad thread's next tick.
#include "configuration.h"

#include "BadgeLeds.h"
#include "BadgeMenu.h"
#include "BadgeScenes.h"

#include "NodeDB.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "main.h"
#include <Preferences.h>
#include <stdio.h>

extern graphics::Screen *screen;

namespace badge
{

enum MenuId { MENU_NONE, MENU_MAIN, MENU_SCENE, MENU_SPEED, MENU_BRIGHT, MENU_RESET, MENU_HELP };
static volatile MenuId queued = MENU_NONE;

static void queue(MenuId m)
{
    queued = m;
}

// ---- persistent "welcome" flag (same key as the 2025 firmware) --------------------------
static bool welcomeEnabled()
{
    Preferences p;
    bool on = true;
    if (p.begin("utah", true)) {
        on = p.getInt("welcome", 1) != 0;
        p.end();
    }
    return on;
}
static void setWelcomeEnabled(bool on)
{
    Preferences p;
    if (p.begin("utah", false)) {
        p.putInt("welcome", on ? 1 : 0);
        p.end();
    }
}

// ---- main menu ----------------------------------------------------------------------------
enum MainItem { M_BACK, M_SCENE, M_SPEED, M_BRIGHT, M_MOON, M_KEYFX, M_WELCOME, M_HELP, M_RESET, M_COUNT };
static char lblScene[40], lblSpeed[32], lblBright[32], lblMoon[32], lblKeyfx[32], lblWelcome[32];
static const char *mainItems[M_COUNT];

static void showMain(int selected = 1)
{
    if (!screen)
        return;
    snprintf(lblScene, sizeof(lblScene), "Lights: %s", sceneAt(ledsScene()).name);
    snprintf(lblSpeed, sizeof(lblSpeed), "Light speed: %s", ledsSpeedName(ledsSpeed()));
    snprintf(lblBright, sizeof(lblBright), "Brightness: %s", ledsBrightName(ledsBright()));
    snprintf(lblMoon, sizeof(lblMoon), "Moon: %s", ledsMoonFollows() ? "follows lights" : "app colour");
    snprintf(lblKeyfx, sizeof(lblKeyfx), "Key flash: %s", ledsKeyFlash() ? "on" : "off");
    snprintf(lblWelcome, sizeof(lblWelcome), "Welcome tips: %s", welcomeEnabled() ? "on" : "off");
    mainItems[M_BACK] = "Back";
    mainItems[M_SCENE] = lblScene;
    mainItems[M_SPEED] = lblSpeed;
    mainItems[M_BRIGHT] = lblBright;
    mainItems[M_MOON] = lblMoon;
    mainItems[M_KEYFX] = lblKeyfx;
    mainItems[M_WELCOME] = lblWelcome;
    mainItems[M_HELP] = "Key help";
    mainItems[M_RESET] = "Factory reset";

    graphics::BannerOverlayOptions o;
    o.message = "BSides Badge";
    o.optionsArrayPtr = mainItems;
    o.optionsCount = M_COUNT;
    o.InitialSelected = selected;
    o.bannerCallback = [](int sel) -> void {
        switch (sel) {
        case M_SCENE: queue(MENU_SCENE); break;
        case M_SPEED: queue(MENU_SPEED); break;
        case M_BRIGHT: queue(MENU_BRIGHT); break;
        case M_MOON:
            ledsSetMoonFollows(!ledsMoonFollows());
            queue(MENU_MAIN);
            break;
        case M_KEYFX:
            ledsSetKeyFlash(!ledsKeyFlash());
            queue(MENU_MAIN);
            break;
        case M_WELCOME:
            setWelcomeEnabled(!welcomeEnabled());
            queue(MENU_MAIN);
            break;
        case M_HELP: queue(MENU_HELP); break;
        case M_RESET: queue(MENU_RESET); break;
        default: break;
        }
    };
    screen->showOverlayBanner(o);
}

// ---- LED scene picker -----------------------------------------------------------------------
static const char *sceneItems[64];
static void showScenes()
{
    int n = sceneCount();
    if (n > 63)
        n = 63;
    sceneItems[0] = "Back";
    for (int i = 0; i < n; i++)
        sceneItems[i + 1] = sceneAt(i).name;
    graphics::BannerOverlayOptions o;
    o.message = "Badge lights";
    o.optionsArrayPtr = sceneItems;
    o.optionsCount = (uint8_t)(n + 1);
    o.InitialSelected = (int8_t)(ledsScene() + 1 <= 127 ? ledsScene() + 1 : 0);
    o.bannerCallback = [](int sel) -> void {
        if (sel > 0)
            ledsSetScene(sel - 1);
        queue(MENU_MAIN);
    };
    screen->showOverlayBanner(o);
}

static const char *listItems[8];
static void showList(const char *title, int count, const char *(*name)(int), int current, void (*apply)(int))
{
    static void (*applyFn)(int) = nullptr;
    applyFn = apply;
    listItems[0] = "Back";
    for (int i = 0; i < count && i < 7; i++)
        listItems[i + 1] = name(i);
    graphics::BannerOverlayOptions o;
    o.message = title;
    o.optionsArrayPtr = listItems;
    o.optionsCount = (uint8_t)(count + 1);
    o.InitialSelected = (int8_t)(current + 1);
    o.bannerCallback = [](int sel) -> void {
        if (sel > 0 && applyFn)
            applyFn(sel - 1);
        queue(MENU_MAIN);
    };
    screen->showOverlayBanner(o);
}

static void showReset()
{
    static const char *items[] = {"No, keep my settings", "Yes, erase everything"};
    graphics::BannerOverlayOptions o;
    o.message = "Factory reset?\nErases name, keys,\nchannels, messages";
    o.optionsArrayPtr = items;
    o.optionsCount = 2;
    o.InitialSelected = 0;
    o.bannerCallback = [](int sel) -> void {
        if (sel != 1) {
            queue(MENU_MAIN);
            return;
        }
        LOG_INFO("Badge menu: factory reset");
        nodeDB->factoryReset(true);
        rebootAtMsec = (DEFAULT_REBOOT_SECONDS < 0) ? 0 : (millis() + DEFAULT_REBOOT_SECONDS * 1000);
        if (screen)
            screen->showSimpleBanner("Rebooting...", 0);
    };
    screen->showOverlayBanner(o);
}

static void showHelp()
{
    screen->showSimpleBanner("# send/OK  * delete/back\n"
                             "Tap a key for letters\n"
                             "Hold a key for its digit\n"
                             "Hold * menu, # lights",
                             12000);
}

void menuOpen()
{
    queue(MENU_MAIN);
}

void menuPoll()
{
    MenuId m = queued;
    if (m == MENU_NONE || !screen)
        return;
    if (screen->isOverlayBannerShowing())
        return; // wait until the previous banner has gone
    queued = MENU_NONE;
    switch (m) {
    case MENU_MAIN: showMain(); break;
    case MENU_SCENE: showScenes(); break;
    case MENU_SPEED: showList("Light speed", ledsSpeedCount(), ledsSpeedName, ledsSpeed(), ledsSetSpeed); break;
    case MENU_BRIGHT: showList("Brightness", ledsBrightCount(), ledsBrightName, ledsBright(), ledsSetBright); break;
    case MENU_RESET: showReset(); break;
    case MENU_HELP: showHelp(); break;
    default: break;
    }
}

void menuShowSceneName()
{
    static char msg[48];
    snprintf(msg, sizeof(msg), "Lights:\n%s", sceneAt(ledsScene()).name);
    if (screen)
        screen->showSimpleBanner(msg, 1500);
}

// ---- welcome screen -----------------------------------------------------------------------
static bool welcomeUp = false;

static void drawWelcome(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    (void)state;
    static const char *lines[] = {
        "Welcome to BSides!",   "Tap keys to type,",   "hold a key for digit", "# = send / OK",
        "* = delete / back",    "joystick = move",     "hold * = badge menu",  "hold # = next lights",
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

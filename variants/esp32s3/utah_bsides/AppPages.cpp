// A menu of Meshtastic's screens, so you can jump straight to one instead of scrolling left/right
// through them all. Shown as an overlay menu over the tiles (like Games), reading the names
// Meshtastic gives each page (Screen::framesetInfo.positions) and, on pick, jumping there with the
// same frame switch the Messages page uses.
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeHome.h"
#include "BadgeMenu.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <Arduino.h>
#include <OLEDDisplayUi.h>
#include <stdio.h>

// reach Screen's private ui + framesetInfo the same way the Messages page does
namespace
{
struct PgUi {
    friend auto pgUi(PgUi);
};
struct PgFrames {
    friend auto pgFrames(PgFrames);
};
template <auto M> struct PgStealUi {
    friend auto pgUi(PgUi) { return M; }
};
template <auto M> struct PgStealFrames {
    friend auto pgFrames(PgFrames) { return M; }
};
template struct PgStealUi<&graphics::Screen::ui>;
template struct PgStealFrames<&graphics::Screen::framesetInfo>;
} // namespace

namespace badge
{

// one entry per Meshtastic page that can be present
enum PageId {
    P_HOME,
    P_DEVICE,
    P_MESSAGES,
    P_NODES,
    P_NODES_LOC,
    P_NODES_HEARD,
    P_NODES_SIGNAL,
    P_NODES_DIST,
    P_NODES_BEARING,
    P_CLOCK,
    P_GPS,
    P_LORA,
    P_WIFI,
    P_SETTINGS,
    P_SYSTEM,
    P_MODULE,
    P_LOG,
    P_WAYPOINT,
    P_CHIRPY,
    P_FAULT,
};

static uint8_t posOf(PageId id)
{
    if (!screen)
        return 255;
    const auto &p = ((*screen).*pgFrames(PgFrames{})).positions;
    switch (id) {
    case P_HOME:
        return p.home;
    case P_DEVICE:
        return p.deviceFocused;
    case P_MESSAGES:
        return p.textMessage;
    case P_NODES:
        return p.nodelist_nodes;
    case P_NODES_LOC:
        return p.nodelist_location;
    case P_NODES_HEARD:
        return p.nodelist_lastheard;
    case P_NODES_SIGNAL:
        return p.nodelist_hopsignal;
    case P_NODES_DIST:
        return p.nodelist_distance;
    case P_NODES_BEARING:
        return p.nodelist_bearings;
    case P_CLOCK:
        return p.clock;
    case P_GPS:
        return p.gps;
    case P_LORA:
        return p.lora;
    case P_WIFI:
        return p.wifi;
    case P_SETTINGS:
        return p.settings;
    case P_SYSTEM:
        return p.system;
    case P_MODULE:
        return p.focusedModule;
    case P_LOG:
        return p.log;
    case P_WAYPOINT:
        return p.waypoint;
    case P_CHIRPY:
        return p.chirpy;
    case P_FAULT:
        return p.fault;
    }
    return 255;
}

// the overlay's option list: index 0 is "Back", 1..n are the pages present now. The strings must
// outlive the banner, so they live here.
static const int MAXP = 24;
static char optBuf[MAXP + 1][22];
static const char *opts[MAXP + 1];
static uint8_t ids[MAXP]; // page id for opts[1..n]
static int nPages = 0;
static int pending = -1; // page id chosen, acted on by pagesPoll on the keypad thread

static void add(PageId id, const char *label)
{
    if (nPages >= MAXP || posOf(id) == 255)
        return;
    ids[nPages] = (uint8_t)id;
    snprintf(optBuf[nPages + 1], sizeof(optBuf[0]), "%s", label);
    nPages++;
}

static void build()
{
    nPages = 0;
    snprintf(optBuf[0], sizeof(optBuf[0]), "Back");
    add(P_HOME, "Home");
    add(P_DEVICE, "Badge / device");
    add(P_MESSAGES, "Messages");
    add(P_NODES, "Nodes");
    add(P_NODES_HEARD, "Nodes: last heard");
    add(P_NODES_SIGNAL, "Nodes: signal");
    add(P_NODES_DIST, "Nodes: distance");
    add(P_NODES_BEARING, "Nodes: bearing");
    add(P_NODES_LOC, "Nodes: location");
    add(P_CLOCK, "Clock");
    add(P_GPS, "GPS");
    add(P_LORA, "LoRa");
    add(P_WIFI, "WiFi");
    add(P_SETTINGS, "Settings");
    add(P_SYSTEM, "System");
    add(P_MODULE, "Module");
    add(P_LOG, "Debug log");
    add(P_WAYPOINT, "Waypoint");
    add(P_CHIRPY, "Chirpy");
    add(P_FAULT, "Fault");
    for (int i = 0; i <= nPages; i++)
        opts[i] = optBuf[i];
}

static void doJump(PageId id)
{
    if (!screen)
        return;
    homeYield();                                         // the user wants a Meshtastic page now: stop home reclaiming the screen
    menuClearReturn();                                   // ...and don't bounce back into the badge menu on close
    screen->setFrames(graphics::Screen::FOCUS_PRESERVE); // rebuild so the switch sticks
    uint8_t idx = posOf(id);                             // re-read: indices may have shifted
    if (idx != 255) {
        OLEDDisplayUi *ui = (*screen).*pgUi(PgUi{});
        if (ui)
            ui->switchToFrame(idx);
    }
    appClose(); // leave the badge screen (home) so the Meshtastic page shows
}

void openPagesMenu()
{
    build();
    if (!screen)
        return;
    if (!nPages) {
        screen->showSimpleBanner("No pages right now", 2000);
        return;
    }
    pending = -1;
    graphics::BannerOverlayOptions o;
    o.message = "Go to page";
    o.optionsArrayPtr = opts;
    o.optionsCount = (uint8_t)(nPages + 1);
    o.InitialSelected = 1; // start on the first page, not "Back"
    o.durationMs = 60UL * 60 * 1000;
    o.bannerCallback = [](int sel) -> void {
        if (sel > 0 && sel <= nPages) {
            homeYield();            // keep home from redrawing before the jump lands
            pending = ids[sel - 1]; // the jump itself runs on the keypad thread (pagesPoll)
        }
    };
    screen->showOverlayBanner(o);
}

// keypad thread: perform a jump chosen in the overlay (frame switches belong off the draw thread)
void pagesPoll()
{
    if (pending < 0)
        return;
    if (screen && screen->isOverlayBannerShowing())
        return; // let the overlay finish closing before we switch frames
    PageId id = (PageId)pending;
    pending = -1;
    doJump(id);
}

} // namespace badge

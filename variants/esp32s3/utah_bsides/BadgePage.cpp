// The badge's own page among Meshtastic's screens (joystick left/right): the everyday things a
// standalone badge needs at a glance. Meshtastic's home page already has nodes online, ChUtil,
// uptime and battery, so they aren't repeated here.
#include "configuration.h"

#include "BadgeFollow.h"
#include "BadgeMesh.h"
#include "BadgePortal.h"
#include "BadgeTime.h"

#include "DisplayFormatters.h"
#include "MeshRadio.h"
#include "MessageStore.h"
#include "NodeDB.h"
#include "gps/RTC.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "mesh/Channels.h"
#include <Arduino.h>
#include <OLEDDisplay.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace graphics
{
extern bool hasUnreadMessage;
}

namespace badge
{

static void fit(OLEDDisplay *d, char *s, int maxW)
{
    int n = strlen(s);
    while (n > 1 && d->getStringWidth(s, n) > maxW)
        s[--n] = 0;
}

static void ago(char *b, size_t n, uint32_t s)
{
    if (s < 60)
        snprintf(b, n, "%us", (unsigned)s);
    else if (s < 3600)
        snprintf(b, n, "%um", (unsigned)(s / 60));
    else if (s < 86400)
        snprintf(b, n, "%uh", (unsigned)(s / 3600));
    else
        snprintf(b, n, "%ud", (unsigned)(s / 86400));
}

void badgePageDraw(OLEDDisplay *d, int16_t x, int16_t y)
{
    d->clear();
    graphics::drawCommonHeader(d, x, y, "Badge");
    const int W = d->width() - 4, lh = FONT_HEIGHT_SMALL;
    int ly = y + graphics::getTextPositions(d)[1];
    char b[96], a[16], name[40];
    d->setTextAlignment(TEXT_ALIGN_LEFT);

    // date and time
    if (timeKnown()) {
        time_t t = getValidTime(RTCQualityFromNet, true); // local time
        struct tm tm;
        gmtime_r(&t, &tm);
        d->setFont(FONT_MEDIUM);
        strftime(b, sizeof(b), "%H:%M", &tm);
        d->drawString(x + 2, ly, b);
        d->setFont(FONT_SMALL);
        strftime(b, sizeof(b), "%a %e %b", &tm);
        d->setTextAlignment(TEXT_ALIGN_RIGHT);
        d->drawString(x + 2 + W, ly + (FONT_HEIGHT_MEDIUM - lh) / 2, b);
        d->setTextAlignment(TEXT_ALIGN_LEFT);
        ly += FONT_HEIGHT_MEDIUM;
    } else {
        d->setFont(FONT_SMALL);
        d->drawString(x + 2, ly, "Time unknown (asking)");
        ly += lh;
    }
    d->setFont(FONT_SMALL);

    // the latest message
    const auto &msgs = messageStore.getMessages();
    if (msgs.empty()) {
        d->drawString(x + 2, ly, "No messages yet");
        ly += 2 * lh;
    } else {
        const StoredMessage &m = msgs.back();
        uint32_t secs = 0;
        bool known = true;
        if (m.isBootRelative)
            secs = millis() / 1000 >= m.timestamp ? millis() / 1000 - m.timestamp : 0;
        else if (timeKnown() && getValidTime(RTCQualityFromNet) >= m.timestamp)
            secs = getValidTime(RTCQualityFromNet) - m.timestamp;
        else
            known = false;
        if (known)
            ago(a, sizeof(a), secs);
        else
            snprintf(a, sizeof(a), "?");
        if (m.sender == nodeDB->getNodeNum())
            snprintf(name, sizeof(name), "You");
        else
            meshNodeName(m.sender, name, sizeof(name));
        snprintf(b, sizeof(b), "%s%s %s", graphics::hasUnreadMessage ? "New: " : "Msg: ", name, a);
        fit(d, b, W);
        d->drawString(x + 2, ly, b);
        ly += lh;
        snprintf(b, sizeof(b), "  %s", MessageStore::getText(m));
        for (char *p = b; *p; p++)
            if (*p == '\n' || *p == '\r')
                *p = ' ';
        fit(d, b, W);
        d->drawString(x + 2, ly, b);
        ly += lh;
    }

    // the last node heard
    uint32_t node, ageMs;
    int rssi, hops;
    float snr;
    if (meshLastHeard(node, rssi, snr, hops, ageMs)) {
        meshNodeName(node, name, sizeof(name));
        ago(a, sizeof(a), ageMs / 1000);
        char sig[24];
        if (hops > 0)
            snprintf(sig, sizeof(sig), " %d hop%s", hops, hops == 1 ? "" : "s");
        else
            snprintf(sig, sizeof(sig), " %ddBm", rssi); // signal only means distance when direct
        snprintf(b, sizeof(b), "Heard %s %s%s", a, name, sig);
        // keep the age and signal: shorten the name instead
        while (d->getStringWidth(b) > W && strlen(name) > 3) {
            name[strlen(name) - 1] = 0;
            snprintf(b, sizeof(b), "Heard %s %s%s", a, name, sig);
        }
        fit(d, b, W);
        d->drawString(x + 2, ly, b);
    } else {
        d->drawString(x + 2, ly, "Heard: nothing yet");
    }
    ly += lh;

    // the background watch (Fox hunt > Background watch)
    if (!watchBackground()) {
        snprintf(b, sizeof(b), "Watch off (Fox hunt menu)");
    } else {
        int near, trackers, following;
        watchSummary(near, trackers, following);
        if (following)
            snprintf(b, sizeof(b), "! %d following you", following);
        else if (near)
            snprintf(b, sizeof(b), "Watch: %d marked near%s", near, trackers ? (trackers == near ? " (trk)" : "") : "");
        else
            snprintf(b, sizeof(b), "Watch: all clear");
        if (trackers && trackers != near && !following)
            snprintf(b + strlen(b), sizeof(b) - strlen(b), ", %d trk", trackers);
    }
    fit(d, b, W);
    d->drawString(x + 2, ly, b);
    ly += lh;

    // Badge WiFi, when it's still sharing (only takes a line while it's on)
    if (portalRunning()) {
        int phones = portalClients();
        snprintf(b, sizeof(b), "WiFi sharing on (%s)", phones ? "phone connected" : "waiting");
        fit(d, b, W);
        d->drawString(x + 2, ly, b);
        ly += lh;
    }

    // can this badge talk?
    if (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_UNSET)
        snprintf(b, sizeof(b), "! Region not set: can't send");
    else if (!config.lora.tx_enabled)
        snprintf(b, sizeof(b), "! Transmit is off");
    else
        snprintf(b, sizeof(b), "%s %s  #%s", myRegion ? myRegion->name : "?",
                 DisplayFormatters::getModemPresetDisplayName(config.lora.modem_preset, false, config.lora.use_preset),
                 channels.getName(channels.getPrimaryIndex()));
    fit(d, b, W);
    d->drawString(x + 2, ly, b);
}

} // namespace badge

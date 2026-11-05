#include "configuration.h"

#include "BadgeTime.h"

#include "BadgeApp.h"
#include "BadgeMenu.h"
#include "BadgeMesh.h"
#include "NodeDB.h"
#include "gps/RTC.h"
#include "graphics/Screen.h"
#include "mesh/Throttle.h"
#include <Arduino.h>
#include <ctype.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

extern uint32_t serialSinceMsec;
namespace graphics
{
extern uint32_t logo_timeout;
}

namespace badge
{

static const uint32_t MIN_EPOCH = 1735689600; // 2025-01-01: anything earlier is junk
static const int TIME_HOPS = 2;
static uint32_t lastAsk = 0, lastAnswer = 0, answerAt = 0;
static bool asked = false, answered = false, clockHiddenByUs = false;

bool timeKnown()
{
    return getRTCQuality() >= RTCQualityFromNet;
}

static void sendTime()
{
    uint32_t t = getValidTime(RTCQualityFromNet);
    if (!t)
        return;
    uint8_t b[7] = {'B', 'T', 'M', (uint8_t)t, (uint8_t)(t >> 8), (uint8_t)(t >> 16), (uint8_t)(t >> 24)};
    meshSendPrivate(NODENUM_BROADCAST, b, sizeof(b), false, TIME_HOPS);
    lastAnswer = millis();
    answered = true;
}

// hide Meshtastic's clock screen while the time is unknown (only between badge screens:
// rebuilding the frames would close a badge app)
static void clockScreen()
{
    if (!screen || appActive() || welcomeActive() || screen->isOverlayBannerShowing() ||
        millis() < graphics::logo_timeout + serialSinceMsec + 3000)
        return;
    bool known = timeKnown(), hidden = screen->isFrameHidden("clock");
    if (!known && !hidden) {
        screen->toggleFrameVisibility("clock");
        clockHiddenByUs = true;
        screen->setFrames(graphics::Screen::FOCUS_PRESERVE);
    } else if (known && hidden && clockHiddenByUs) {
        screen->toggleFrameVisibility("clock");
        clockHiddenByUs = false;
        screen->setFrames(graphics::Screen::FOCUS_PRESERVE);
    }
}

void timeTick()
{
    uint32_t now = millis();
    if (!timeKnown() && now > 30000 && (!asked || !Throttle::isWithinTimespanMs(lastAsk, 120000))) {
        static const uint8_t q[] = {'B', 'T', 'Q'};
        meshSendPrivate(NODENUM_BROADCAST, q, sizeof(q), false, TIME_HOPS);
        lastAsk = now;
        asked = true;
    }
    if (answerAt && (int32_t)(now - answerAt) >= 0) {
        answerAt = 0;
        if (timeKnown())
            sendTime();
    }
    clockScreen();
}

void timePacket(uint32_t from, const uint8_t *b, size_t len)
{
    (void)from;
    if (len >= 3 && !memcmp(b, "BTQ", 3)) {
        // answer after a random wait; if another badge answers first, stay quiet
        if (timeKnown() && !answerAt && (!answered || !Throttle::isWithinTimespanMs(lastAnswer, 30000)))
            answerAt = (millis() + 500 + random(5500)) | 1;
    } else if (len >= 7 && !memcmp(b, "BTM", 3)) {
        answerAt = 0;
        uint32_t t = b[3] | (b[4] << 8) | (b[5] << 16) | ((uint32_t)b[6] << 24);
        if (!timeKnown() && t > MIN_EPOCH) {
            struct timeval tv = {(time_t)t, 0};
            perhapsSetRTC(RTCQualityFromNet, &tv);
        }
    }
}

// the same zones as Meshtastic's own Timezone menu
static const char *const ZONES[][2] = {
    {"US/Hawaii", "HST10"},
    {"US/Alaska", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"US/Pacific", "PST8PDT,M3.2.0,M11.1.0"},
    {"US/Arizona", "MST7"},
    {"US/Mountain", "MST7MDT,M3.2.0,M11.1.0"},
    {"US/Central", "CST6CDT,M3.2.0,M11.1.0"},
    {"US/Eastern", "EST5EDT,M3.2.0,M11.1.0"},
    {"BR/Brasilia", "BRT3"},
    {"UTC", "UTC0"},
    {"EU/Western", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"EU/Central", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"EU/Eastern", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Asia/Kolkata", "IST-5:30"},
    {"Asia/Hong_Kong", "HKT-8"},
    {"AU/AWST", "AWST-8"},
    {"AU/ACST", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
    {"AU/AEST", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Pacific/NZ", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
};
static const int NZONES = sizeof(ZONES) / sizeof(ZONES[0]);

int timeZoneCount()
{
    return NZONES;
}
const char *timeZoneLabel(int i)
{
    return i >= 0 && i < NZONES ? ZONES[i][0] : "";
}
const char *timeZoneValue(int i)
{
    return i >= 0 && i < NZONES ? ZONES[i][1] : "";
}
const char *timeZoneCurrentLabel()
{
    for (int i = 0; i < NZONES; i++)
        if (!strcmp(config.device.tzdef, ZONES[i][1]))
            return ZONES[i][0];
    return config.device.tzdef[0] ? "Custom" : "UTC";
}

static void applyZone()
{
    setenv("TZ", config.device.tzdef, 1);
    tzset();
}

void timeZoneDefault()
{
    if (config.device.tzdef[0])
        return;
    strcpy(config.device.tzdef, "MST7MDT,M3.2.0,M11.1.0"); // a Utah badge; Settings > Time zone to change
    applyZone();
}

bool timeSetZone(const char *posix)
{
    size_t n = posix ? strlen(posix) : 0;
    if (!n || n >= sizeof(config.device.tzdef))
        return false;
    for (const char *c = posix; *c; c++)
        if (!isalnum((unsigned char)*c) && !strchr("<>+-:,./", *c))
            return false;
    if (!strcmp(config.device.tzdef, posix))
        return true;
    strcpy(config.device.tzdef, posix);
    applyZone();
    nodeDB->saveToDisk(SEGMENT_CONFIG);
    return true;
}

bool timeSetFromBrowser(uint32_t epoch)
{
    if (epoch < MIN_EPOCH)
        return false;
    struct timeval tv = {(time_t)epoch, 0};
    perhapsSetRTC(RTCQualityNTP, &tv); // same trust as the phone app's time
    return true;
}

} // namespace badge

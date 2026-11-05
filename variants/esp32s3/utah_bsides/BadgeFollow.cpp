#include "configuration.h"

#include "BadgeFollow.h"

#include "BadgeApp.h"
#include "BadgeDevices.h"
#include "BadgeKeypad.h"
#include "BadgeLeds.h"
#include "BadgeLog.h"
#include "BadgeMesh.h"
#include "BadgeRules.h"
#include "BadgeTime.h"

#include "concurrency/OSThread.h"
#include "gps/RTC.h"
#include "graphics/Screen.h"
#include "mesh/MeshTypes.h"
#include "mesh/Throttle.h"
#include <Arduino.h>
#include <Preferences.h>
#include <stdio.h>
#include <string.h>

namespace badge
{

static const uint32_t GAP_MS = 5 * 60 * 1000;      // longer out of range: it starts over
static uint32_t followMs();                        // in range this long: with you (Alert settings, 10 min by default)
static const uint32_t LANDMARK_MS = 2 * 60 * 1000; // a landmark heard this recently is still around
static const uint32_t ALERT_GAP_MS = 15 * 1000;    // at most one alert message per this
static const uint32_t ATTACK_REPEAT_MS = 2 * 60 * 1000;
static const uint32_t SHARE_GAP_MS = 10 * 1000;       // at most one shared sighting per this
static const int DEAUTH_ALERT = 30, POPUP_ALERT = 20; // in 10 s
static const int MAX_TRACKED = 16, MAX_REFS = 16, MAX_LANDMARKS = 64;

struct Tracked {
    bool used;
    uint8_t mac[6];
    uint32_t first, last;
    int8_t best;
    uint8_t nRefs;
    uint32_t refs[MAX_REFS];
    const char *code, *title; // from the rule table (static strings)
    bool tracker;
    bool wifi;
    uint16_t seenBefore; // earlier sightings (log index)
    uint32_t agoS;       // since the last one
    bool saidNew, saidAgain, saidFollow, saidMoved, loggedFollow, loggedMoved;
};
struct Landmark {
    uint32_t id, last;
};

static Tracked tracked[MAX_TRACKED];
static Landmark landmarks[MAX_LANDMARKS];
static uint32_t lastAlert = 0, lastDeauthAlert = 0, lastPopupAlert = 0, lastShare = 0;
static char message[128], pending[128];

static uint32_t idOf(const uint8_t *mac)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; i++)
        h = (h ^ mac[i]) * 16777619u;
    return h | 1; // 0 means empty
}

static void noteLandmark(uint32_t id, uint32_t now)
{
    int slot = 0;
    for (int i = 0; i < MAX_LANDMARKS; i++) {
        if (landmarks[i].id == id) {
            landmarks[i].last = now;
            return;
        }
        if (!landmarks[i].id || now - landmarks[i].last > now - landmarks[slot].last)
            slot = i; // empty or oldest
        if (!landmarks[i].id)
            break;
    }
    landmarks[slot] = {id, now};
}

static bool landmarkAround(uint32_t id)
{
    for (const auto &l : landmarks)
        if (l.id == id)
            return Throttle::isWithinTimespanMs(l.last, LANDMARK_MS);
    return false;
}

static Tracked *find(const uint8_t *mac)
{
    for (auto &t : tracked)
        if (t.used && memcmp(t.mac, mac, 6) == 0)
            return &t;
    return nullptr;
}

static void gone(Tracked &t)
{
    char extra[24];
    snprintf(extra, sizeof(extra), "%u min", (unsigned)((t.last - t.first) / 60000));
    logEvent("GONE", t.code, t.mac, t.best, t.title, extra);
    t.used = false;
}

static Tracked *start(const ScanEntry &e, const RuleHit &h, uint32_t now)
{
    Tracked *t = nullptr;
    for (auto &c : tracked) { // a free slot, else the one heard least recently
        if (!c.used) {
            t = &c;
            break;
        }
        if (!t || now - c.last > now - t->last)
            t = &c;
    }
    if (t->used)
        gone(*t);
    *t = {};
    t->used = true;
    memcpy(t->mac, e.mac, 6);
    t->first = t->last = now;
    t->best = e.rssi;
    t->code = h.code;
    t->title = h.title;
    t->tracker = strcmp(h.tag, "Tracker") == 0;
    for (const auto &l : landmarks)
        if (l.id && t->nRefs < MAX_REFS && Throttle::isWithinTimespanMs(l.last, LANDMARK_MS))
            t->refs[t->nRefs++] = l.id;
    t->seenBefore = logSeenBefore(e.mac, &t->agoS);
    logEvent(t->seenBefore ? "AGAIN" : "SEEN", h.code, e.mac, e.rssi, h.title, e.name);
    return t;
}

static void info(const Tracked &t, FollowInfo &out)
{
    out = {};
    out.withYouMs = t.last - t.first;
    out.seenMs = t.last;
    out.bestRssi = t.best;
    out.refs = t.nRefs;
    for (int i = 0; i < t.nRefs; i++)
        out.refsLeft += landmarkAround(t.refs[i]);
    out.following = out.withYouMs >= followMs();
    out.moved = out.following && out.refs >= 3 && out.refsLeft * 3 < out.refs;
    out.seenBefore = t.seenBefore;
    out.agoS = t.agoS;
}

bool followGet(const uint8_t mac[6], FollowInfo &out)
{
    const Tracked *t = find(mac);
    if (!t || !Throttle::isWithinTimespanMs(t->last, GAP_MS))
        return false;
    info(*t, out);
    return true;
}

static void agoText(char *out, size_t n, uint32_t s)
{
    if (!s)
        snprintf(out, n, "earlier");
    else if (s < 3600)
        snprintf(out, n, "%u min ago", (unsigned)(s / 60));
    else if (s < 2 * 86400)
        snprintf(out, n, "%u h ago", (unsigned)(s / 3600));
    else
        snprintf(out, n, "%u days ago", (unsigned)(s / 86400));
}

const char *watchShareNow(const ScanEntry &e, bool wifi)
{
    RuleHit h;
    if (!ruleMatch(e, wifi, h))
        return "Only marked devices\ncan be shared";
    if (lastShare && Throttle::isWithinTimespanMs(lastShare, SHARE_GAP_MS))
        return "Shared a moment ago;\ntry again in a few seconds";
    uint8_t p[3 + 4 + 1 + 6 + 40];
    memcpy(p, "BSG", 3);
    memset(p + 3, ' ', 4);
    memcpy(p + 3, h.code, strnlen(h.code, 4));
    p[7] = (uint8_t)e.rssi;
    memcpy(p + 8, e.mac, 6);
    size_t tl = strnlen(h.title, 40);
    memcpy(p + 14, h.title, tl);
    if (!meshSendPrivate(NODENUM_BROADCAST, p, 14 + tl, false))
        return "Couldn't send\n(mesh not ready)";
    lastShare = millis();
    logEvent("SHARED", h.code, e.mac, e.rssi, h.title);
    return "Shared with\nother badges";
}

static void meshSighting(uint32_t from, const uint8_t *p, size_t len)
{
    if (len < 11)
        return;
    char code[5], title[41], who[40], extra[48];
    memcpy(code, p, 4);
    code[4] = 0;
    for (int i = 3; i >= 0 && code[i] == ' '; i--)
        code[i] = 0;
    size_t tl = len - 11 > 40 ? 40 : len - 11;
    memcpy(title, p + 11, tl);
    title[tl] = 0;
    meshNodeName(from, who, sizeof(who));
    snprintf(extra, sizeof(extra), "from %s", who);
    logEvent("MESH", code, p + 5, (int8_t)p[4], title, extra);
    snprintf(pending, sizeof(pending), "%s spotted:\n%s %s\n%d dBm there", who, code, title, (int8_t)p[4]);
}

// ---- alert settings (NVS "utah") ---------------------------------------------------------------
static int getInt(const char *key, int def);
static void putInt(const char *key, int v);

static const int FOLLOW_CHOICES[] = {5, 10, 20};
int watchFollowMin()
{
    int v = getInt("fmin", 10);
    return v == 5 || v == 20 ? v : 10;
}
void watchSetFollowMin(int m)
{
    putInt("fmin", m);
}
static uint32_t followMs()
{
    return (uint32_t)watchFollowMin() * 60 * 1000;
}
int watchAlertMinRssi()
{
    return getInt("armin", 0);
}
void watchSetAlertMinRssi(int dbm)
{
    putInt("armin", dbm);
}
int watchAlertCats()
{
    return getInt("acat", AC_ALL);
}
void watchSetAlertCats(int cats)
{
    putInt("acat", cats);
}
int watchQuietStart()
{
    return getInt("qhs", -1);
}
int watchQuietEnd()
{
    return getInt("qhe", 7);
}
void watchSetQuiet(int startHour, int endHour)
{
    putInt("qhs", startHour);
    putInt("qhe", endHour);
}
bool watchListFaint()
{
    return getInt("faint", 1) != 0;
}
void watchSetListFaint(bool on)
{
    putInt("faint", on);
}

int watchCategoryOf(const char *code)
{
    if (!code)
        return 0;
    if (!strcmp(code, "TAG"))
        return AC_TRACKERS;
    if (!strcmp(code, "CAM"))
        return AC_CAMERAS;
    if (!strcmp(code, "SURV"))
        return AC_SURVEILLANCE;
    if (!strcmp(code, "POL"))
        return AC_POLICE;
    if (!strcmp(code, "SKIM"))
        return AC_SKIMMERS;
    if (!strcmp(code, "DRON"))
        return AC_DRONES;
    if (!strcmp(code, "ATK") || !strcmp(code, "TWIN"))
        return AC_ATTACKS;
    return AC_OTHER;
}

bool watchQuietNow()
{
    int s = watchQuietStart(), e = watchQuietEnd();
    if (s < 0 || s == e || !timeKnown())
        return false;
    int h = (int)(getValidTime(RTCQualityFromNet, true) % 86400 / 3600);
    return s < e ? (h >= s && h < e) : (h >= s || h < e); // e.g. 22-7 wraps past midnight
}

bool watchAlertAllowed(const ScanEntry &e, const RuleHit &h)
{
    if (devTrusted(e.mac))
        return false;
    int cat = watchCategoryOf(h.code);
    if (cat && !(watchAlertCats() & cat))
        return false;
    int min = watchAlertMinRssi();
    if (min && e.rssi < min)
        return false;
    return !watchQuietNow();
}

static bool attackAlertAllowed()
{
    return (watchAlertCats() & AC_ATTACKS) && !watchQuietNow();
}

// ---- the watchlist: devices you asked to hear about when they turn up / when they're gone ------
static const uint32_t AWAY_BEFORE_HERE_MS = 5 * 60 * 1000; // gone this long, then back = "is here"
static const uint32_t GONE_AFTER_MS = 3 * 60 * 1000;       // not heard this long = "is gone"
struct WatchState {
    uint8_t mac[6];
    uint32_t lastSeen;
    bool saidGone;
};
static WatchState watchStates[16];

template <typename Say> static void watchlistStep(const ScanEntry *list, int n, bool wifi, uint32_t now, bool may, Say say)
{
    for (int i = 0; i < devCount(); i++) {
        const DevMemo &d = devAt(i);
        if (!(d.flags & (DEV_WATCH_HERE | DEV_WATCH_GONE)) || ((d.flags & DEV_WIFI) != 0) != wifi)
            continue; // only judge it from the scan of its own kind
        WatchState *w = nullptr;
        for (auto &s : watchStates)
            if (!memcmp(s.mac, d.mac, 6)) {
                w = &s;
                break;
            }
        if (!w) { // a free (or the stalest) slot
            w = &watchStates[0];
            for (auto &s : watchStates)
                if (!s.lastSeen && !s.saidGone) {
                    w = &s;
                    break;
                }
            memset(w, 0, sizeof(*w));
            memcpy(w->mac, d.mac, 6);
            w->saidGone = true; // not seen yet: it hasn't "gone"
        }
        bool here = false;
        int rssi = 0;
        for (int j = 0; j < n && !here; j++)
            if (!memcmp(list[j].mac, d.mac, 6) && Throttle::isWithinTimespanMs(list[j].seenMs, 15000)) {
                here = true;
                rssi = list[j].rssi;
            }
        char msg[96];
        const char *name = d.nick[0] ? d.nick : "a watched device";
        if (here) {
            bool returning = !w->lastSeen || now - w->lastSeen > AWAY_BEFORE_HERE_MS;
            w->lastSeen = now ? now : 1;
            w->saidGone = false;
            if (returning && (d.flags & DEV_WATCH_HERE) && may && !watchQuietNow()) {
                snprintf(msg, sizeof(msg), "Watched device is here:\n%s\n%d dBm", name, rssi);
                say(msg);
                may = false;
            }
        } else if (w->lastSeen && !w->saidGone && now - w->lastSeen > GONE_AFTER_MS) {
            w->saidGone = true;
            if ((d.flags & DEV_WATCH_GONE) && may && !watchQuietNow()) {
                snprintf(msg, sizeof(msg), "Watched device is gone:\n%s\nnot heard for %u min", name,
                         (unsigned)((now - w->lastSeen) / 60000));
                say(msg);
                may = false;
            }
        }
    }
}

int watchTrackedList(TrackedView *out, int max)
{
    int n = 0;
    uint32_t now = millis();
    for (const auto &t : tracked) {
        if (!t.used || n >= max)
            continue;
        TrackedView &v = out[n++];
        memcpy(v.mac, t.mac, 6);
        v.code = t.code;
        v.title = t.title;
        v.best = t.best;
        v.withYouMs = t.last - t.first;
        v.agoMs = now - t.last;
        v.following = t.last - t.first >= followMs();
        v.moved = t.saidMoved || t.loggedMoved;
        v.wifi = t.wifi;
    }
    return n;
}

const char *watchStep(const ScanEntry *list, int n, bool wifi, bool canAlert)
{
    uint32_t now = millis();
    bool mayAlert = canAlert && watchAlerts() && (!lastAlert || !Throttle::isWithinTimespanMs(lastAlert, ALERT_GAP_MS));
    const char *out = nullptr;
    auto say = [&](const char *msg) {
        if (msg != message)
            snprintf(message, sizeof(message), "%s", msg);
        out = message;
        lastAlert = now;
    };

    for (int i = 0; i < n; i++) {
        const ScanEntry &e = list[i];
        RuleHit h;
        bool hit = ruleMatch(e, wifi, h);
        if (!hit || !h.alert) {
            if (e.publicAddr) // fixed things with registered addresses mark where you are
                noteLandmark(idOf(e.mac), now);
            continue;
        }
        Tracked *t = find(e.mac);
        if (t && !Throttle::isWithinTimespanMs(t->last, GAP_MS)) { // out of range too long: start over
            gone(*t);
            t = nullptr;
        }
        if (!t) {
            t = start(e, h, now);
            t->wifi = wifi;
        }
        t->last = now;
        if (e.rssi > t->best)
            t->best = e.rssi;
        FollowInfo f;
        info(*t, f);
        if (f.following && !t->loggedFollow) {
            logEvent("FOLW", t->code, e.mac, e.rssi, t->title);
            t->loggedFollow = true;
        }
        if (f.moved && !t->loggedMoved) {
            logEvent("MOVED", t->code, e.mac, e.rssi, t->title);
            t->loggedMoved = true;
        }
        if (!mayAlert || out || !watchAlertAllowed(e, h))
            continue;
        char ago[24];
        const char *who = devName(e.mac, h.title);
        if (t->tracker && t->seenBefore && !t->saidAgain) {
            agoText(ago, sizeof(ago), t->agoS);
            snprintf(message, sizeof(message), "Tracker seen before:\n%s\nlast time %s", who, ago);
            t->saidAgain = true;
        } else if (!t->tracker && !t->saidNew) {
            snprintf(message, sizeof(message), "%s nearby:\n%s\n%d dBm", h.tag, who, e.rssi);
            t->saidNew = true;
        } else if (t->tracker && f.following && !t->saidFollow) {
            snprintf(message, sizeof(message), "Tracker with you %u min:\n%s\nCheck bags, pockets, car",
                     (unsigned)(f.withYouMs / 60000), who);
            t->saidFollow = true;
        } else if (f.moved && !t->saidMoved) {
            snprintf(message, sizeof(message), "Followed you:\n%s\nYou moved, it stayed with you", who);
            t->saidMoved = true;
        } else {
            continue;
        }
        say(message);
    }
    watchlistStep(list, n, wifi, now, mayAlert && !out, [&](const char *m) {
        snprintf(message, sizeof(message), "%s", m);
        say(message);
    });
    for (auto &t : tracked) // forget what has been gone a while
        if (t.used && !Throttle::isWithinTimespanMs(t.last, GAP_MS))
            gone(t);

    // attacks
    uint8_t ch = 0;
    int deauths = wifi ? radioDeauthRecent(&ch) : 0, popups = wifi ? 0 : radioPopupSpamRecent();
    if (deauths >= DEAUTH_ALERT && (!lastDeauthAlert || !Throttle::isWithinTimespanMs(lastDeauthAlert, ATTACK_REPEAT_MS))) {
        char extra[32];
        snprintf(extra, sizeof(extra), "%d frames in 10 s, ch %u", deauths, ch);
        logEvent("ATTACK", "ATK", nullptr, 0, "WiFi deauth flood", extra);
        if (mayAlert && !out && attackAlertAllowed()) {
            snprintf(message, sizeof(message), "WiFi deauth attack:\n%d kick-off frames in 10 s\nchannel %u", deauths, ch);
            say(message);
            lastDeauthAlert = now;
        }
    }
    if (popups >= POPUP_ALERT && (!lastPopupAlert || !Throttle::isWithinTimespanMs(lastPopupAlert, ATTACK_REPEAT_MS))) {
        char extra[32];
        snprintf(extra, sizeof(extra), "%d senders in 10 s", popups);
        logEvent("ATTACK", "ATK", nullptr, 0, "Bluetooth pop-up spam", extra);
        if (mayAlert && !out && attackAlertAllowed()) {
            snprintf(message, sizeof(message), "Bluetooth spam attack:\nfake pairing pop-ups from\n%d addresses in 10 s", popups);
            say(message);
            lastPopupAlert = now;
        }
    }
    if (!out && pending[0] && canAlert && watchAlerts()) { // another badge's sighting
        say(pending);
        pending[0] = 0;
    }
    return out;
}

// ---- settings ----------------------------------------------------------------------------
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
static int alertsOn = -1, backgroundMode = -1; // -1: not read yet

void watchSummary(int &near, int &trackers, int &following)
{
    near = trackers = following = 0;
    uint32_t now = millis();
    for (const Tracked &t : tracked) {
        if (!t.used || now - t.last > LANDMARK_MS)
            continue;
        near++;
        trackers += t.tracker;
        following += now - t.first >= followMs();
    }
}

bool watchAlerts()
{
    if (alertsOn < 0)
        alertsOn = getInt("fwalert", 1);
    return alertsOn;
}
void watchSetAlerts(bool on)
{
    alertsOn = on;
    putInt("fwalert", on);
}
int watchBackground()
{
    if (backgroundMode < 0)
        backgroundMode = getInt("fwbgmode", 0);
    return backgroundMode;
}
void watchSetBackground(int mode)
{
    backgroundMode = mode;
    putInt("fwbgmode", mode);
}

// ---- background watch ----------------------------------------------------------------------
static bool paused = false;

void watchPause(bool p)
{
    paused = p;
}

class WatchThread : public concurrency::OSThread
{
  public:
    WatchThread() : concurrency::OSThread("FoxWatch") { meshSetSightingHandler(meshSighting); }

  protected:
    int32_t runOnce() override
    {
        if (paused) { // the fox hunt app has the radio
            scanning = false;
            return 2000;
        }
        int mode = watchBackground();
        if (!mode) {
            if (scanning) {
                radioScanStop();
                scanning = false;
            }
            deliver(nullptr); // other badges' sightings still show
            return 2000;
        }
        uint32_t now = millis();
        if (sweepingWifi) { // a short WiFi sweep every couple of minutes
            radioScanPoll();
            if (Throttle::isWithinTimespanMs(sweepStart, 4000))
                return 200;
            int n = radioScanSnapshot(buf, MAX);
            deliver(watchStep(buf, n, true, canShow()));
            sweepingWifi = false;
            scanning = false; // back to Bluetooth
            return 100;
        }
        if (mode == 2 && (!lastSweep || !Throttle::isWithinTimespanMs(lastSweep, 120000))) {
            lastSweep = now;
            if (!radioScanStart(SCAN_WIFI)) {
                sweepingWifi = true;
                sweepStart = now;
                scanning = false;
                return 200;
            }
        }
        if (!scanning) {
            if (radioScanStart(SCAN_BLE, true)) {
                deliver(nullptr);
                return 30000; // Bluetooth off (or Paxcounter on): try again later
            }
            scanning = true;
        }
        radioScanPoll();
        int n = radioScanSnapshot(buf, MAX);
        deliver(watchStep(buf, n, false, canShow()));
        return 2000;
    }

  private:
    static const int MAX = 40;
    ScanEntry buf[MAX];
    bool scanning = false, sweepingWifi = false;
    uint32_t lastSweep = 0, sweepStart = 0;

    static bool canShow() { return screen && !appActive(); }

    void deliver(const char *msg)
    {
        if (!msg && pending[0] && canShow() && watchAlerts()) {
            snprintf(message, sizeof(message), "%s", pending);
            pending[0] = 0;
            msg = message;
        }
        if (!msg)
            return;
        keypadPokeAwake();
        screen->showSimpleBanner(msg, 10000);
        ledsFlourish();
    }
};

static WatchThread *watchThread = nullptr;

void watchStart()
{
    if (!watchThread)
        watchThread = new WatchThread();
}

} // namespace badge

// Badge mesh module. See BadgeMesh.h.
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeHunt.h"
#include "BadgeLeds.h"
#include "BadgeMesh.h"
#include "BadgeScenes.h"
#include "BadgeStats.h"

#include "MeshService.h"
#include "MeshTypes.h"
#include "NodeDB.h"
#include "Router.h"
#include "concurrency/OSThread.h"
#include "graphics/Screen.h"
#include "MeshModule.h"
#include <Preferences.h>
#include <ctype.h>
#include <strings.h>
#include <stdio.h>
#include <string.h>

extern graphics::Screen *screen;

namespace badge
{

static const meshtastic_PortNum BADGE_PORT = meshtastic_PortNum_PRIVATE_APP; // 256

static bool remoteCmds = true;
static bool joinShows = true;
static bool leading = false;
static uint32_t foxTarget = 0;
static FoxReading fox = {false, 0, 0, 0, -1};
static float foxLastSnr = -999;
static uint32_t foxLastHeard = 0;
static TttHandler tttHandler = nullptr;

// ---- settings ---------------------------------------------------------------------------
static void saveSettings()
{
    Preferences p;
    if (p.begin("utah", false)) {
        p.putBool("remote", remoteCmds);
        p.putBool("joinshow", joinShows);
        p.end();
    }
}
static void loadSettings()
{
    Preferences p;
    if (p.begin("utah", true)) {
        remoteCmds = p.getBool("remote", true);
        joinShows = p.getBool("joinshow", true);
        p.end();
    }
}
bool meshRemoteCommands()
{
    return remoteCmds;
}
void meshSetRemoteCommands(bool on)
{
    remoteCmds = on;
    saveSettings();
}
bool meshJoinShows()
{
    return joinShows;
}
void meshSetJoinShows(bool on)
{
    joinShows = on;
    saveSettings();
}

// ---- helpers ------------------------------------------------------------------------------
uint32_t meshMyNode()
{
    return nodeDB ? nodeDB->getNodeNum() : 0;
}

const char *meshNodeName(uint32_t node, char *buf, size_t n)
{
    meshtastic_NodeInfoLite *info = nodeDB ? nodeDB->getMeshNode(node) : nullptr;
    if (info && info->has_user && info->user.long_name[0])
        snprintf(buf, n, "%s", info->user.long_name);
    else if (info && info->has_user && info->user.short_name[0])
        snprintf(buf, n, "%s", info->user.short_name);
    else
        snprintf(buf, n, "!%08x", (unsigned)node);
    return buf;
}

bool meshSendPrivate(uint32_t to, const uint8_t *data, size_t len, bool wantAck)
{
    if (!router || !service || len > sizeof(meshtastic_Data_payload_t::bytes))
        return false;
    meshtastic_MeshPacket *p = router->allocForSending();
    if (!p)
        return false;
    p->to = to;
    p->channel = 0;
    p->want_ack = wantAck;
    p->decoded.portnum = BADGE_PORT;
    memcpy(p->decoded.payload.bytes, data, len);
    p->decoded.payload.size = (pb_size_t)len;
    service->sendToMesh(p, RX_SRC_LOCAL, false);
    return true;
}

bool meshSendTextDM(uint32_t to, const char *text, uint8_t channel)
{
    if (!router || !service || !text)
        return false;
    meshtastic_MeshPacket *p = router->allocForSending();
    if (!p)
        return false;
    size_t len = strlen(text);
    if (len > sizeof(meshtastic_Data_payload_t::bytes))
        len = sizeof(meshtastic_Data_payload_t::bytes);
    p->to = to;
    p->channel = channel;
    p->want_ack = true;
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    memcpy(p->decoded.payload.bytes, text, len);
    p->decoded.payload.size = (pb_size_t)len;
    service->sendToMesh(p, RX_SRC_LOCAL, true);
    return true;
}

// ---- light shows --------------------------------------------------------------------------
// "BSL" 1 speed time_ms(4, little endian) name...
void meshShareLightShow()
{
    uint8_t b[40];
    memcpy(b, "BSL", 3);
    b[3] = 1;
    b[4] = (uint8_t)ledsSpeed();
    uint32_t t = ledsSceneTimeMs();
    memcpy(b + 5, &t, 4);
    const char *name = sceneAt(ledsScene()).name;
    size_t n = strnlen(name, 30);
    memcpy(b + 9, name, n);
    meshSendPrivate(NODENUM_BROADCAST, b, 9 + n, false);
    statsAchieve(ACH_SHOW);
}

bool meshLeadingShow()
{
    return leading;
}
void meshSetLeadingShow(bool on)
{
    leading = on;
    if (on)
        meshShareLightShow();
}

static void handleLightShow(const meshtastic_MeshPacket &mp, const uint8_t *b, size_t len)
{
    if (!joinShows || len < 10 || b[0] != 1)
        return;
    char name[32];
    size_t n = len - 6;
    if (n > sizeof(name) - 1)
        n = sizeof(name) - 1;
    memcpy(name, b + 6, n);
    name[n] = 0;
    int idx = sceneFind(name);
    if (idx < 0)
        return;
    uint32_t t;
    memcpy(&t, b + 2, 4);
    // allow for time on air: roughly 0.3 s for the packet plus 0.4 s per relay
    int hops = (mp.hop_start >= mp.hop_limit) ? mp.hop_start - mp.hop_limit : 0;
    t += 300 + 400 * hops;
    ledsSetSceneSynced(idx, b[1], t);
    statsAchieve(ACH_SHOW);
}

// ---- fox hunt -----------------------------------------------------------------------------
void meshFoxStart(uint32_t target)
{
    foxTarget = target;
    fox = {false, 0, 0, 0, -1};
    foxLastSnr = -999;
    foxLastHeard = 0;
    meshFoxPing();
}
void meshFoxStop()
{
    foxTarget = 0;
}
FoxReading meshFoxReading()
{
    return fox;
}
void meshFoxPing()
{
    if (!foxTarget)
        return;
    // any Meshtastic node acknowledges a direct message, and that ACK gives us a fresh reading
    static const uint8_t ping[] = {'B', 'F', 'X', 'P'};
    meshSendPrivate(foxTarget, ping, sizeof(ping), true);
}
static void foxUpdate(int32_t rssi, float snr, int hops)
{
    fox.valid = true;
    if (rssi)
        fox.rssi = rssi;
    fox.snr = snr;
    fox.hops = hops;
    fox.atMs = millis();
    if ((hops == 0 && rssi > -55 && rssi != 0) || (hops == 0 && snr >= 11))
        statsAchieve(ACH_FOX);
    appRedraw();
}
// NodeDB keeps the last SNR of every node it hears, including packets other modules stopped
static void foxPollNodeDB()
{
    if (!foxTarget || !nodeDB)
        return;
    meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(foxTarget);
    if (!n)
        return;
    if (n->snr != foxLastSnr || n->last_heard != foxLastHeard) {
        bool first = foxLastSnr == -999;
        foxLastSnr = n->snr;
        foxLastHeard = n->last_heard;
        if (!first || n->last_heard)
            foxUpdate(0, n->snr, n->has_hops_away ? n->hops_away : -1);
    }
}

void meshSetTttHandler(TttHandler h)
{
    tttHandler = h;
}

// ---- remote commands ----------------------------------------------------------------------
static bool startsWithWord(const char *s, const char *w)
{
    size_t n = strlen(w);
    return strncasecmp(s, w, n) == 0 && (s[n] == 0 || s[n] == ' ');
}

static int findByName(const char *arg, int count, const char *(*name)(int))
{
    for (int i = 0; i < count; i++)
        if (strcasecmp(arg, name(i)) == 0)
            return i;
    return -1;
}

static void handleCommand(const meshtastic_MeshPacket &mp, const char *text)
{
    char reply[200];
    const char *arg = strchr(text, ' ');
    while (arg && *arg == ' ')
        arg++;
    if (startsWithWord(text, "!lights")) {
        if (!arg || !*arg) {
            snprintf(reply, sizeof(reply), "Lights: %s. Try !lights next, !lights list, !lights <name>",
                     sceneAt(ledsScene()).name);
        } else if (strcasecmp(arg, "next") == 0) {
            ledsNextScene();
            snprintf(reply, sizeof(reply), "Lights: %s", sceneAt(ledsScene()).name);
        } else if (strcasecmp(arg, "list") == 0) {
            size_t o = (size_t)snprintf(reply, sizeof(reply), "Scenes:");
            for (int i = 0; i < sceneCount() && o < sizeof(reply) - 20; i++)
                o += (size_t)snprintf(reply + o, sizeof(reply) - o, "%s%s", i ? ", " : " ", sceneAt(i).name);
        } else {
            int i = sceneFind(arg); // any case, old names too
            if (i >= 0) {
                ledsSetScene(i);
                snprintf(reply, sizeof(reply), "Lights: %s", sceneAt(i).name);
            } else {
                snprintf(reply, sizeof(reply), "No scene called '%s'. !lights list", arg);
            }
        }
    } else if (startsWithWord(text, "!bright") && arg) {
        int i = findByName(arg, ledsBrightCount(), ledsBrightName);
        if (i >= 0)
            ledsSetBright(i);
        snprintf(reply, sizeof(reply), "Brightness: %s (dim low medium high max)", ledsBrightName(ledsBright()));
    } else if (startsWithWord(text, "!speed") && arg) {
        int i = findByName(arg, ledsSpeedCount(), ledsSpeedName);
        if (i >= 0)
            ledsSetSpeed(i);
        snprintf(reply, sizeof(reply), "Speed: %s (slowest slow normal fast fastest)", ledsSpeedName(ledsSpeed()));
    } else if (startsWithWord(text, "!help")) {
        snprintf(reply, sizeof(reply), "Badge commands: !lights [next|list|name], !bright <level>, !speed <level>");
    } else {
        return; // not for us
    }
    meshSendTextDM(mp.from, reply, mp.channel);
}

// ---- the module ---------------------------------------------------------------------------
class BadgeMeshModule : public MeshModule, private concurrency::OSThread
{
  public:
    BadgeMeshModule() : MeshModule("badge"), concurrency::OSThread("BadgeMesh")
    {
        isPromiscuous = true; // see broadcasts and packets for others too (LED traffic blink)
    }

  protected:
    bool wantPacket(const meshtastic_MeshPacket *p) override
    {
        return p->which_payload_variant == meshtastic_MeshPacket_decoded_tag;
    }

    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override
    {
        if (isFromUs(&mp))
            return ProcessMessage::CONTINUE;
        ledsMeshPulse();

        int hops = (mp.hop_start > 0 && mp.hop_start >= mp.hop_limit) ? mp.hop_start - mp.hop_limit : -1;
        if (hops >= 3)
            statsAchieve(ACH_FAR);
        if (mp.from == foxTarget && mp.from)
            foxUpdate(mp.rx_rssi, mp.rx_snr, hops);
        checkNewBadge(mp.from);

        const auto &d = mp.decoded;
        if (d.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
            bool dm = isToUs(&mp) && !isBroadcast(mp.to);
            ledsMessageAlert(dm);
            if (dm && remoteCmds && d.payload.size > 1 && d.payload.bytes[0] == '!') {
                char text[sizeof(d.payload.bytes) + 1];
                memcpy(text, d.payload.bytes, d.payload.size);
                text[d.payload.size] = 0;
                handleCommand(mp, text);
            }
        } else if (d.portnum == BADGE_PORT && d.payload.size >= 3) {
            const uint8_t *b = d.payload.bytes;
            size_t len = d.payload.size;
            if (memcmp(b, "BSL", 3) == 0) {
                handleLightShow(mp, b + 3, len - 3);
            } else if (memcmp(b, "BTT", 3) == 0 && isToUs(&mp) && tttHandler) {
                tttHandler(mp.from, b + 3, len - 3);
            } else if (memcmp(b, "BHN", 3) == 0) {
                int f = huntHandlePacket(b + 3, len - 3);
                if (f >= 0 && !statsHasFlag(f)) {
                    statsSetFlag(f);
                    static char msg[160];
                    snprintf(msg, sizeof(msg), "Hunt flag %d/%d: %s\n%s", statsFlagCount(), huntFlagCount(),
                             huntFlag(f).name, huntFlag(f).clue);
                    ledsFlourish();
                    if (screen && !appActive())
                        screen->showSimpleBanner(msg, 8000);
                }
            }
        }
        return ProcessMessage::CONTINUE;
    }

    int32_t runOnce() override
    {
        uint32_t now = millis();
        foxPollNodeDB();
        if (foxTarget && now - lastFoxPing > 20000) {
            lastFoxPing = now;
            meshFoxPing();
        }
        if (leading && now - lastShow > 60000) {
            lastShow = now;
            meshShareLightShow();
        }
        uint8_t beacon[16];
        size_t bl = huntBeaconPacket(beacon, sizeof(beacon));
        if (bl && now - lastBeacon > 180000) {
            lastBeacon = now;
            meshSendPrivate(NODENUM_BROADCAST, beacon, bl, false);
        }
        return 1000;
    }

  private:
    uint32_t lastFoxPing = 0, lastShow = 0, lastBeacon = 0;

    void checkNewBadge(uint32_t from)
    {
        if (!from || !nodeDB)
            return;
        meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(from);
        if (!n || !n->has_user || n->user.hw_model != meshtastic_HardwareModel_PRIVATE_HW)
            return;
        if (!statsMetBadge(from))
            return;
        ledsFlourish();
        static char msg[80];
        char name[40];
        snprintf(msg, sizeof(msg), "New badge met!\n%s (%d)", meshNodeName(from, name, sizeof(name)), statsBadgeCount());
        if (screen && !appActive())
            screen->showSimpleBanner(msg, 3000);
    }
};

static BadgeMeshModule *module = nullptr;

void meshStart()
{
    loadSettings();
    if (!module)
        module = new BadgeMeshModule();
}

} // namespace badge

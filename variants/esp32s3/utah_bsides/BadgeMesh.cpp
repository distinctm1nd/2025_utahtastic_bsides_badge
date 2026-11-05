// Badge mesh module. See BadgeMesh.h.
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeHunt.h"
#include "BadgeLeds.h"
#include "BadgeMesh.h"
#include "BadgeCapture.h"
#include "mesh/Throttle.h"
#include "BadgeScenes.h"
#include "BadgeStats.h"
#include "BadgeTime.h"

#include "DisplayFormatters.h"
#include "MeshService.h"
#include "MeshTypes.h"
#include "NodeDB.h"
#include "mesh/Channels.h"
#include "mesh/RadioLibInterface.h"
#include "Router.h"
#include "concurrency/OSThread.h"
#include "graphics/Screen.h"
#include "MeshModule.h"
#include "airtime.h"
#include "modules/NodeInfoModule.h"
#include <Preferences.h>
#include <ctype.h>
#include <strings.h>
#include <stdio.h>
#include <string.h>


namespace graphics
{
namespace MessageRenderer
{
extern uint32_t scrollStartDelay;
extern bool scrollStarted;
} // namespace MessageRenderer
} // namespace graphics

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
static SightingHandler sightingHandler = nullptr;
static bool beingFox = false;

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
    if (info && nodeInfoLiteHasUser(info) && info->long_name[0])
        snprintf(buf, n, "%s", info->long_name);
    else if (info && nodeInfoLiteHasUser(info) && info->short_name[0])
        snprintf(buf, n, "%s", info->short_name);
    else
        snprintf(buf, n, "!%08x", (unsigned)node);
    return buf;
}

bool meshSendPrivate(uint32_t to, const uint8_t *data, size_t len, bool wantAck, int hopLimit)
{
    if (!router || !service || len > sizeof(meshtastic_Data_payload_t::bytes))
        return false;
    meshtastic_MeshPacket *p = router->allocForSending();
    if (!p)
        return false;
    p->to = to;
    p->channel = 0;
    p->want_ack = wantAck;
    if (hopLimit >= 0)
        p->hop_limit = hopLimit;
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

void meshSetSightingHandler(SightingHandler h)
{
    sightingHandler = h;
}
bool meshBeingFox()
{
    return beingFox;
}
void meshSetBeingFox(bool on)
{
    beingFox = on;
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
// Meshtastic's Messages page scrolls by itself 2 s after it's shown (newest message at the top,
// then down through older ones). Keep its start delay fresh so it stays on the newest; the
// joystick still scrolls it by hand.
static void holdMessageScroll()
{
    ::graphics::MessageRenderer::scrollStartDelay = millis();
    ::graphics::MessageRenderer::scrollStarted = false;
}

static struct {
    uint32_t node = 0, at = 0;
    int rssi = 0, hops = -1;
    float snr = 0;
} lastHeard;

// every node heard over the air this session, decoded or not (the packet header -- sender,
// signal, hops -- is in the clear even when we lack the channel key). Lets the LoRa node list
// show nodes on other channels that share our frequency.
static const int HEARD_MAX = 300;
static HeardNode *heard = nullptr; // lazily placed in PSRAM (big; keeps internal RAM free for WiFi)
static int heardN = 0;
static portMUX_TYPE heardLock = portMUX_INITIALIZER_UNLOCKED;

static void noteHeard(uint32_t num, int rssi, float snr, int hops, bool decoded, uint16_t chHash)
{
    if (!num)
        return;
    if (!heard) {
        heard = (HeardNode *)heap_caps_calloc(HEARD_MAX, sizeof(HeardNode), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!heard)
            heard = (HeardNode *)calloc(HEARD_MAX, sizeof(HeardNode));
        if (!heard)
            return;
    }
    uint32_t now = millis();
    portENTER_CRITICAL(&heardLock);
    int slot = -1, oldest = 0;
    for (int i = 0; i < heardN; i++) {
        if (heard[i].num == num) {
            slot = i;
            break;
        }
        if (heard[i].lastMs < heard[oldest].lastMs)
            oldest = i;
    }
    if (slot < 0)
        slot = heardN < HEARD_MAX ? heardN++ : oldest;
    HeardNode &h = heard[slot];
    h.num = num;
    h.rssi = rssi;
    h.snr = snr;
    h.hops = hops;
    h.lastMs = now;
    h.chHash = chHash;
    h.preset = (uint8_t)config.lora.modem_preset;
    h.decoded = h.decoded || decoded; // once decoded, stays known
    portEXIT_CRITICAL(&heardLock);
}

// ---- live modem-preset sweeping for the node list ------------------------------------------
static int savedPreset = -1; // the badge's own preset, saved while we listen on others

void meshListenPreset(int preset)
{
    if (savedPreset < 0)
        savedPreset = config.lora.modem_preset;
    config.lora.use_preset = true;
    config.lora.modem_preset = (meshtastic_Config_LoRaConfig_ModemPreset)preset;
    if (RadioLibInterface::instance)
        RadioLibInterface::instance->reconfigure(); // retune + resume receive, no reboot, no save
}

int meshCurrentPreset()
{
    return config.lora.modem_preset;
}

void meshRestorePreset()
{
    if (savedPreset < 0)
        return;
    config.lora.modem_preset = (meshtastic_Config_LoRaConfig_ModemPreset)savedPreset;
    savedPreset = -1;
    if (RadioLibInterface::instance)
        RadioLibInterface::instance->reconfigure();
}

// the presets worth sweeping, in range order (skips deprecated / niche ones)
static const int SWEEP_PRESETS[] = {
    meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW,     meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE,
    meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST,     meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW,
    meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST,   meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW,
    meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST,    meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO};

int meshPresetCount()
{
    return sizeof(SWEEP_PRESETS) / sizeof(SWEEP_PRESETS[0]);
}
int meshPresetAt(int i)
{
    return (i >= 0 && i < meshPresetCount()) ? SWEEP_PRESETS[i] : meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
}
const char *meshPresetName(int preset)
{
    return DisplayFormatters::getModemPresetDisplayName((meshtastic_Config_LoRaConfig_ModemPreset)preset, false, true);
}

int meshHeardList(HeardNode *out, int max)
{
    if (!heard)
        return 0;
    int n = 0;
    portENTER_CRITICAL(&heardLock);
    for (int i = 0; i < heardN && n < max; i++)
        out[n++] = heard[i];
    portEXIT_CRITICAL(&heardLock);
    return n;
}

void meshRequestNodeInfo(uint32_t node, uint8_t channel)
{
    static uint32_t lastNode = 0, lastAt = 0;
    if (!node || (node == lastNode && Throttle::isWithinTimespanMs(lastAt, 60000)))
        return; // once a minute per node is plenty
    lastNode = node;
    lastAt = millis();
    if (nodeInfoModule && airTime && airTime->isTxAllowedChannelUtil(true))
        nodeInfoModule->sendOurNodeInfo(node, true, channel, true); // ours, and asks for theirs back
}

// ---- channel invites: a channel's name and key, sent to one badge end to end encrypted --------
// "BCI" + name length + name + key length + key. Sent only as a PKI direct message (Meshtastic
// refuses rather than falling back to the channel key), and accepted only if it arrived that way.
static struct {
    bool pending = false;
    uint32_t from = 0;
    char name[12] = {};
    uint8_t psk[32] = {};
    uint8_t len = 0;
} invite;

bool meshSendInvite(uint32_t to, uint8_t channelIndex)
{
    if (!router || !service || !to || isBroadcast(to))
        return false;
    const meshtastic_Channel &c = channels.getByIndex((ChannelIndex)channelIndex);
    const char *name = channels.getName(channelIndex);
    size_t nl = strlen(name), kl = c.settings.psk.size;
    if (nl > 11 || kl > 32)
        return false;
    meshtastic_MeshPacket *p = router->allocForSending();
    if (!p)
        return false;
    uint8_t *b = p->decoded.payload.bytes;
    size_t n = 0;
    memcpy(b, "BCI", 3);
    n = 3;
    b[n++] = (uint8_t)nl;
    memcpy(b + n, name, nl);
    n += nl;
    b[n++] = (uint8_t)kl;
    memcpy(b + n, c.settings.psk.bytes, kl);
    n += kl;
    p->to = to;
    p->channel = 0;
    p->want_ack = true;
    p->pki_encrypted = true; // end to end or not at all: never readable by the rest of a channel
    p->decoded.portnum = BADGE_PORT;
    p->decoded.payload.size = (pb_size_t)n;
    service->sendToMesh(p, RX_SRC_LOCAL, false);
    return true;
}

static void inviteReceived(const meshtastic_MeshPacket &mp, const uint8_t *b, size_t len)
{
    if (!mp.pki_encrypted || !isToUs(&mp) || isBroadcast(mp.to) || len < 2)
        return; // only ever from one badge to this one, end to end encrypted
    size_t nl = b[0];
    if (nl == 0 || nl > 11 || 1 + nl + 1 > len)
        return;
    size_t kl = b[1 + nl];
    if ((kl != 16 && kl != 32) || 2 + nl + kl > len)
        return; // private groups only (a real key)
    memcpy(invite.name, b + 1, nl);
    invite.name[nl] = 0;
    memcpy(invite.psk, b + 2 + nl, kl);
    invite.len = (uint8_t)kl;
    invite.from = mp.from;
    invite.pending = true;
    ledsMessageAlert(true);
}

bool meshTakeInvite(uint32_t &from, char *name, size_t nameSize, uint8_t *psk, int &len)
{
    if (!invite.pending)
        return false;
    invite.pending = false;
    from = invite.from;
    snprintf(name, nameSize, "%s", invite.name);
    memcpy(psk, invite.psk, invite.len);
    len = invite.len;
    memset(invite.psk, 0, sizeof(invite.psk));
    return true;
}

bool meshInvitePending()
{
    return invite.pending;
}

bool meshLastHeard(uint32_t &node, int &rssi, float &snr, int &hops, uint32_t &ageMs)
{
    if (!lastHeard.node)
        return false;
    node = lastHeard.node, rssi = lastHeard.rssi, snr = lastHeard.snr, hops = lastHeard.hops;
    ageMs = millis() - lastHeard.at;
    return true;
}

class BadgeMeshModule : public MeshModule, private concurrency::OSThread
{
  public:
    BadgeMeshModule() : MeshModule("badge"), concurrency::OSThread("BadgeMesh")
    {
        isPromiscuous = true; // see broadcasts and packets for others too (capture, sightings)
        encryptedOk = true;   // also see packets we can't decrypt (other channels on our frequency)
    }

  protected:
    bool wantUIFrame() override { return true; }
    void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) override
    {
        (void)state;
        badgePageDraw(display, x, y);
    }

    bool wantPacket(const meshtastic_MeshPacket *p) override
    {
        return p->which_payload_variant == meshtastic_MeshPacket_decoded_tag ||
               p->which_payload_variant == meshtastic_MeshPacket_encrypted_tag;
    }

    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override
    {
        if (isFromUs(&mp))
            return ProcessMessage::CONTINUE;

        bool decoded = mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag;
        int hops = (mp.hop_start > 0 && mp.hop_start >= mp.hop_limit) ? mp.hop_start - mp.hop_limit : -1;

        // record every sender heard over the air (clear header), decoded or not
        noteHeard(mp.from, mp.rx_rssi, mp.rx_snr, hops, decoded, decoded ? 0 : mp.channel);
        if (mp.from && mp.rx_rssi) {
            lastHeard.node = mp.from;
            lastHeard.rssi = mp.rx_rssi;
            lastHeard.snr = mp.rx_snr;
            lastHeard.hops = hops;
            lastHeard.at = millis();
        }
        if (mp.from == foxTarget && mp.from)
            foxUpdate(mp.rx_rssi, mp.rx_snr, hops); // the fox hunt works on any node on our frequency

        if (!decoded) // can't read the payload (another channel's key): the header was enough
            return ProcessMessage::CONTINUE;

        capturePacket(mp);
        if (hops >= 3)
            statsAchieve(ACH_FAR);
        checkNewBadge(mp.from);

        const auto &d = mp.decoded;
        if (d.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
            bool dm = isToUs(&mp) && !isBroadcast(mp.to);
            ledsMessageAlert(dm);
            wantName(mp.from, mp.channel);
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
            } else if (memcmp(b, "BSG", 3) == 0 && sightingHandler) {
                sightingHandler(mp.from, b + 3, len - 3);
            } else if (memcmp(b, "BTT", 3) == 0 && isToUs(&mp) && tttHandler) {
                tttHandler(mp.from, b + 3, len - 3);
            } else if (memcmp(b, "BCI", 3) == 0) {
                inviteReceived(mp, b + 3, len - 3);
            } else if (memcmp(b, "BTQ", 3) == 0 || memcmp(b, "BTM", 3) == 0) {
                timePacket(mp.from, b, len);
            } else if (memcmp(b, "BHN", 3) == 0) {
                int f = huntHandlePacket(b + 3, len - 3);
                // only straight from the beacon (not relayed), and only up close
                bool direct = mp.hop_start == 0 || mp.hop_start == mp.hop_limit;
                if (f >= 0 && !statsHasFlag(f) && direct && mp.rx_rssi && mp.rx_rssi < HUNT_MIN_RSSI) {
                    if (!Throttle::isWithinTimespanMs(lastHuntHint, 60000) && screen && !appActive()) {
                        lastHuntHint = millis();
                        static char hint[80];
                        snprintf(hint, sizeof(hint), "Hunt flag nearby:\n%s\nGet closer!", huntFlag(f).name);
                        screen->showSimpleBanner(hint, 4000);
                    }
                } else if (f >= 0 && !statsHasFlag(f) && direct) {
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
        timeTick();
        holdMessageScroll();
        askNames(now);
        if (foxTarget && now - lastFoxPing > 20000) {
            lastFoxPing = now;
            meshFoxPing();
        }
        if (leading && now - lastShow > 60000) {
            lastShow = now;
            meshShareLightShow();
        }
        if (beingFox && !Throttle::isWithinTimespanMs(lastFox, 15000)) {
            lastFox = now;
            static const uint8_t fox[] = {'B', 'F', 'X', 'B'};
            meshSendPrivate(NODENUM_BROADCAST, fox, sizeof(fox), false, 0);
        }
        uint8_t beacon[16];
        size_t bl = huntBeaconPacket(beacon, sizeof(beacon));
        if (bl && !Throttle::isWithinTimespanMs(lastBeacon, HUNT_BEACON_MS)) {
            lastBeacon = now;
            meshSendPrivate(NODENUM_BROADCAST, beacon, bl, false, 0); // hop 0: never relayed
        }
        return 1000;
    }

  private:
    uint32_t lastFoxPing = 0, lastShow = 0, lastBeacon = 0, lastFox = 0, lastHuntHint = 0;

    // A message from a node we have no name for shows its number: Meshtastic asks it once for
    // its node info; ask again a few times in case that request or the reply was lost.
    struct NameAsk {
        uint32_t node, lastAsk;
        uint8_t channel, tries;
    };
    NameAsk nameAsks[6] = {};

    static bool hasName(uint32_t node)
    {
        const meshtastic_NodeInfoLite *n = nodeDB ? nodeDB->getMeshNode(node) : nullptr;
        return nodeInfoLiteHasUser(n) && (n->long_name[0] || n->short_name[0]);
    }
    void wantName(uint32_t node, uint8_t channel)
    {
        if (!node || hasName(node))
            return;
        NameAsk *slot = nullptr;
        for (auto &a : nameAsks) {
            if (a.node == node)
                return;
            if (!a.node && !slot)
                slot = &a;
        }
        if (slot)
            *slot = {node, millis(), channel, 0}; // Meshtastic's own request counts as the first
    }
    void askNames(uint32_t now)
    {
        for (auto &a : nameAsks) {
            if (!a.node)
                continue;
            if (hasName(a.node) || a.tries >= 3) {
                a.node = 0;
                continue;
            }
            if (Throttle::isWithinTimespanMs(a.lastAsk, a.tries ? 300000 : 90000))
                continue;
            a.lastAsk = now;
            a.tries++;
            if (nodeInfoModule && airTime && airTime->isTxAllowedChannelUtil(true))
                nodeInfoModule->sendOurNodeInfo(a.node, true, a.channel, true);
        }
    }

    void checkNewBadge(uint32_t from)
    {
        if (!from || !nodeDB)
            return;
        meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(from);
        if (!nodeInfoLiteHasUser(n) || n->hw_model != meshtastic_HardwareModel_PRIVATE_HW)
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

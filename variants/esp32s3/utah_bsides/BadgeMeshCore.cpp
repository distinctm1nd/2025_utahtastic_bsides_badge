#include "configuration.h"

#include "BadgeMeshCore.h"

#include <Preferences.h>

#if !BADGE_MESHCORE

extern uint32_t rebootAtMsec;
extern bool suppressRebootBanner; // src/main.cpp

namespace badge
{
bool meshcoreBuilt()
{
    return false;
}
bool meshcoreMode()
{
    return false;
}
const char *meshName()
{
    return "Meshtastic";
}
void meshcoreStart() {}
void meshcoreSetMode(bool) {}
bool meshcoreSendText(uint32_t, const char *, uint8_t)
{
    return false;
}
void meshcorePause(bool) {}
bool meshcoreNameOnly(uint32_t)
{
    return false;
}
bool meshcoreHidesNode(uint32_t)
{
    return false;
}
McRadioCfg meshcoreRadio()
{
    return McRadioCfg();
}
bool meshcoreSetRadio(const McRadioCfg &)
{
    return false;
}
void meshcoreAdvertise(bool) {}
int meshcoreContactCount()
{
    return 0;
}
bool meshcoreContact(int, McContact &)
{
    return false;
}
bool meshcoreContactFav(int, bool)
{
    return false;
}
bool meshcoreContactResetPath(int)
{
    return false;
}
bool meshcoreContactDelete(int)
{
    return false;
}
int meshcoreHeardCount()
{
    return 0;
}
const char *meshcoreHeardName(int)
{
    return "";
}
bool meshcoreHeardAdd(int)
{
    return false;
}
bool meshcoreAutoAdd()
{
    return true;
}
void meshcoreSetAutoAdd(bool) {}
bool meshcoreShareLocation()
{
    return false;
}
void meshcoreSetShareLocation(bool) {}
void meshcoreHashtagKey(const char *, uint8_t *) {}
void openMeshBootChoice(void (*then)())
{
    if (then)
        then();
}
bool meshcoreResumeChoice()
{
    return false;
}
const char *meshcoreLoadingInto()
{
    return nullptr;
}
bool meshBootPicked()
{
    return false;
}
} // namespace badge

#else

#include "BadgeApp.h"
#include "BadgeMesh.h"
#include "FSCommon.h"
#include "MessageStore.h"
#include "NodeDB.h"
#include "SPILock.h"
#include "concurrency/OSThread.h"
#include "gps/RTC.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "main.h"
#include "mesh/Channels.h"
#include "mesh/MeshModule.h"
#include "mesh/RadioLibInterface.h"
#include "mesh/Throttle.h"
#include "mesh/mesh-pb-constants.h"
#include "modules/TextMessageModule.h"
#include "nimble/NimbleBluetooth.h"
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <esp_mac.h>
#include <nvs.h>

#include <Arduino.h>
#include <esp_random.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/BaseChatMesh.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/radiolib/RadioLibWrappers.h>

namespace badge
{

// PROOF OF CONCEPT: MC_FORCE starts MeshCore mode regardless of the saved setting.
#ifndef MC_FORCE
#define MC_FORCE 0
#endif

static bool modeOn = false;    // this run
static bool savedMode = false; // the choice offered first at startup

bool meshcoreBuilt()
{
    return true;
}
bool meshcoreMode()
{
    return modeOn;
}
const char *meshName()
{
    return modeOn ? "MeshCore" : "Meshtastic";
}
void meshcoreSetMode(bool on)
{
    savedMode = on;
    Preferences p;
    if (p.begin("utah", false)) {
        p.putInt("meshmode", on ? 1 : 0);
        p.end();
    }
}

// Meshtastic keeps its RadioLib object and its upkeep timers protected; member pointers named
// through a derived class reach them without changing Meshtastic's code (as BadgeRfSniff does).
struct McPeek : RadioLibInterface {
    static PhysicalLayer *radio(RadioLibInterface *r) { return r->*(&McPeek::iface); }
    // Meshtastic's periodic upkeep resets a radio it thinks has gone deaf, which would put the
    // chip back on Meshtastic's settings. Keep telling it the radio was heard from just now.
    static bool receiving(RadioLibInterface *r) { return r->*(&McPeek::isReceiving); }
    static void keepQuiet(RadioLibInterface *r)
    {
        r->*(&McPeek::lastRxGoodMs) = millis();
        r->*(&McPeek::lastAgcResetMs) = millis();
    }
};

class McBoard : public mesh::MainBoard
{
  public:
    uint16_t getBattMilliVolts() override { return 0; }
    const char *getManufacturerName() const override { return "Utah BSides badge"; }
    void reboot() override { ESP.restart(); }
    uint8_t getStartupReason() const override { return BD_STARTUP_NORMAL; }
};

// MeshCore's radio driver over the SX1262 / LLCC68 object Meshtastic created.
class McRadio : public RadioLibWrapper
{
  public:
    McRadio(SX1262 &radio, mesh::MainBoard &board) : RadioLibWrapper(radio, board) {}
    SX1262 *sx() const { return static_cast<SX1262 *>(_radio); }

    void setParams(float freq, float bw, uint8_t sf, uint8_t cr) override
    {
        sx()->setFrequency(freq);
        sx()->setBandwidth(bw);
        sx()->setSpreadingFactor(sf);
        sx()->setCodingRate(cr);
        sf_ = sf;
        updatePreamble(sf);
        PacketMillis pm = calcMaxPacketMillis(sf, bw, cr, preambleLengthForSF(sf));
        preambleMs = pm.preambleMillis;
        payloadMs = pm.payloadMillis;
    }
    // a packet is arriving: preamble or header seen, with MeshCore's guard against flags left stuck
    bool isReceivingPacket() override
    {
        uint32_t irq = sx()->getIrqFlags();
        const uint32_t ALL = RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED | RADIOLIB_SX126X_IRQ_HEADER_VALID |
                             RADIOLIB_SX126X_IRQ_HEADER_ERR | RADIOLIB_SX126X_IRQ_SYNC_WORD_VALID;
        bool preamble = irq & RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED, header = irq & RADIOLIB_SX126X_IRQ_HEADER_VALID;
        if ((irq & RADIOLIB_SX126X_IRQ_HEADER_ERR) || (!header && headerSeen)) {
            if (irq & RADIOLIB_SX126X_IRQ_HEADER_ERR)
                sx()->clearIrqFlags(ALL);
            activityAt = 0;
            headerSeen = false;
            return false;
        }
        if (header || preamble) {
            if (header && !headerSeen) {
                headerSeen = true;
                activityAt = millis();
            } else if (!activityAt) {
                activityAt = millis();
            }
            if (Throttle::hasElapsed(activityAt, header ? payloadMs : preambleMs)) {
                sx()->clearIrqFlags(header ? ALL : RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED);
                activityAt = 0;
                headerSeen = false;
                return false;
            }
            return true;
        }
        activityAt = 0;
        headerSeen = false;
        return false;
    }
    float getCurrentRSSI() override { return sx()->getRSSI(false); }
    float getLastRSSI() const override { return sx()->getRSSI(); }
    float getLastSNR() const override { return sx()->getSNR(); }
    uint8_t getSpreadingFactor() const override { return sf_; }
    float packetScore(float snr, int packet_len) override { return packetScoreInt(snr, sf_, packet_len); }

  private:
    uint8_t sf_ = LORA_SF;
    uint32_t preambleMs = 66, payloadMs = 3934, activityAt = 0;
    bool headerSeen = false;
};

// ---- MeshCore under the badge's apps ----------------------------------------------------------
// The badge's apps (messages, node list, ...) work on Meshtastic's data: nodes in NodeDB, messages
// in its message store. In MeshCore mode this bridges MeshCore into that same data, so the apps run
// unchanged:
//   * a MeshCore contact is a node, numbered by the first four bytes of its public key
//   * a MeshCore message arrives as a Meshtastic text packet handed to Meshtastic's modules, so it
//     is stored, shown and announced exactly like a Meshtastic one
//   * Meshtastic channel N is MeshCore group channel N with the same key (the default public
//     channel is MeshCore's public channel), so two badges with the same channels chat on either
//   * what the badge sends (BadgeMesh's meshSendTextDM) goes out through meshcoreSendText()
static const char *const ID_FILE = "/badge/mc_identity";
static const char *const CONTACTS_FILE = "/badge/mc_contacts";
// MeshCore's public channel key ("izOH6cXN6mrJ5e26oRXNcg==")
static const uint8_t PUBLIC_KEY[16] = {0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
                                       0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72};

class McRng : public mesh::RNG
{
  public:
    void random(uint8_t *dest, size_t sz) override { esp_fill_random(dest, sz); }
};

// MeshCore wants a clock that never runs backwards across restarts (adverts carry a time, and an
// older one than last seen is ignored). Meshtastic's clock when it is set (phone / NTP / the Badge
// WiFi page); otherwise the last time saved in NVS ("utah" / "mctime") plus the time since startup.
class McClock : public mesh::RTCClock
{
  public:
    void begin()
    {
        Preferences p;
        if (p.begin("utah", true)) {
            base = p.getUInt("mctime", 1794000000u) + 60; // a restart always moves it on
            p.end();
        }
    }
    uint32_t getCurrentTime() override
    {
        uint32_t t = getValidTime(RTCQualityDevice);
        if (t > 1700000000u)
            return t;
        return base + millis() / 1000;
    }
    void setCurrentTime(uint32_t t) override { base = t - millis() / 1000; }
    void tick() override
    {
        if (!Throttle::hasElapsed(savedAt, 10 * 60 * 1000))
            return;
        savedAt = millis();
        Preferences p;
        if (p.begin("utah", false)) {
            p.putUInt("mctime", getCurrentTime());
            p.end();
        }
    }

  private:
    uint32_t base = 1794000000u, savedAt = 0;
};

// The node number a MeshCore identity gets among the badge's nodes.
static uint32_t nodeNumFor(const uint8_t *pubKey)
{
    uint32_t n = ((uint32_t)pubKey[0] << 24) | ((uint32_t)pubKey[1] << 16) | ((uint32_t)pubKey[2] << 8) | pubKey[3];
    if (n < 16 || n == NODENUM_BROADCAST || (nodeDB && n == nodeDB->getNodeNum()))
        n ^= 0x40000000u; // off the reserved numbers and our own
    return n;
}
// ... and for a name seen only in a group message (MeshCore group messages carry a name, not a key)
static uint32_t nodeNumForName(const char *name)
{
    uint32_t h = 2166136261u;
    for (const char *c = name; *c; c++)
        h = (h ^ (uint8_t)*c) * 16777619u;
    h |= 0x80000000u;
    if (h == NODENUM_BROADCAST || (nodeDB && h == nodeDB->getNodeNum()))
        h ^= 0x100;
    return h;
}

// ---- keeping the two meshes apart ---------------------------------------------------------------
// MeshCore's nodes live among Meshtastic's (the apps read them there), so which ones are MeshCore's
// is written down (/badge/mc_nodes: number + name). In Meshtastic mode they are taken out of the
// node list; in MeshCore mode they are put back and Meshtastic's are hidden from the badge's lists.
static const char *const NODES_FILE = "/badge/mc_nodes";
struct McNode {
    uint32_t num;
    char name[32];
};
static const int MAX_MC_NODES = 160;
static McNode *mcNodes = nullptr;
static int nMcNodes = 0;
static bool mcNodesDirty = false;

static void mcNodesLoad()
{
    if (!mcNodes)
        mcNodes = psramArray<McNode>(MAX_MC_NODES);
    nMcNodes = 0;
    if (!mcNodes)
        return;
    concurrency::LockGuard g(spiLock);
    File f = FSCom.open(NODES_FILE, FILE_O_READ);
    if (!f)
        return;
    while (nMcNodes < MAX_MC_NODES && f.read((uint8_t *)&mcNodes[nMcNodes], sizeof(McNode)) == (int)sizeof(McNode)) {
        mcNodes[nMcNodes].name[sizeof(mcNodes[0].name) - 1] = 0;
        nMcNodes++;
    }
    f.close();
}
static void mcNodesSave()
{
    mcNodesDirty = false;
    if (!mcNodes)
        return;
    concurrency::LockGuard g(spiLock);
    if (!FSCom.exists("/badge"))
        FSCom.mkdir("/badge");
    File f = FSCom.open(NODES_FILE, FILE_O_WRITE);
    if (!f)
        return;
    f.write((const uint8_t *)mcNodes, sizeof(McNode) * nMcNodes);
    f.close();
}
static int mcNodeIndex(uint32_t num)
{
    for (int i = 0; i < nMcNodes; i++)
        if (mcNodes[i].num == num)
            return i;
    return -1;
}
static void mcNodeForget(uint32_t num)
{
    int i = mcNodeIndex(num);
    if (i < 0)
        return;
    mcNodes[i] = mcNodes[--nMcNodes];
    mcNodesDirty = true;
}

static void ensureNode(uint32_t num, const char *name)
{
    if (!nodeDB || !name || !name[0])
        return;
    if (mcNodes) { // write down that this node is MeshCore's
        int i = mcNodeIndex(num);
        if (i < 0 && nMcNodes < MAX_MC_NODES)
            i = nMcNodes++, mcNodes[i].num = num, mcNodes[i].name[0] = 0;
        if (i >= 0 && strncmp(mcNodes[i].name, name, sizeof(mcNodes[i].name) - 1)) {
            strlcpy(mcNodes[i].name, name, sizeof(mcNodes[i].name));
            mcNodesDirty = true;
        }
    }
    const meshtastic_NodeInfoLite *nd = nodeDB->getMeshNode(num);
    if (nd && nodeInfoLiteHasUser(nd) && !strncmp(nd->long_name, name, sizeof(nd->long_name) - 1))
        return;
    meshtastic_User u = meshtastic_User_init_default;
    snprintf(u.id, sizeof(u.id), "!%08x", (unsigned)num);
    strlcpy(u.long_name, name, sizeof(u.long_name));
    strlcpy(u.short_name, name, sizeof(u.short_name)); // the first few characters
    u.hw_model = meshtastic_HardwareModel_PRIVATE_HW;
    nodeDB->updateUser(num, u, 0);
}

// Entering a mode: MeshCore's nodes are in the node list in MeshCore mode only.
static void mcNodesFor(bool meshcore)
{
    mcNodesLoad();
    if (!nodeDB)
        return;
    for (int i = 0; i < nMcNodes; i++) {
        if (meshcore)
            ensureNode(mcNodes[i].num, mcNodes[i].name);
        else if (nodeDB->getMeshNode(mcNodes[i].num))
            nodeDB->removeNodeByNum(mcNodes[i].num);
    }
}

// Each mesh has its own channel list. Meshtastic keeps one, in its channels file; the other mesh's
// is kept beside it (/badge/chan_mt, /badge/chan_mc) and the two are swapped when the mesh changes.
// NVS "utah" / "chanset" says whose list Meshtastic's file holds now: 0 Meshtastic's, 1 MeshCore's,
// +16 while a swap is under way (so a power cut mid-swap is finished at the next start, and the
// list that was saved first is never overwritten by the other).
static const char *chanFile(bool meshcore)
{
    return meshcore ? "/badge/chan_mc" : "/badge/chan_mt";
}
static bool chanWrite(bool meshcore)
{
    static uint8_t buf[meshtastic_ChannelFile_size];
    size_t n = pb_encode_to_bytes(buf, sizeof(buf), &meshtastic_ChannelFile_msg, &channelFile);
    if (!n)
        return false;
    concurrency::LockGuard g(spiLock);
    if (!FSCom.exists("/badge"))
        FSCom.mkdir("/badge");
    File f = FSCom.open(chanFile(meshcore), FILE_O_WRITE);
    if (!f)
        return false;
    bool ok = f.write(buf, n) == n;
    f.close();
    return ok;
}
static bool chanRead(bool meshcore)
{
    static uint8_t buf[meshtastic_ChannelFile_size];
    size_t n = 0;
    {
        concurrency::LockGuard g(spiLock);
        File f = FSCom.open(chanFile(meshcore), FILE_O_READ);
        if (!f)
            return false;
        n = f.read(buf, sizeof(buf));
        f.close();
    }
    meshtastic_ChannelFile loaded = meshtastic_ChannelFile_init_default;
    if (!n || !pb_decode_from_bytes(buf, n, &meshtastic_ChannelFile_msg, &loaded))
        return false;
    channelFile = loaded;
    return true;
}
static void channelsFor(bool meshcore)
{
    int state = 0;
    {
        Preferences p;
        if (p.begin("utah", true)) {
            state = p.getInt("chanset", 0);
            p.end();
        }
    }
    auto setState = [](int v) {
        Preferences p;
        if (p.begin("utah", false)) {
            p.putInt("chanset", v);
            p.end();
        }
    };
    bool holds = state & 1, swapping = state & 16;
    if (!swapping && holds == meshcore)
        return; // the right list is already in place
    if (!swapping) {
        if (!chanWrite(holds)) // put away the list that is in place now...
            return;            // ...and without that copy nothing is swapped
        setState((meshcore ? 1 : 0) | 16);
    } else {
        meshcore = holds; // finish the swap that was under way, to the list it was heading for
    }
    if (!chanRead(meshcore)) {
        if (meshcore) { // MeshCore's first time: just the default channel, which is its #Public
            channelFile = meshtastic_ChannelFile_init_default;
            channels.initDefaults();
        }
        // (no saved Meshtastic list: keep what is there)
    }
    channels.onConfigChanged();
    if (nodeDB)
        nodeDB->saveToDisk(SEGMENT_CHANNELS);
    setState(meshcore ? 1 : 0);
}

// MeshCore's text, to the badge exactly as a Meshtastic text would arrive - but only to the badge:
// it goes to the two modules that store, show and announce messages, not through Meshtastic's
// whole receive path, which would also pass it to a connected Meshtastic phone app.
struct TextPeek : TextMessageModule {
    static void deliver(TextMessageModule *m, const meshtastic_MeshPacket &mp) { (m->*(&TextPeek::handleReceived))(mp); }
};
static void deliverText(uint32_t from, uint32_t to, uint8_t channel, const char *text, float snr, float rssi, int hops)
{
    meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_default;
    mp.from = from;
    mp.to = to;
    mp.channel = channel;
    mp.id = esp_random();
    mp.rx_time = getValidTime(RTCQualityFromNet);
    mp.rx_snr = snr;
    mp.rx_rssi = (int32_t)rssi;
    mp.hop_start = (uint8_t)(hops < 0 ? 0 : hops > 7 ? 7 : hops);
    mp.hop_limit = 0;
    mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    mp.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    size_t len = strlen(text);
    if (len > sizeof(mp.decoded.payload.bytes))
        len = sizeof(mp.decoded.payload.bytes);
    memcpy(mp.decoded.payload.bytes, text, len);
    mp.decoded.payload.size = (pb_size_t)len;
    if (nodeDB)
        nodeDB->updateFrom(mp); // last heard, signal
    if (textMessageModule)
        TextPeek::deliver(textMessageModule, mp); // the message history and Meshtastic's notice
    meshBadgeReceived(mp);                        // the badge's own alerts (lights, unread mark)
}

class McMesh : public BaseChatMesh
{
  public:
    McMesh(mesh::Radio &radio, mesh::RNG &rng, mesh::RTCClock &rtc, mesh::MeshTables &tables)
        : BaseChatMesh(radio, *new ArduinoMillis(), rng, rtc, *new StaticPoolPacketManager(16), tables)
    {
    }

    void start()
    {
        BaseChatMesh::begin();
        {
            Preferences p;
            if (p.begin("utah", true)) {
                autoAdd = p.getBool("mcauto", true);
                shareLoc = p.getBool("mcloc", false);
                p.end();
            }
        }
        loadIdentity();
        loadContacts();
        syncChannels();
        advertise(true, 1500);
    }

    // the badge's Meshtastic name is its MeshCore name
    static const char *myName() { return owner.long_name[0] ? owner.long_name : "UTAH badge"; }

    void advertise(bool flood, uint32_t delayMs = 0)
    {
        // with "share location" on, the place Meshtastic knows for this badge (a fixed position set in
        // its settings; the badge has no GPS) goes out in the announcement
        mesh::Packet *pkt = nullptr;
        if (shareLoc && (localPosition.latitude_i || localPosition.longitude_i))
            pkt = createSelfAdvert(myName(), localPosition.latitude_i * 1e-7, localPosition.longitude_i * 1e-7);
        else
            pkt = createSelfAdvert(myName());
        if (!pkt)
            return;
        if (flood)
            sendFlood(pkt, delayMs);
        else
            sendZeroHop(pkt, delayMs);
        advertAt = millis();
    }

    void upkeep()
    {
        if (pend.active && pend.retryDue)
            retryPending();
        if (Throttle::hasElapsed(advertAt, 15 * 60 * 1000))
            advertise(false); // keep nearby badges' lists fresh
        if (Throttle::hasElapsed(channelsAt, 10000))
            syncChannels(); // a group made or joined in the Messages app
        if (contactsDirty && Throttle::hasElapsed(contactsDirtyAt, 20000))
            saveContacts();
        if (mcNodesDirty && Throttle::hasElapsed(contactsDirtyAt, 20000))
            mcNodesSave();
    }

    bool sendText(uint32_t to, const char *text, uint8_t channel)
    {
        uint32_t now = getRTCClock()->getCurrentTimeUnique();
        if (to == NODENUM_BROADCAST) {
            syncChannels();
            ChannelDetails ch;
            if (channel >= MAX_GROUP_CHANNELS || !getChannel(channel, ch) || !channelUsed[channel])
                return false;
            return sendGroupMessage(now, ch.channel, myName(), text, (int)strlen(text));
        }
        ContactInfo *c = contactFor(to);
        if (!c) {
            announceSoon(); // they may not know us either: once they hear this they announce back
            return false;
        }
        uint32_t ack = 0, timeout = 0;
        if (sendMessage(*c, now, 0, text, ack, timeout) == MSG_SEND_FAILED)
            return false;
        // remember it until the other badge confirms it: no confirmation in time and it goes again
        pend.active = true;
        pend.ack = ack;
        pend.to = to;
        pend.timestamp = now;
        pend.attempt = 0;
        pend.retryDue = false;
        strlcpy(pend.text, text, sizeof(pend.text));
        strlcpy(pend.name, c->name, sizeof(pend.name));
        return true;
    }

    // ---- for the MeshCore settings menu ----
    bool autoAdd = true, shareLoc = false;
    void setFlag(const char *key, bool &flag, bool on)
    {
        flag = on;
        Preferences p;
        if (p.begin("utah", false)) {
            p.putBool(key, on);
            p.end();
        }
    }
    // the i-th contact (people, repeaters, rooms); nullptr past the end
    ContactInfo *contactAt(int i)
    {
        ContactInfo c;
        if (i < 0 || !getContactByIdx((uint32_t)i + MAX_ANON_CONTACTS, c))
            return nullptr;
        return lookupContactByPubKey(c.id.pub_key, PUB_KEY_SIZE);
    }
    bool describe(int i, McContact &out)
    {
        ContactInfo *c = contactAt(i);
        if (!c)
            return false;
        strlcpy(out.name, c->name, sizeof(out.name));
        out.type = c->type;
        out.favourite = c->isFav();
        out.hops = c->out_path_len == OUT_PATH_UNKNOWN ? -1 : (c->out_path_len & 63);
        out.node = nodeNumFor(c->id.pub_key);
        return true;
    }
    bool setFav(int i, bool on)
    {
        ContactInfo *c = contactAt(i);
        if (!c)
            return false;
        c->flags = on ? (c->flags | 0x01) : (c->flags & ~0x01);
        markContactsDirty();
        return true;
    }
    bool forgetRoute(int i)
    {
        ContactInfo *c = contactAt(i);
        if (!c)
            return false;
        resetPathTo(*c);
        markContactsDirty();
        return true;
    }
    bool remove(int i)
    {
        ContactInfo *c = contactAt(i);
        if (!c)
            return false;
        uint32_t num = nodeNumFor(c->id.pub_key);
        if (!removeContact(*c))
            return false;
        if (nodeDB)
            nodeDB->removeNodeByNum(num);
        mcNodeForget(num);
        saveContacts();
        mcNodesSave();
        return true;
    }
    // heard announcing but not a contact (auto-add is off, or the list was full)
    int heardCount() const { return nHeard; }
    const char *heardName(int i) const { return i >= 0 && i < nHeard ? heard[i].name : ""; }
    bool heardAdd(int i)
    {
        if (i < 0 || i >= nHeard || !addContact(heard[i]))
            return false;
        ensureNode(nodeNumFor(heard[i].id.pub_key), heard[i].name);
        for (int k = i; k + 1 < nHeard; k++)
            heard[k] = heard[k + 1];
        nHeard--;
        saveContacts();
        return true;
    }

  protected:
    void onDiscoveredContact(ContactInfo &c, bool isNew, uint8_t pathLen, const uint8_t *) override
    {
        printf("MC: advert from \"%s\"%s\n", c.name, isNew ? " (new)" : "");
        if (isNew)
            announceSoon(); // a stranger: let them know us too
        if (!lookupContactByPubKey(c.id.pub_key, PUB_KEY_SIZE)) {
            // not added (auto-add off, or the list is full): keep it to offer in the settings menu
            for (int k = 0; k < nHeard; k++)
                if (heard[k].id.matches(c.id))
                    return;
            if (nHeard < MAX_HEARD)
                heard[nHeard++] = c;
            return;
        }
        uint32_t num = nodeNumFor(c.id.pub_key);
        ensureNode(num, c.name);
        if (nodeDB) { // heard just now: the node list sorts and ages by this
            meshtastic_MeshPacket mp = meshtastic_MeshPacket_init_default;
            mp.from = num;
            mp.to = NODENUM_BROADCAST;
            mp.rx_time = getValidTime(RTCQualityFromNet);
            mp.rx_snr = _radio->getLastSNR();
            mp.rx_rssi = (int32_t)_radio->getLastRSSI();
            mp.hop_start = pathLen & 63;
            mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
            mp.decoded.portnum = meshtastic_PortNum_NODEINFO_APP;
            nodeDB->updateFrom(mp);
        }
        markContactsDirty();
    }
    bool isAutoAddEnabled() const override { return autoAdd; }
    // the other badge confirmed a message
    ContactInfo *processAck(const uint8_t *data) override
    {
        if (!pend.active || memcmp(data, &pend.ack, 4))
            return nullptr;
        pend.active = false;
        printf("MC: delivered to \"%s\"\n", pend.name);
        char b[64];
        snprintf(b, sizeof(b), "Delivered to\n%s", pend.name);
        if (screen)
            screen->showSimpleBanner(b, 2500);
        return contactFor(pend.to);
    }
    void onContactPathUpdated(const ContactInfo &) override { markContactsDirty(); }
    void onMessageRecv(const ContactInfo &c, mesh::Packet *pkt, uint32_t, const char *text) override
    {
        printf("MC: message from \"%s\"\n", c.name);
        uint32_t num = nodeNumFor(c.id.pub_key);
        ensureNode(num, c.name);
        deliverText(num, nodeDB ? nodeDB->getNodeNum() : 0, 0, text, pkt->getSNR(), _radio->getLastRSSI(),
                    pkt->isRouteFlood() ? pkt->getPathHashCount() : 0);
    }
    void onCommandDataRecv(const ContactInfo &, mesh::Packet *, uint32_t, const char *) override {}
    void onCLICommandRecv(const ContactInfo &, mesh::Packet *, uint32_t, const char *, char *reply) override { reply[0] = 0; }
    void onSignedMessageRecv(const ContactInfo &, mesh::Packet *, uint32_t, const uint8_t *, const char *) override {}
    uint32_t calcFloodTimeoutMillisFor(uint32_t airtime) const override { return 500 + (uint32_t)(16.0f * airtime); }
    uint32_t calcDirectTimeoutMillisFor(uint32_t airtime, uint8_t pathLen) const override
    {
        return 500 + (uint32_t)((airtime * 6.0f + 250) * (pathLen + 1));
    }
    // No confirmation in time. The resend can't happen here: MeshCore clears its timer right after
    // this returns, which would cancel the new attempt's timer too. So note it, and upkeep() resends.
    void onSendTimeout() override
    {
        if (pend.active)
            pend.retryDue = true;
    }
    // a group message is "Name: text"; the name is all MeshCore says about who sent it
    void onChannelMessageRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint32_t, const char *text) override
    {
        int idx = findChannelIdx(channel);
        if (idx < 0 || idx >= MAX_GROUP_CHANNELS || !channelUsed[idx])
            return;
        char name[32] = "?";
        const char *body = text, *sep = strstr(text, ": ");
        if (sep && sep > text && (size_t)(sep - text) < sizeof(name)) {
            memcpy(name, text, sep - text);
            name[sep - text] = 0;
            body = sep + 2;
        }
        printf("MC: channel %d message from \"%s\"\n", idx, name);
        uint32_t num = 0;
        ContactInfo c;
        for (uint32_t i = MAX_ANON_CONTACTS; getContactByIdx(i, c); i++)
            if (!strcmp(c.name, name)) {
                num = nodeNumFor(c.id.pub_key);
                break;
            }
        if (!num) {
            num = nodeNumForName(name);
            announceSoon(); // someone here we have no contact for: they announce back when they hear us
        }
        ensureNode(num, name);
        deliverText(num, NODENUM_BROADCAST, (uint8_t)idx, body, pkt->getSNR(), _radio->getLastRSSI(),
                    pkt->isRouteFlood() ? pkt->getPathHashCount() : 0);
    }
    uint8_t onContactRequest(const ContactInfo &, uint32_t, const uint8_t *, uint8_t, uint8_t *) override { return 0; }
    void onContactResponse(const ContactInfo &, const uint8_t *, uint8_t) override {}

  private:
    uint32_t advertAt = 0, channelsAt = 0, contactsDirtyAt = 0, lastIntro = 0;
    struct {
        bool active = false;
        uint32_t ack = 0, to = 0, timestamp = 0;
        uint8_t attempt = 0;
        bool retryDue = false;
        char text[201] = "", name[32] = "";
    } pend; // the direct message waiting for its confirmation
    static const int MAX_HEARD = 12;
    ContactInfo heard[MAX_HEARD];
    int nHeard = 0;
    bool contactsDirty = false;
    bool channelUsed[MAX_GROUP_CHANNELS] = {};
    uint32_t channelSig[MAX_GROUP_CHANNELS] = {};

    // Send the unconfirmed message again (the same message, marked as a further attempt, so the
    // other side doesn't show it twice); the last time by flooding, in case the known route has gone
    // stale. After three tries, say so.
    void retryPending()
    {
        pend.retryDue = false;
        ContactInfo *c = contactFor(pend.to);
        if (c && pend.attempt < 2) {
            pend.attempt++;
            if (pend.attempt == 2)
                resetPathTo(*c);
            uint32_t ack = 0, timeout = 0;
            if (sendMessage(*c, pend.timestamp, pend.attempt, pend.text, ack, timeout) != MSG_SEND_FAILED) {
                pend.ack = ack;
                printf("MC: no confirmation from \"%s\", try %d\n", pend.name, pend.attempt + 1);
                return;
            }
        }
        pend.active = false;
        printf("MC: gave up on \"%s\"\n", pend.name);
        char b[72];
        snprintf(b, sizeof(b), "No answer from\n%s\n(tried 3 times)", pend.name);
        if (screen)
            screen->showSimpleBanner(b, 4000);
    }

    void markContactsDirty()
    {
        if (!contactsDirty)
            contactsDirtyAt = millis();
        contactsDirty = true;
    }

    // (contacts are walked from MAX_ANON_CONTACTS: the slots before it are MeshCore's own, not people)
    ContactInfo *contactFor(uint32_t num)
    {
        ContactInfo c;
        for (uint32_t i = MAX_ANON_CONTACTS; getContactByIdx(i, c); i++)
            if (nodeNumFor(c.id.pub_key) == num)
                return lookupContactByPubKey(c.id.pub_key, PUB_KEY_SIZE);
        // a node known only by the name on its group messages: the contact of that name, if any by now
        const meshtastic_NodeInfoLite *nd = nodeDB ? nodeDB->getMeshNode(num) : nullptr;
        if (nd && nodeInfoLiteHasUser(nd))
            for (uint32_t i = MAX_ANON_CONTACTS; getContactByIdx(i, c); i++)
                if (!strcmp(c.name, nd->long_name))
                    return lookupContactByPubKey(c.id.pub_key, PUB_KEY_SIZE);
        return nullptr;
    }

    // Announce ourselves to badges in range, at most once a minute. Used when it turns out someone
    // nearby doesn't know us (or we them): a badge that hears an announcement from a stranger
    // announces back, so two badges that meet learn each other within moments.
    void announceSoon()
    {
        if (lastIntro && Throttle::isWithinTimespanMs(lastIntro, 60000))
            return;
        lastIntro = millis();
        advertise(false, 300 + (esp_random() % 1200)); // a random wait, so several badges don't collide
    }

    // ---- identity: made once, kept in the badge's files ----
    void loadIdentity()
    {
        bool have = false;
        {
            concurrency::LockGuard g(spiLock);
            File f = FSCom.open(ID_FILE, FILE_O_READ);
            if (f) {
                have = self_id.readFrom(f);
                f.close();
            }
        }
        if (have)
            return;
        self_id = mesh::LocalIdentity(getRNG());
        for (int i = 0; i < 10 && (self_id.pub_key[0] == 0x00 || self_id.pub_key[0] == 0xFF); i++)
            self_id = mesh::LocalIdentity(getRNG()); // 00 and FF are reserved
        concurrency::LockGuard g(spiLock);
        if (!FSCom.exists("/badge"))
            FSCom.mkdir("/badge");
        File f = FSCom.open(ID_FILE, FILE_O_WRITE);
        if (f) {
            self_id.writeTo(f);
            f.close();
        }
        printf("MC: new identity made\n");
    }

    // ---- contacts: public key, name and the route to each ----
    struct ContactRec {
        uint8_t pubKey[PUB_KEY_SIZE];
        char name[32];
        uint8_t type, flags, outPathLen;
        uint8_t outPath[MAX_PATH_SIZE];
        uint32_t lastAdvert, lastMod;
        int32_t lat, lon;
    };
    void loadContacts()
    {
        readContacts();
        // every contact is one of MeshCore's nodes (done after the file is closed: making a node
        // can write to storage itself)
        ContactInfo c;
        for (uint32_t i = MAX_ANON_CONTACTS; getContactByIdx(i, c); i++)
            if (c.type != ADV_TYPE_NONE)
                ensureNode(nodeNumFor(c.id.pub_key), c.name);
    }
    void readContacts()
    {
        concurrency::LockGuard g(spiLock);
        File f = FSCom.open(CONTACTS_FILE, FILE_O_READ);
        if (!f)
            return;
        ContactRec r;
        int n = 0;
        while (f.read((uint8_t *)&r, sizeof(r)) == (int)sizeof(r)) {
            if (r.type == ADV_TYPE_NONE)
                continue; // an empty slot saved by an earlier version
            ContactInfo c;
            memset(&c, 0, sizeof(c));
            c.id = mesh::Identity(r.pubKey);
            memcpy(c.name, r.name, sizeof(c.name));
            c.name[sizeof(c.name) - 1] = 0;
            c.type = r.type;
            c.flags = r.flags;
            c.out_path_len = r.outPathLen;
            memcpy(c.out_path, r.outPath, sizeof(c.out_path));
            c.last_advert_timestamp = r.lastAdvert;
            c.lastmod = r.lastMod;
            c.gps_lat = r.lat;
            c.gps_lon = r.lon;
            if (!addContact(c))
                break;
            n++;
        }
        f.close();
        printf("MC: %d contacts loaded\n", n);
    }
    void saveContacts()
    {
        contactsDirty = false;
        concurrency::LockGuard g(spiLock);
        File f = FSCom.open(CONTACTS_FILE, FILE_O_WRITE);
        if (!f)
            return;
        ContactInfo c;
        for (uint32_t i = MAX_ANON_CONTACTS; getContactByIdx(i, c); i++) {
            ContactRec r;
            memset(&r, 0, sizeof(r));
            memcpy(r.pubKey, c.id.pub_key, PUB_KEY_SIZE);
            memcpy(r.name, c.name, sizeof(r.name));
            r.type = c.type;
            r.flags = c.flags;
            r.outPathLen = c.out_path_len;
            memcpy(r.outPath, c.out_path, sizeof(r.outPath));
            r.lastAdvert = c.last_advert_timestamp;
            r.lastMod = c.lastmod;
            r.lat = c.gps_lat;
            r.lon = c.gps_lon;
            f.write((const uint8_t *)&r, sizeof(r));
        }
        f.close();
    }

    // ---- channels: Meshtastic's, with the same keys ----
    void syncChannels()
    {
        channelsAt = millis();
        for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
            const meshtastic_Channel &mc = ::channels.getByIndex((ChannelIndex)i);
            bool used = mc.role != meshtastic_Channel_Role_DISABLED;
            const auto &psk = mc.settings.psk;
            const char *name = ::channels.getName(i);
            uint32_t sig = used ? 2166136261u : 0;
            if (used) {
                for (pb_size_t k = 0; k < psk.size; k++)
                    sig = (sig ^ psk.bytes[k]) * 16777619u;
                for (const char *c = name; *c; c++)
                    sig = (sig ^ (uint8_t)*c) * 16777619u;
                sig |= 1;
            }
            if (sig == channelSig[i])
                continue;
            channelSig[i] = sig;
            channelUsed[i] = used;
            ChannelDetails d;
            memset(&d, 0, sizeof(d));
            if (used) {
                strlcpy(d.name, name, sizeof(d.name));
                if (psk.size == 16 || psk.size == 32) {
                    memcpy(d.channel.secret, psk.bytes, psk.size);
                    if (psk.size == 32 && !memcmp(d.channel.secret + 16, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16))
                        d.channel.secret[31] = 1; // MeshCore reads an all-zero second half as a 128-bit key
                } else if (psk.size == 1 && psk.bytes[0] == 1) {
                    memcpy(d.channel.secret, PUBLIC_KEY, 16); // Meshtastic's default channel = MeshCore's public one
                } else {
                    // no key / a short key index: a key made from the name and those bytes
                    uint8_t seed[48];
                    size_t n = strlcpy((char *)seed, name, 32);
                    if (n > 31)
                        n = 31;
                    for (pb_size_t k = 0; k < psk.size && n < sizeof(seed); k++)
                        seed[n++] = psk.bytes[k];
                    mesh::Utils::sha256(d.channel.secret, 16, seed, (int)n);
                }
            }
            setChannel(i, d);
        }
    }
};

// a phone connected: the pairing PIN goes on the badge's screen (shown from the MeshCore thread)
static volatile bool mcBleShowPin = false;

class McThread : public concurrency::OSThread
{
  public:
    McThread() : concurrency::OSThread("MeshCore") {}

    bool sendText(uint32_t to, const char *text, uint8_t channel)
    {
        return mesh_ && !paused && mesh_->sendText(to, text, channel);
    }

    // Something else (the 915 MHz scanner) borrows the radio; MeshCore leaves it alone meanwhile,
    // and puts its own settings back afterwards.
    void pause(bool on)
    {
        if (paused && !on && radio_)
            configure();
        paused = on;
    }

    McRadioCfg radioCfg() const { return cfg_; }
    // false: this radio refused that setting (it stays on what it had)
    bool setRadioCfg(const McRadioCfg &c)
    {
        if (!radio_)
            return false;
        McRadioCfg old = cfg_;
        cfg_ = c;
        if (!configure()) {
            cfg_ = old;
            configure();
            return false;
        }
        Preferences p;
        if (p.begin("utah", false)) {
            p.putUInt("mcfq", (uint32_t)(cfg_.freq * 1000 + 0.5f));
            p.putInt("mcbw", (int)(cfg_.bw * 10 + 0.5f));
            p.putUChar("mcsf", cfg_.sf);
            p.putUChar("mccr", cfg_.cr);
            p.putChar("mcpw", cfg_.power);
            p.end();
        }
        return true;
    }
    McMesh *mesh() { return paused ? nullptr : mesh_; }

  protected:
    int32_t runOnce() override
    {
        RadioLibInterface *r = RadioLibInterface::instance;
        if (!r)
            return 500; // Meshtastic hasn't brought the radio up yet
        if (!mesh_) {
            if (!takeOver(r))
                return disable();
            return 5;
        }
        if (mcBleShowPin) {
            mcBleShowPin = false;
            if (screen)
                screen->showSimpleBanner("MeshCore app pairing\nPIN: 123456", 20000);
        }
        if (paused)
            return 50;
        McPeek::keepQuiet(r);
        if (McPeek::receiving(r)) { // Meshtastic took the radio back (a settings change): take it again
            r->RadioInterface::disable();
            configure();
        }
        mesh_->loop();
        mesh_->upkeep();
        clock_.tick();
        if (Throttle::hasElapsed(lastNote, 10000)) {
            lastNote = millis();
            printf("MC: running, sent=%lu recv=%lu errors=%lu noise=%d contacts=%d\n", (unsigned long)radio_->getPacketsSent(),
                   (unsigned long)radio_->getPacketsRecv(), (unsigned long)radio_->getPacketsRecvErrors(),
                   radio_->getNoiseFloor(), mesh_->getNumContacts());
        }
        return 5;
    }

  private:
    // MeshCore's settings on the chip, and its receive interrupt
    bool configure()
    {
        sx_->standby();
        sx_->setSyncWord(RADIOLIB_SX126X_SYNC_WORD_PRIVATE);
        sx_->setCRC(1);
        // every step is checked: the stock LLCC68 refuses bandwidths under 125 kHz and the higher
        // spreading factors, and a setting the chip did not take must not be reported as in use
        bool ok = sx_->setFrequency(cfg_.freq) == RADIOLIB_ERR_NONE && sx_->setBandwidth(cfg_.bw) == RADIOLIB_ERR_NONE &&
                  sx_->setSpreadingFactor(cfg_.sf) == RADIOLIB_ERR_NONE && sx_->setCodingRate(cfg_.cr) == RADIOLIB_ERR_NONE &&
                  sx_->setOutputPower(cfg_.power) == RADIOLIB_ERR_NONE;
        radio_->begin();
        if (ok)
            radio_->setParams(cfg_.freq, cfg_.bw, cfg_.sf, cfg_.cr);
        return ok;
    }

    bool takeOver(RadioLibInterface *r)
    {
        sx_ = static_cast<SX1262 *>(McPeek::radio(r)); // an LLCC68 is an SX1262 for these calls
        if (!sx_)
            return false;
        for (int i = 0; i < 150 && !r->canSleep(false); i++) // let a Meshtastic packet in the air finish
            delay(20);
        r->RadioInterface::disable(); // Meshtastic now drops what it would send, and stops listening
        McPeek::keepQuiet(r);

        // The radio setting, NVS "utah" / "mcfq" (kHz), "mcbw" (kHz x10), "mcsf", "mccr", "mcpw". Unless
        // set otherwise: 910.525 MHz, 125 kHz, SF7 - every badge can do it, so badges hear each other.
        // MeshCore's public US mesh uses 62.5 kHz, which only an SX1262-modded badge can set; a
        // setting this badge's radio refuses is replaced by that default.
        const McRadioCfg fallback;
        cfg_ = fallback;
        {
            Preferences p;
            if (p.begin("utah", true)) {
                cfg_.freq = p.getUInt("mcfq", 910525) / 1000.0f;
                cfg_.bw = p.getInt("mcbw", 1250) / 10.0f;
                cfg_.sf = p.getUChar("mcsf", fallback.sf);
                cfg_.cr = p.getUChar("mccr", fallback.cr);
                cfg_.power = p.getChar("mcpw", fallback.power);
                p.end();
            }
        }
        radio_ = new McRadio(*sx_, board_);
        if (!configure()) {
            cfg_ = fallback;
            configure();
        }
        radio_->resetStats(); // MeshCore's wrapper leaves its error counter uninitialised

        clock_.begin();
        mesh_ = new McMesh(*radio_, rng_, clock_, tables_);
        mesh_->start();
        printf("MC: MeshCore owns the radio: %.3f MHz bw=%.1f sf=%d name=\"%s\"\n", (double)cfg_.freq, (double)cfg_.bw, cfg_.sf,
               McMesh::myName());
        return true;
    }

    McBoard board_;
    McClock clock_;
    McRng rng_;
    SimpleMeshTables tables_;
    SX1262 *sx_ = nullptr;
    McRadio *radio_ = nullptr;
    McMesh *mesh_ = nullptr;
    McRadioCfg cfg_;
    bool paused = false;
    uint32_t lastNote = 0;
};
static McThread *mcThread = nullptr;

// In MeshCore mode the badge's lists show MeshCore's nodes only.
bool meshcoreHidesNode(uint32_t num)
{
    return modeOn && mcNodeIndex(num) < 0;
}
bool meshcoreNameOnly(uint32_t num)
{
    const meshtastic_NodeInfoLite *nd = modeOn && nodeDB ? nodeDB->getMeshNode(num) : nullptr;
    return nd && nodeInfoLiteHasUser(nd) && nodeNumForName(nd->long_name) == num;
}
bool meshcoreSendText(uint32_t to, const char *text, uint8_t channel)
{
    return mcThread && mcThread->sendText(to, text, channel);
}
void meshcorePause(bool on)
{
    if (mcThread)
        mcThread->pause(on);
}
McRadioCfg meshcoreRadio()
{
    return mcThread ? mcThread->radioCfg() : McRadioCfg();
}
bool meshcoreSetRadio(const McRadioCfg &c)
{
    return mcThread && mcThread->setRadioCfg(c);
}
static McMesh *theMesh()
{
    return mcThread ? mcThread->mesh() : nullptr;
}
void meshcoreAdvertise(bool flood)
{
    if (McMesh *m = theMesh())
        m->advertise(flood);
}
int meshcoreContactCount()
{
    McMesh *m = theMesh();
    return m ? m->getNumContacts() : 0;
}
bool meshcoreContact(int i, McContact &out)
{
    McMesh *m = theMesh();
    return m && m->describe(i, out);
}
bool meshcoreContactFav(int i, bool on)
{
    McMesh *m = theMesh();
    return m && m->setFav(i, on);
}
bool meshcoreContactResetPath(int i)
{
    McMesh *m = theMesh();
    return m && m->forgetRoute(i);
}
bool meshcoreContactDelete(int i)
{
    McMesh *m = theMesh();
    return m && m->remove(i);
}
int meshcoreHeardCount()
{
    McMesh *m = theMesh();
    return m ? m->heardCount() : 0;
}
const char *meshcoreHeardName(int i)
{
    McMesh *m = theMesh();
    return m ? m->heardName(i) : "";
}
bool meshcoreHeardAdd(int i)
{
    McMesh *m = theMesh();
    return m && m->heardAdd(i);
}
bool meshcoreAutoAdd()
{
    McMesh *m = theMesh();
    return m ? m->autoAdd : true;
}
void meshcoreSetAutoAdd(bool on)
{
    if (McMesh *m = theMesh())
        m->setFlag("mcauto", m->autoAdd, on);
}
bool meshcoreShareLocation()
{
    McMesh *m = theMesh();
    return m && m->shareLoc;
}
void meshcoreSetShareLocation(bool on)
{
    if (McMesh *m = theMesh())
        m->setFlag("mcloc", m->shareLoc, on);
}
// A MeshCore hashtag channel's key: the first 16 bytes of SHA-256 of "#name" (MeshCore's rule, so
// anyone who knows the name can join; "#test" gives 9cd8fcf22a47333b591d96a2b848b73f).
void meshcoreHashtagKey(const char *name, uint8_t *key16)
{
    char tag[40];
    snprintf(tag, sizeof(tag), "#%s", name[0] == '#' ? name + 1 : name);
    mesh::Utils::sha256(key16, 16, (const uint8_t *)tag, (int)strlen(tag));
}

static void bluetoothAtStart(bool meshcore);
void meshcoreStart()
{
    {
        Preferences p;
        if (p.begin("utah", true)) {
            savedMode = p.getInt("meshmode", 0) == 1;
            p.end();
        }
    }
    bluetoothAtStart(savedMode);
}

// Which phone app the badge's Bluetooth serves depends on the mesh, and the Bluetooth library can be
// started only once a run (shut down and started again, it crashes). So Bluetooth waits for the
// mesh question: at startup Meshtastic is told its Bluetooth is off - in memory only - and once the
// mesh is chosen the right service is started: Meshtastic's own, or MeshCore's.
// NVS "utah" / "mcbt" holds the real setting while the in-memory one is "off", and is put back at
// every start, so a settings save in the meantime can't turn Bluetooth off for good.
static bool btWanted = false; // Bluetooth as set in Mesh settings
static void bluetoothAtStart(bool)
{
    Preferences p;
    if (!p.begin("utah", false))
        return;
    int was = p.getInt("mcbt", -1);
    if (was >= 0)
        config.bluetooth.enabled = was == 1; // the real setting, whatever a save wrote meanwhile
    btWanted = config.bluetooth.enabled;
    p.putInt("mcbt", btWanted ? 1 : 0);
    p.end();
    config.bluetooth.enabled = false; // until the mesh is chosen
}
// To a phone the badge is two different Bluetooth devices, one per mesh, so each phone app keeps its
// own pairing and neither has to be "forgotten" to use the other:
//   * MeshCore mode uses its own Bluetooth address (the badge's, with bits changed; see mcBleSpike)
//   * each mode has its own pairing records. The Bluetooth stack keeps them in NVS namespace
//     "nimble_bond"; the other mode's are kept aside ("bond_mt" / "bond_mc") and swapped in before
//     Bluetooth starts. NVS "utah" / "bondset" says whose are in place: 0 Meshtastic's, 1 MeshCore's.
static void bondsCopy(const char *from, const char *to)
{
    nvs_handle_t src, dst;
    if (nvs_open(to, NVS_READWRITE, &dst) != ESP_OK)
        return;
    nvs_erase_all(dst);
    if (nvs_open(from, NVS_READONLY, &src) == ESP_OK) {
        nvs_iterator_t it = nullptr;
        static uint8_t blob[512];
        for (esp_err_t e = nvs_entry_find("nvs", from, NVS_TYPE_BLOB, &it); e == ESP_OK && it; e = nvs_entry_next(&it)) {
            nvs_entry_info_t info;
            nvs_entry_info(it, &info);
            size_t n = sizeof(blob);
            if (nvs_get_blob(src, info.key, blob, &n) == ESP_OK)
                nvs_set_blob(dst, info.key, blob, n);
        }
        nvs_release_iterator(it);
        nvs_close(src);
    }
    nvs_commit(dst);
    nvs_close(dst);
}
static void bondsFor(bool meshcore)
{
    Preferences p;
    if (!p.begin("utah", false))
        return;
    bool holds = p.getInt("bondset", 0) == 1;
    if (holds != meshcore) {
        bondsCopy("nimble_bond", holds ? "bond_mc" : "bond_mt");    // put away the ones in place
        bondsCopy(meshcore ? "bond_mc" : "bond_mt", "nimble_bond"); // (an empty set the first time)
        p.putInt("bondset", meshcore ? 1 : 0);
    }
    p.end();
}

// The mesh is chosen: bring up the Bluetooth that goes with it.
static void mcBleSpike();
static void bluetoothFor(bool meshcore)
{
    bondsFor(meshcore); // before the Bluetooth stack reads them
    if (meshcore) {
        if (btWanted)
            mcBleSpike(); // (Meshtastic's stays off this run; "mcbt" keeps the real setting)
        return;
    }
    config.bluetooth.enabled = btWanted;
    Preferences p;
    if (p.begin("utah", false)) {
        p.remove("mcbt");
        p.end();
    }
    if (nodeDB)
        nodeDB->saveToDisk(SEGMENT_CONFIG); // in case a save during a MeshCore run wrote "off"
    if (btWanted)
        setBluetoothEnable(true); // Meshtastic's own start, which was skipped while it was "off"
}

// FEASIBILITY TEST (not the real thing): in MeshCore mode put up the Bluetooth service the MeshCore
// phone app looks for (a Nordic UART service, named "MeshCore-...", PIN 123456). Nothing answers
// the app yet - this only shows whether the app finds the badge and can pair with it.
class McBleEvents : public BLEServerCallbacks
{
    void onConnect(BLEServer *) override { mcBleShowPin = true; }
    void onDisconnect(BLEServer *) override { BLEDevice::startAdvertising(); } // findable again
};
static void mcBleSpike()
{
    static const char *NUS = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
    if (BLEDevice::getInitialized())
        return; // Meshtastic's Bluetooth is up: the library can't be taken down and restarted (it crashes)
    static char name[48];
    snprintf(name, sizeof(name), "MeshCore-%s", McMesh::myName());
    BLEDevice::init(name);
    {
        // MeshCore's own address: the badge's, as a "static random" address (top two bits set) with
        // one more bit changed, so a phone sees a different device from the Meshtastic one
        uint8_t addr[6] = {0};
        esp_read_mac(addr, ESP_MAC_BT);
        uint8_t le[6] = {(uint8_t)(addr[5] ^ 0x01), addr[4], addr[3], addr[2], addr[1], (uint8_t)(addr[0] | 0xC0)};
        bool ok = BLEDevice::setOwnAddr(le) && BLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
        printf("MC: BLE address %02x:%02x:%02x:%02x:%02x:%02x set=%d\n", le[5], le[4], le[3], le[2], le[1], le[0], (int)ok);
    }
    BLESecurity security;
    security.setCapability(ESP_IO_CAP_OUT);
    security.setPassKey(true, 123456);
    security.setAuthenticationMode(true, true, true);
    BLEServer *srv = BLEDevice::createServer();
    if (srv)
        srv->setCallbacks(new McBleEvents());
    BLEService *svc = srv ? srv->createService(NUS) : nullptr;
    if (svc) {
        svc->createCharacteristic("6E400003-B5A3-F393-E0A9-E50E24DCCA9E",
                                  BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
        svc->createCharacteristic("6E400002-B5A3-F393-E0A9-E50E24DCCA9E", BLECharacteristic::PROPERTY_WRITE);
        svc->start();
        BLEAdvertising *adv = BLEDevice::getAdvertising();
        adv->addServiceUUID(NUS);
        BLEAdvertisementData scan;
        scan.setName(name);
        adv->setScanResponseData(scan);
        adv->start();
    }
    printf("MC: BLE test: library up=%d server=%d service=%d name=\"%s\"\n", (int)BLEDevice::getInitialized(), srv != nullptr,
           svc != nullptr, name);
}

static const char *restartingInto = nullptr; // set when the badge is about to restart into the other mesh
static uint32_t restartAt = 0;

// The mesh for this run is settled: remember it, give it its own channels, nodes and message
// history, and hand MeshCore the radio if it was picked.
static void chooseMesh(bool meshcore)
{
    if (modeOn)
        return; // MeshCore already has the radio (the choice is made once a run)
    if (meshcore != savedMode)
        meshcoreSetMode(meshcore);
    channelsFor(meshcore);
    mcNodesFor(meshcore);
    if (meshcore) {
        // MeshCore's chats have their own history file, loaded in place of the one Meshtastic
        // loaded at startup (which stays on disk untouched)
        messageStore = MessageStore("meshcore");
        messageStore.loadFromFlash();
        mcThread = new McThread();
    } else if (RadioLibInterface::instance) {
        RadioLibInterface::instance->reconfigure(); // Meshtastic started on whichever channels were in place
    }
    modeOn = meshcore;
    bluetoothFor(meshcore);
}

// ---- the startup question --------------------------------------------------------------------
//   joystick up / down (or 2 / 8) move, # or 5 choose; the highlighted one is taken after 8 s
class MeshBootApp : public App
{
  public:
    const char *name() const override { return "MeshBoot"; }
    uint32_t frameMs() const override { return 250; }
    bool keepAwake() const override { return true; }
    bool picked = false; // chosen with a key, not by the countdown
    void (*then)() = nullptr;
    void begin() override
    {
        picked = false;
        sel = savedMode ? 1 : 0;
        shownAt = millis();
    }
    void tick(uint32_t now) override
    {
        (void)now;
        if (restartingInto) {
            // Restart straight away, not through Meshtastic's shutdown: that switches the display
            // off first, and the point is for "Loading ..." to stay on the panel across the restart.
            // (Nothing is lost: the choice is already saved, and this is seconds after startup.)
            if (restartAt && (int32_t)(millis() - restartAt) >= 0)
                ESP.restart();
            return;
        }
        if (Throttle::hasElapsed(shownAt, WAIT_MS))
            done();
    }
    void key(int k, KeyAction a) override
    {
        if (restartingInto)
            return;
        // Choosing happens when the key is let go, not when it goes down: the home menu opens the
        // moment the mesh is chosen, and a key still held would carry on into it (holding # there
        // opens the settings menu).
        if ((k == AK_HASH || k == AK_5) && a == KA_RELEASE) {
            picked = true;
            done();
            return;
        }
        if (a != KA_PRESS)
            return;
        if (k == AK_UP || k == AK_2 || k == AK_DOWN || k == AK_8 || k == AK_LEFT || k == AK_RIGHT || k == AK_4 || k == AK_6) {
            sel = !sel;
            shownAt = millis(); // a key press restarts the countdown
            appRedraw();
        }
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        int lh = fontHeight(FONT_SMALL), W = d->width(), H = d->height();
        d->setColor(BLACK);
        d->fillRect(x, y, W, H);
        d->setColor(WHITE);
        if (restartingInto) { // the other mesh than last time: this screen stays up until the badge restarts
            drawTitle(d, x, y, "LoRa mesh", nullptr);
            drawCentered(d, x, y + H / 2 - lh - 4, "Loading", FONT_SMALL);
            drawCentered(d, x, y + H / 2 - 2, restartingInto, FONT_MEDIUM);
            return;
        }
        drawTitle(d, x, y, "LoRa mesh", "#choose");
        static const char *const NAMES[] = {"Meshtastic", "MeshCore"};
        int rowH = fontHeight(FONT_MEDIUM) + 6, top = y + lh + 10;
        for (int i = 0; i < 2; i++) {
            int ry = top + i * (rowH + 4);
            d->setColor(WHITE);
            if (i == sel) {
                d->fillRect(x + 8, ry, W - 16, rowH);
                d->setColor(BLACK);
            } else {
                d->drawRect(x + 8, ry, W - 16, rowH);
            }
            drawCentered(d, x, ry + 3, NAMES[i], FONT_MEDIUM);
            d->setColor(WHITE);
        }
        uint32_t left = Throttle::remainingMs(shownAt, WAIT_MS);
        char b[40];
        snprintf(b, sizeof(b), "starts %s in %u s", NAMES[sel], (unsigned)((left + 999) / 1000));
        drawCentered(d, x, y + H - lh - 1, b, FONT_SMALL);
    }

  private:
    static const uint32_t WAIT_MS = 8000;
    int sel = 0;
    uint32_t shownAt = 0;
    void done()
    {
        chooseMesh(sel == 1);
        if (restartingInto) {
            appRedraw(); // stay here, saying so, until the restart (no glimpse of the wrong mesh's screens)
            return;
        }
        appClose();
        if (then)
            then(); // the next screen, straight away
    }
};
static MeshBootApp meshBootApp;

bool meshBootPicked()
{
    return meshBootApp.picked;
}

const char *meshcoreLoadingInto()
{
    const char *into = nullptr;
    Preferences p;
    if (p.begin("utah", true)) {
        if (p.getBool("meshgo", false))
            into = p.getInt("meshmode", 0) == 1 ? "MeshCore" : "Meshtastic";
        p.end();
    }
    return into;
}

// The badge restarted because the other mesh was picked at the question: go into it without asking
// again (true), exactly as if it had just been picked with a key.
bool meshcoreResumeChoice()
{
    bool go = false;
    Preferences p;
    if (p.begin("utah", false)) {
        go = p.getBool("meshgo", false);
        if (go)
            p.remove("meshgo");
        p.end();
    }
    if (!go)
        return false;
    chooseMesh(savedMode);
    meshBootApp.picked = true;
    return true;
}

void openMeshBootChoice(void (*then)())
{
    meshBootApp.then = then;
    appOpen(&meshBootApp);
}

} // namespace badge

#endif // BADGE_MESHCORE

#include "configuration.h"

#include "BadgeMeshCore.h"

#include <Preferences.h>

#if !BADGE_MESHCORE

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
float meshcoreBandwidth()
{
    return 0;
}
bool meshcoreSetBandwidth(float)
{
    return false;
}
void openMeshBootChoice(void (*then)())
{
    if (then)
        then();
}
bool meshBootPicked()
{
    return false;
}
} // namespace badge

#else

#include "BadgeApp.h"
#include "FSCommon.h"
#include "MessageStore.h"
#include "NodeDB.h"
#include "SPILock.h"
#include "concurrency/OSThread.h"
#include "gps/RTC.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "mesh/Channels.h"
#include "mesh/MeshModule.h"
#include "mesh/RadioLibInterface.h"
#include "mesh/Throttle.h"

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

static void ensureNode(uint32_t num, const char *name)
{
    if (!nodeDB || !name || !name[0])
        return;
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

// Hand Meshtastic's modules a text packet, as if its radio had received it.
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
    MeshModule::callModules(mp, RX_SRC_RADIO);
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
        loadIdentity();
        loadContacts();
        syncChannels();
        advertise(true, 1500);
    }

    // the badge's Meshtastic name is its MeshCore name
    static const char *myName() { return owner.long_name[0] ? owner.long_name : "UTAH badge"; }

    void advertise(bool flood, uint32_t delayMs = 0)
    {
        mesh::Packet *pkt = createSelfAdvert(myName());
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
        if (Throttle::hasElapsed(advertAt, 15 * 60 * 1000))
            advertise(false); // keep nearby badges' lists fresh
        if (Throttle::hasElapsed(channelsAt, 10000))
            syncChannels(); // a group made or joined in the Messages app
        if (contactsDirty && Throttle::hasElapsed(contactsDirtyAt, 20000))
            saveContacts();
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
        return sendMessage(*c, now, 0, text, ack, timeout) != MSG_SEND_FAILED;
    }

  protected:
    void onDiscoveredContact(ContactInfo &c, bool isNew, uint8_t pathLen, const uint8_t *) override
    {
        printf("MC: advert from \"%s\"%s\n", c.name, isNew ? " (new)" : "");
        if (isNew)
            announceSoon(); // a stranger: let them know us too
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
    ContactInfo *processAck(const uint8_t *) override { return nullptr; }
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
    void onSendTimeout() override {}
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
    bool contactsDirty = false;
    bool channelUsed[MAX_GROUP_CHANNELS] = {};
    uint32_t channelSig[MAX_GROUP_CHANNELS] = {};

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

    float bandwidth() const { return bw_; }
    // false: this radio can't do that bandwidth (it stays on what it had)
    bool setBandwidth(float bw)
    {
        if (!radio_)
            return false;
        float old = bw_;
        bw_ = bw;
        if (!configure()) {
            bw_ = old;
            configure();
            return false;
        }
        Preferences p;
        if (p.begin("utah", false)) {
            p.putInt("mcbw", (int)(bw * 10));
            p.end();
        }
        return true;
    }

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
        sx_->setOutputPower(LORA_TX_POWER);
        bool ok = sx_->setBandwidth(bw_) == RADIOLIB_ERR_NONE;
        radio_->begin();
        if (ok)
            radio_->setParams(LORA_FREQ, bw_, LORA_SF, 5);
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

        // Bandwidth in kHz, NVS "utah" / "mcbw" (x10). 125 unless set otherwise: every badge can do
        // it, so badges hear each other. MeshCore's public US mesh uses 62.5, which only an
        // SX1262-modded badge can set; the stock LLCC68 refuses it and is put on 125.
        bw_ = 125.0;
        {
            Preferences p;
            if (p.begin("utah", true)) {
                bw_ = p.getInt("mcbw", 1250) / 10.0f;
                p.end();
            }
        }
        radio_ = new McRadio(*sx_, board_);
        if (!configure()) {
            bw_ = 125.0;
            configure();
        }
        radio_->resetStats(); // MeshCore's wrapper leaves its error counter uninitialised

        clock_.begin();
        mesh_ = new McMesh(*radio_, rng_, clock_, tables_);
        mesh_->start();
        printf("MC: MeshCore owns the radio: %.3f MHz bw=%.1f sf=%d name=\"%s\"\n", (double)LORA_FREQ, (double)bw_, LORA_SF,
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
    float bw_ = 125.0;
    bool paused = false;
    uint32_t lastNote = 0;
};
static McThread *mcThread = nullptr;

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
float meshcoreBandwidth()
{
    return mcThread ? mcThread->bandwidth() : 0;
}
bool meshcoreSetBandwidth(float khz)
{
    return mcThread && mcThread->setBandwidth(khz);
}

void meshcoreStart()
{
    Preferences p;
    if (p.begin("utah", true)) {
        savedMode = p.getInt("meshmode", 0) == 1;
        p.end();
    }
}

// The mesh for this run is settled: remember it, and hand MeshCore the radio if it was picked.
static void chooseMesh(bool meshcore)
{
    if (meshcore != savedMode)
        meshcoreSetMode(meshcore);
    if (meshcore && !modeOn) {
        // MeshCore's chats are kept apart from Meshtastic's: their own history file, loaded in
        // place of the one Meshtastic loaded at startup (which stays on disk untouched)
        messageStore = MessageStore("meshcore");
        messageStore.loadFromFlash();
        mcThread = new McThread();
    }
    modeOn = meshcore;
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
        if (Throttle::hasElapsed(shownAt, WAIT_MS))
            done();
    }
    void key(int k, KeyAction a) override
    {
        if (a != KA_PRESS)
            return;
        if (k == AK_UP || k == AK_2 || k == AK_DOWN || k == AK_8 || k == AK_LEFT || k == AK_RIGHT || k == AK_4 || k == AK_6) {
            sel = !sel;
            shownAt = millis(); // a key press restarts the countdown
            appRedraw();
        } else if (k == AK_HASH || k == AK_5) {
            picked = true;
            done();
        }
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        int lh = fontHeight(FONT_SMALL), W = d->width(), H = d->height();
        d->setColor(BLACK);
        d->fillRect(x, y, W, H);
        d->setColor(WHITE);
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

void openMeshBootChoice(void (*then)())
{
    meshBootApp.then = then;
    appOpen(&meshBootApp);
}

} // namespace badge

#endif // BADGE_MESHCORE

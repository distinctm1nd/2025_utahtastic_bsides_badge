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
#include "concurrency/OSThread.h"
#include "gps/RTC.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
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

class McClock : public mesh::RTCClock
{
  public:
    uint32_t getCurrentTime() override { return getTime(); } // Meshtastic's clock (phone / NTP / portal)
    void setCurrentTime(uint32_t) override {}
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

// The chat layer. PROOF OF CONCEPT: logs what it hears; the badge apps are not wired to it yet.
class McMesh : public BaseChatMesh
{
  public:
    McMesh(mesh::Radio &radio, mesh::RNG &rng, mesh::RTCClock &rtc, mesh::MeshTables &tables)
        : BaseChatMesh(radio, *new ArduinoMillis(), rng, rtc, *new StaticPoolPacketManager(16), tables)
    {
    }
    void start(const char *name)
    {
        BaseChatMesh::begin();
        self_id = mesh::LocalIdentity(getRNG()); // not saved yet: a new identity every start
        for (int i = 0; i < 10 && (self_id.pub_key[0] == 0x00 || self_id.pub_key[0] == 0xFF); i++)
            self_id = mesh::LocalIdentity(getRNG());
        if (mesh::Packet *pkt = createSelfAdvert(name))
            sendFlood(pkt, 1500);
    }

  protected:
    void onDiscoveredContact(ContactInfo &c, bool isNew, uint8_t, const uint8_t *) override
    {
        printf("MC: advert from \"%s\"%s\n", c.name, isNew ? " (new)" : "");
    }
    ContactInfo *processAck(const uint8_t *) override { return nullptr; }
    void onContactPathUpdated(const ContactInfo &) override {}
    void onMessageRecv(const ContactInfo &c, mesh::Packet *, uint32_t, const char *text) override
    {
        printf("MC: message from \"%s\": %s\n", c.name, text);
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
    void onChannelMessageRecv(const mesh::GroupChannel &, mesh::Packet *, uint32_t, const char *text) override
    {
        printf("MC: channel message: %s\n", text);
    }
    uint8_t onContactRequest(const ContactInfo &, uint32_t, const uint8_t *, uint8_t, uint8_t *) override { return 0; }
    void onContactResponse(const ContactInfo &, const uint8_t *, uint8_t) override {}
};

class McThread : public concurrency::OSThread
{
  public:
    McThread() : concurrency::OSThread("MeshCore") {}

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
        McPeek::keepQuiet(r);
        mesh_->loop();
        if (Throttle::hasElapsed(lastNote, 10000)) {
            lastNote = millis();
            printf("MC: running, sent=%lu recv=%lu errors=%lu noise=%d\n", (unsigned long)radio_->getPacketsSent(),
                   (unsigned long)radio_->getPacketsRecv(), (unsigned long)radio_->getPacketsRecvErrors(),
                   radio_->getNoiseFloor());
        }
        return 5;
    }

  private:
    bool takeOver(RadioLibInterface *r)
    {
        SX1262 *sx = static_cast<SX1262 *>(McPeek::radio(r)); // an LLCC68 is an SX1262 for these calls
        if (!sx)
            return false;
        for (int i = 0; i < 150 && !r->canSleep(false); i++) // let a Meshtastic packet in the air finish
            delay(20);
        r->RadioInterface::disable(); // Meshtastic now drops what it would send, and stops listening
        McPeek::keepQuiet(r);
        sx->standby();

        // Bandwidth in kHz, NVS "utah" / "mcbw" (x10). Default 125: every badge can do it, so badges
        // hear each other. MeshCore's public US mesh uses 62.5, which only an SX1262-modded badge can
        // set (the stock LLCC68 refuses it and is put back on 125 below). No settings screen yet.
        float bw = 125.0;
        {
            Preferences p;
            if (p.begin("utah", true)) {
                bw = p.getInt("mcbw", 1250) / 10.0f;
                p.end();
            }
        }
        radio_ = new McRadio(*sx, board_);
        sx->setSyncWord(RADIOLIB_SX126X_SYNC_WORD_PRIVATE);
        sx->setCRC(1);
        sx->setOutputPower(LORA_TX_POWER);
        if (sx->setBandwidth(bw) != RADIOLIB_ERR_NONE)
            bw = 125.0;
        radio_->begin();
        radio_->resetStats(); // MeshCore's wrapper leaves its error counter uninitialised
        radio_->setParams(LORA_FREQ, bw, LORA_SF, 5);

        rng_.begin(esp_random());
        mesh_ = new McMesh(*radio_, rng_, clock_, tables_);
        mesh_->start("UTAH badge");
        printf("MC: MeshCore owns the radio: %.3f MHz bw=%.1f sf=%d\n", (double)LORA_FREQ, (double)bw, LORA_SF);
        return true;
    }

    McBoard board_;
    McClock clock_;
    StdRNG rng_;
    SimpleMeshTables tables_;
    McRadio *radio_ = nullptr;
    McMesh *mesh_ = nullptr;
    uint32_t lastNote = 0;
};

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
    if (meshcore && !modeOn)
        new McThread();
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

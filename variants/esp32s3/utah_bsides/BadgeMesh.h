// The badge's mesh features, as one Meshtastic module that sees every received packet:
//  * LED alerts for messages, a moon/logo blink for mesh traffic
//  * meeting new badges (badge collection), the "far away" achievement
//  * remote "!" commands in direct messages (!lights, !bright, !speed, !help)
//  * synced light shows, tic-tac-toe moves and scavenger-hunt beacons on the private
//    app port (256), and the fox hunt signal readings
#pragma once
#include <stddef.h>
#include <stdint.h>

class OLEDDisplay;

namespace badge
{

void meshStart(); // from lateInitVariant()

uint32_t meshMyNode();
void badgePageDraw(OLEDDisplay *d, int16_t x, int16_t y); // BadgePage.cpp
// the last packet heard from another node (its signal at the last hop); false if none yet
bool meshLastHeard(uint32_t &node, int &rssi, float &snr, int &hops, uint32_t &ageMs);

// every node heard over the air this session (decoded or not); for the LoRa node list, so it can
// show nodes on other channels that share our frequency. decoded=false means we lacked its key.
struct HeardNode {
    uint32_t num;
    uint32_t lastMs; // millis() last heard
    int rssi, hops;  // hops -1 if unknown
    float snr;
    uint16_t chHash; // channel-hash byte from the clear header (undecoded packets)
    uint8_t preset;  // the modem preset the badge was listening on when heard
    bool decoded;    // we have decrypted at least one packet from it (so it's in NodeDB with a name)
};
int meshHeardList(HeardNode *out, int max);

// Live modem-preset switching for the LoRa node list, so you can sweep LongFast / LongSlow / ...
// The radio hears one preset at a time; this retunes it (no reboot) to listen on another, and
// restores the badge's own preset afterwards. meshListenPreset does not save to disk.
void meshListenPreset(int preset);   // a meshtastic_Config_LoRaConfig_ModemPreset value
int meshCurrentPreset();
void meshRestorePreset();            // put the badge back on its configured preset
int meshPresetCount();               // the presets worth sweeping
int meshPresetAt(int i);             // the preset value at list position i
const char *meshPresetName(int preset);
void meshRequestNodeInfo(uint32_t node, uint8_t channel);
bool meshSendInvite(uint32_t to, uint8_t channelIndex); // a channel's name + key, end to end encrypted
bool meshInvitePending();                               // an invite arrived and hasn't been shown
bool meshTakeInvite(uint32_t &from, char *name, size_t nameSize, uint8_t *psk, int &len); // ask a node for its node info (name, key)
const char *meshNodeName(uint32_t node, char *buf, size_t n); // long name, short name or !hex
// hopLimit 0: only badges in direct range hear it (not relayed); -1: the usual hop limit
bool meshSendPrivate(uint32_t to, const uint8_t *data, size_t len, bool wantAck, int hopLimit = -1);
bool meshSendTextDM(uint32_t to, const char *text, uint8_t channel = 0);

// settings (NVS)
bool meshRemoteCommands();
void meshSetRemoteCommands(bool on);
bool meshJoinShows();
void meshSetJoinShows(bool on);

// light shows: share the current scene once, or keep re-sending it every minute
void meshShareLightShow();
bool meshLeadingShow();
void meshSetLeadingShow(bool on);

// fox hunt
struct FoxReading {
    bool valid;
    int32_t rssi;   // 0 if unknown (only packets this module saw carry RSSI)
    float snr;
    uint32_t atMs;  // millis() of the last reading
    int hops;       // -1 unknown, 0 = heard directly
};
void meshFoxStart(uint32_t target); // also pings the target every 20 s
void meshFoxStop();
FoxReading meshFoxReading();
void meshFoxPing();

// tic-tac-toe packets ("BTT" + type, game id, cell) are passed here
typedef void (*TttHandler)(uint32_t from, const uint8_t *payload, size_t len);
void meshSetTttHandler(TttHandler h);

// fox hunt sightings other badges share ("BSG" packets) are passed here
typedef void (*SightingHandler)(uint32_t from, const uint8_t *payload, size_t len);
void meshSetSightingHandler(SightingHandler h);

// "Be the fox": a short packet every 15 s that only badges in direct range hear, so they can
// hunt this badge (Fox hunt -> LoRa node). Not saved: off after a reboot.
bool meshBeingFox();
void meshSetBeingFox(bool on);

} // namespace badge

// The badge's mesh features, as one Meshtastic module that sees every received packet:
//  * LED alerts for messages, a moon/logo blink for mesh traffic
//  * meeting new badges (badge collection), the "far away" achievement
//  * remote "!" commands in direct messages (!lights, !bright, !speed, !help)
//  * synced light shows, tic-tac-toe moves and scavenger-hunt beacons on the private
//    app port (256), and the fox hunt signal readings
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace badge
{

void meshStart(); // from lateInitVariant()

uint32_t meshMyNode();
const char *meshNodeName(uint32_t node, char *buf, size_t n); // long name, short name or !hex
bool meshSendPrivate(uint32_t to, const uint8_t *data, size_t len, bool wantAck);
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

} // namespace badge

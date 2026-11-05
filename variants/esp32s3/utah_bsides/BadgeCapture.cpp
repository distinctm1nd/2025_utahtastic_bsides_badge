#include "configuration.h"

#include "BadgeCapture.h"

#include "Channels.h"
#include "FSCommon.h"
#include "NodeDB.h"
#include "SPILock.h"
#include "gps/RTC.h"
#include "mesh/RadioLibInterface.h"
#include "mesh/mesh-pb-constants.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <string.h>

namespace badge
{

static const char *FILE_PATH = "/badge/capture.pcap";
static const char *WIFI_PATH = "/badge/wifi.pcap";
static const char *BLE_PATH = "/badge/ble.pcap";
static const size_t MAX_SIZE = 1024 * 1024;      // mesh
static const size_t WIFI_MAX = 512 * 1024;
static const size_t BLE_MAX = 256 * 1024;
static bool on = false, full = false;
static uint32_t count = 0;
static bool wifiOn = false, wifiFull = false, bleOn = false, bleFull = false;
static uint32_t wifiCount = 0, bleCount = 0;

// live-stream ring (bytes of a pcap record stream, no file involved)
static const int STR_SIZE = 8192;
static uint8_t *strBuf = nullptr;
static volatile int strHead = 0, strTail = 0, streamKind = 0;
static portMUX_TYPE strLock = portMUX_INITIALIZER_UNLOCKED;

static void strPush(const uint8_t *d, size_t n)
{
    if (!strBuf)
        return;
    portENTER_CRITICAL(&strLock);
    for (size_t i = 0; i < n; i++) {
        int next = (strHead + 1) % STR_SIZE;
        if (next == strTail)
            break; // ring full: drop the rest (the socket isn't keeping up)
        strBuf[strHead] = d[i];
        strHead = next;
    }
    portEXIT_CRITICAL(&strLock);
}

static void put32le(uint8_t *p, uint32_t v);
static void streamRec(const uint8_t *payload, size_t len)
{
    uint8_t hdr[16];
    uint32_t epoch = getValidTime(RTCQualityDevice), ms = millis();
    put32le(hdr, epoch ? epoch : ms / 1000);
    put32le(hdr + 4, (ms % 1000) * 1000);
    put32le(hdr + 8, len);
    put32le(hdr + 12, len);
    strPush(hdr, 16);
    strPush(payload, len);
}

void captureStreamSet(int kind)
{
    if (kind != STREAM_OFF && !strBuf)
        strBuf = (uint8_t *)heap_caps_malloc(STR_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    portENTER_CRITICAL(&strLock);
    strHead = strTail = 0;
    streamKind = strBuf ? kind : STREAM_OFF;
    portEXIT_CRITICAL(&strLock);
}
int captureStreamKind() { return streamKind; }
uint32_t captureStreamLinktype() { return streamKind == STREAM_BLE ? 251 : 270; }
int captureStreamDrain(uint8_t *out, int max)
{
    int n = 0;
    portENTER_CRITICAL(&strLock);
    while (n < max && strTail != strHead) {
        out[n++] = strBuf[strTail];
        strTail = (strTail + 1) % STR_SIZE;
    }
    portEXIT_CRITICAL(&strLock);
    return n;
}

static void put32le(uint8_t *p, uint32_t v)
{
    p[0] = v, p[1] = v >> 8, p[2] = v >> 16, p[3] = v >> 24;
}
static void put32be(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24, p[1] = v >> 16, p[2] = v >> 8, p[3] = v;
}

// append one record to a pcap file (writing the global header on the first write); caps the size
static bool writeRec(const char *path, uint32_t linktype, uint32_t snaplen, const uint8_t *data, size_t len,
                     size_t maxSize, uint32_t &cnt, bool &isFull)
{
    uint8_t hdr[16];
    uint32_t epoch = getValidTime(RTCQualityDevice), ms = millis();
    put32le(hdr, epoch ? epoch : ms / 1000);
    put32le(hdr + 4, (ms % 1000) * 1000);
    put32le(hdr + 8, len);
    put32le(hdr + 12, len);
    concurrency::LockGuard g(spiLock);
    if (!FSCom.exists("/badge"))
        FSCom.mkdir("/badge");
    File f = FSCom.open(path, FILE_O_READ);
    size_t size = f ? f.size() : 0;
    if (f)
        f.close();
    if (size + 16 + len > maxSize) {
        isFull = true;
        return false;
    }
    f = FSCom.open(path, "a");
    if (!f)
        return false;
    if (!size) {
        uint8_t gh[24] = {0xd4, 0xc3, 0xb2, 0xa1, 2, 0, 4, 0};
        put32le(gh + 16, snaplen);
        put32le(gh + 20, linktype);
        f.write(gh, sizeof(gh));
        cnt = 0;
    }
    f.write(hdr, 16);
    f.write(data, len);
    f.close();
    cnt++;
    return true;
}

// spreading factor and bandwidth (LoRaTap: 1 = 125 kHz, 2 = 250, 4 = 500) of the preset in use
static void modem(uint8_t &sf, uint8_t &bw)
{
    sf = 0, bw = 0;
    if (!config.lora.use_preset)
        return;
    switch (config.lora.modem_preset) {
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST: sf = 11, bw = 2; break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW: sf = 12, bw = 1; break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE: sf = 11, bw = 1; break;
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST: sf = 9, bw = 2; break;
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW: sf = 10, bw = 2; break;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST: sf = 7, bw = 2; break;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW: sf = 8, bw = 2; break;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO: sf = 7, bw = 4; break;
    default: break;
    }
}

bool captureOn()
{
    return on;
}

void captureSet(bool v)
{
    on = v;
}

uint32_t captureCount()
{
    return count;
}

bool captureFull()
{
    return full;
}

void captureClear()
{
    concurrency::LockGuard g(spiLock);
    FSCom.remove(FILE_PATH);
    count = 0;
    full = false;
}

// ---- WiFi ----------------------------------------------------------------------------------
bool captureWifiOn() { return wifiOn; }
void captureWifiSet(bool v) { wifiOn = v; }
uint32_t captureWifiCount() { return wifiCount; }
bool captureWifiFull() { return wifiFull; }

void captureWifiFrame(const uint8_t *frame, uint16_t len, uint8_t channel)
{
    (void)channel;
    if (!wifiOn || wifiFull || !len)
        return;
    if (len > 256)
        len = 256; // snap: full management frames, headers of big data frames
    writeRec(WIFI_PATH, 105 /*LINKTYPE_IEEE802_11*/, 256, frame, len, WIFI_MAX, wifiCount, wifiFull);
}

// ---- BLE advertising -----------------------------------------------------------------------
bool captureBleOn() { return bleOn; }
void captureBleSet(bool v) { bleOn = v; }
uint32_t captureBleCount() { return bleCount; }
bool captureBleFull() { return bleFull; }

void captureBleAdv(const uint8_t macPrinted[6], bool randomAddr, uint8_t advType, const uint8_t *adv, uint8_t advLen)
{
    bool wantFile = bleOn && !bleFull, wantStream = streamKind == STREAM_BLE;
    if (!wantFile && !wantStream)
        return;
    if (advLen > 37)
        advLen = 37;
    uint8_t rec[4 + 2 + 6 + 37 + 3];
    // LINKTYPE_BLUETOOTH_LE_LL (251): access address, PDU header, payload, CRC
    rec[0] = 0xD6, rec[1] = 0xBE, rec[2] = 0x89, rec[3] = 0x8E; // advertising access address
    rec[4] = (advType & 0x0F) | (randomAddr ? 0x40 : 0);       // PDU type + TxAdd
    rec[5] = 6 + advLen;                                       // payload length (AdvA + AdvData)
    for (int i = 0; i < 6; i++)                                // AdvA, little-endian (reverse of printed)
        rec[6 + i] = macPrinted[5 - i];
    memcpy(rec + 12, adv, advLen);
    rec[12 + advLen] = rec[13 + advLen] = rec[14 + advLen] = 0; // CRC placeholder
    size_t rlen = 4 + 2 + 6 + advLen + 3;
    if (wantStream)
        streamRec(rec, rlen);
    if (wantFile)
        writeRec(BLE_PATH, 251, 64, rec, rlen, BLE_MAX, bleCount, bleFull);
}

void captureClearAll()
{
    concurrency::LockGuard g(spiLock);
    FSCom.remove(FILE_PATH);
    FSCom.remove(WIFI_PATH);
    FSCom.remove(BLE_PATH);
    count = wifiCount = bleCount = 0;
    full = wifiFull = bleFull = false;
}

void capturePacket(const meshtastic_MeshPacket &p)
{
    if (p.which_payload_variant != meshtastic_MeshPacket_decoded_tag)
        return;
    bool wantFile = on && !full, wantStream = streamKind == STREAM_MESH;
    if (!wantFile && !wantStream)
        return;
    uint8_t rec[15 + 16 + 256];
    // LoRaTap v0 header (big-endian): version, padding, length, frequency (Hz), bandwidth, SF,
    // packet / max / current RSSI (+139), SNR (quarter dB), sync word
    uint8_t *lt = rec;
    memset(lt, 0, 15);
    lt[3] = 15;
    float mhz = RadioLibInterface::instance ? RadioLibInterface::instance->getFreq() : 0;
    put32be(lt + 4, (uint32_t)(mhz * 1e6f));
    modem(lt[9], lt[8]);
    int rssi = p.rx_rssi + 139;
    lt[10] = lt[11] = lt[12] = rssi < 0 ? 0 : rssi > 255 ? 255 : rssi;
    lt[13] = (uint8_t)(int8_t)(p.rx_snr * 4);
    lt[14] = 0x2b;
    // Meshtastic's over-the-air header (little-endian): to, from, id, flags, channel hash,
    // next hop, relay node
    uint8_t *h = lt + 15;
    put32le(h, p.to);
    put32le(h + 4, p.from);
    put32le(h + 8, p.id);
    h[12] = (p.hop_limit & 7) | (p.want_ack ? 0x08 : 0) | (p.via_mqtt ? 0x10 : 0) | ((p.hop_start & 7) << 5);
    h[13] = (!p.pki_encrypted && p.channel < channels.getNumChannels()) ? (uint8_t)channels.getHash(p.channel) : 0;
    h[14] = p.next_hop;
    h[15] = p.relay_node;
    // the payload, as the decrypted meshtastic.Data protobuf
    size_t n = pb_encode_to_bytes(h + 16, 256, &meshtastic_Data_msg, &p.decoded);
    size_t len = 15 + 16 + n;
    if (wantStream)
        streamRec(rec, len);
    if (wantFile)
        writeRec(FILE_PATH, 270 /*LINKTYPE_LORATAP*/, 65535, rec, len, MAX_SIZE, count, full);
}

} // namespace badge

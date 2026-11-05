#include "configuration.h"

#include "BadgeLog.h"

#include "FSCommon.h"
#include "NodeDB.h"
#include "SPILock.h"
#include "gps/RTC.h"
#include "mesh/Throttle.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace badge
{

static const char *DIR = "/badge";
static const char *LOG = "/badge/sightings.csv";
static const char *OLD = "/badge/sightings.old.csv";
static const char *SEEN = "/badge/seen.bin";
static const size_t MAX_LOG = 512 * 1024;
static const uint32_t AGAIN_GAP_S = 30 * 60; // a sighting counts as a new one after this long

struct SeenRec {
    uint8_t mac[6];
    uint16_t count;
    uint32_t lastEpoch; // when last seen (0 if the time wasn't known)
    uint32_t lastBoot;  // reboot count then
    uint32_t lastUptime;
};
static const int MAX_SEEN = 256;
static SeenRec *seen = nullptr; // loaded on first use (PSRAM)
static int nSeen = 0;
static bool seenDirty = false;
static uint32_t lastSave = 0;

static bool ensureDir()
{
    return FSCom.exists(DIR) || FSCom.mkdir(DIR);
}

static void position(int32_t &lat, int32_t &lon)
{
    lat = lon = 0;
    if (!nodeDB)
        return;
    meshtastic_PositionLite pos; // 2.8 keeps node positions in NodeDB, not on NodeInfoLite
    if (nodeDB->copyNodePosition(nodeDB->getNodeNum(), pos)) {
        lat = pos.latitude_i;
        lon = pos.longitude_i;
    }
}

static void clean(char *out, size_t n, const char *in) // no commas / quotes / newlines in a field
{
    size_t j = 0;
    for (; in && *in && j + 1 < n; in++)
        out[j++] = (*in == ',' || *in == '"' || *in == '\n' || *in == '\r') ? ' ' : *in;
    out[j] = 0;
}

void logEvent(const char *event, const char *code, const uint8_t mac[6], int rssi, const char *title, const char *extra)
{
    char t[48], x[48], line[200];
    clean(t, sizeof(t), title);
    clean(x, sizeof(x), extra);
    int32_t lat, lon;
    position(lat, lon);
    snprintf(line, sizeof(line), "%u,%u,%u,%s,%s,%02X:%02X:%02X:%02X:%02X:%02X,%d,%ld,%ld,%s,%s\n",
             (unsigned)getValidTime(RTCQualityDevice), (unsigned)myNodeInfo.reboot_count, (unsigned)(millis() / 1000), event,
             code ? code : "", mac ? mac[0] : 0, mac ? mac[1] : 0, mac ? mac[2] : 0, mac ? mac[3] : 0, mac ? mac[4] : 0,
             mac ? mac[5] : 0, rssi, (long)lat, (long)lon, t, x);
    concurrency::LockGuard g(spiLock);
    if (!ensureDir())
        return;
    File f = FSCom.open(LOG, FILE_O_READ);
    size_t size = f ? f.size() : 0;
    if (f)
        f.close();
    if (size > MAX_LOG) {
        FSCom.remove(OLD);
        FSCom.rename(LOG, OLD);
    }
    f = FSCom.open(LOG, "a");
    if (f) {
        if (!size || size > MAX_LOG)
            f.print("epoch,boot,uptime_s,event,code,address,dbm,lat_e7,lon_e7,title,extra\n");
        f.print(line);
        f.close();
    }
}

static void loadSeen()
{
    if (seen)
        return;
    seen = (SeenRec *)calloc(MAX_SEEN, sizeof(SeenRec));
    if (!seen)
        return;
    concurrency::LockGuard g(spiLock);
    File f = FSCom.open(SEEN, FILE_O_READ);
    if (f) {
        nSeen = f.read((uint8_t *)seen, MAX_SEEN * sizeof(SeenRec)) / sizeof(SeenRec);
        f.close();
    }
}

static void saveSeen(bool force)
{
    if (!seen || !seenDirty || (!force && lastSave && Throttle::isWithinTimespanMs(lastSave, 60000)))
        return;
    concurrency::LockGuard g(spiLock);
    if (!ensureDir())
        return;
    File f = FSCom.open(SEEN, FILE_O_WRITE);
    if (f) {
        f.write((const uint8_t *)seen, nSeen * sizeof(SeenRec));
        f.close();
        seenDirty = false;
        lastSave = millis();
    }
}

int logSeenBefore(const uint8_t mac[6], uint32_t *agoS)
{
    *agoS = 0;
    loadSeen();
    if (!seen)
        return 0;
    uint32_t epoch = getValidTime(RTCQualityDevice), boot = myNodeInfo.reboot_count, up = millis() / 1000;
    SeenRec *r = nullptr;
    for (int i = 0; i < nSeen; i++)
        if (memcmp(seen[i].mac, mac, 6) == 0)
            r = &seen[i];
    int before = 0;
    if (r) {
        bool sameBoot = r->lastBoot == boot;
        uint32_t gap = epoch && r->lastEpoch ? epoch - r->lastEpoch : sameBoot ? up - r->lastUptime : AGAIN_GAP_S;
        if (gap >= AGAIN_GAP_S) {
            before = r->count;
            *agoS = (epoch && r->lastEpoch) || sameBoot ? gap : 0;
            r->count++;
        }
    } else {
        if (nSeen < MAX_SEEN) {
            r = &seen[nSeen++];
        } else { // replace the one seen longest ago
            r = &seen[0];
            for (int i = 1; i < nSeen; i++)
                if (seen[i].lastEpoch < r->lastEpoch)
                    r = &seen[i];
        }
        memcpy(r->mac, mac, 6);
        r->count = 1;
    }
    r->lastEpoch = epoch;
    r->lastBoot = boot;
    r->lastUptime = up;
    seenDirty = true;
    saveSeen(before > 0);
    return before;
}

int logReadRecent(LogLine *out, int max)
{
    concurrency::LockGuard g(spiLock);
    File f = FSCom.open(LOG, FILE_O_READ);
    if (!f)
        return 0;
    size_t size = f.size(), chunk = 8192;
    size_t from = size > chunk ? size - chunk : 0;
    char *buf = (char *)malloc(chunk + 1);
    if (!buf) {
        f.close();
        return 0;
    }
    f.seek(from);
    size_t n = f.read((uint8_t *)buf, chunk);
    f.close();
    buf[n] = 0;
    int count = 0;
    char *end = buf + n;
    while (end > buf && count < max) { // walk back line by line
        while (end > buf && (end[-1] == '\n' || end[-1] == '\r'))
            *--end = 0;
        char *start = end;
        while (start > buf && start[-1] != '\n')
            start--;
        if (start == buf && from > 0)
            break; // a partial first line
        if (strncmp(start, "epoch,", 6) != 0 && *start) {
            strncpy(out[count].text, start, sizeof(out[count].text) - 1);
            out[count].text[sizeof(out[count].text) - 1] = 0;
            count++;
        }
        end = start;
    }
    free(buf);
    return count;
}

void logClear()
{
    concurrency::LockGuard g(spiLock);
    FSCom.remove(LOG);
    FSCom.remove(OLD);
    FSCom.remove(SEEN);
    nSeen = 0;
    seenDirty = false;
}

} // namespace badge

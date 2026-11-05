#include "configuration.h"

#include "BadgeDevices.h"

#include "FSCommon.h"
#include "SPILock.h"
#include <string.h>

namespace badge
{

static const char *FILE_PATH = "/badge/devices.bin";
static const int MAX_DEVS = 64;
static DevMemo devs[MAX_DEVS];
static int count = -1;

static void load()
{
    if (count >= 0)
        return;
    count = 0;
    concurrency::LockGuard g(spiLock);
    File f = FSCom.open(FILE_PATH, FILE_O_READ);
    if (!f)
        return;
    int n = f.read((uint8_t *)devs, sizeof(devs)) / (int)sizeof(DevMemo);
    f.close();
    count = n < 0 ? 0 : n;
    for (int i = 0; i < count; i++)
        devs[i].nick[sizeof(devs[i].nick) - 1] = 0;
}

static void save()
{
    concurrency::LockGuard g(spiLock);
    if (!FSCom.exists("/badge"))
        FSCom.mkdir("/badge");
    File f = FSCom.open(FILE_PATH, FILE_O_WRITE);
    if (!f)
        return;
    f.write((const uint8_t *)devs, sizeof(DevMemo) * count);
    f.close();
}

const DevMemo *devFind(const uint8_t mac[6])
{
    load();
    for (int i = 0; i < count; i++)
        if (!memcmp(devs[i].mac, mac, 6))
            return &devs[i];
    return nullptr;
}

bool devTrusted(const uint8_t mac[6])
{
    const DevMemo *d = devFind(mac);
    return d && (d->flags & DEV_TRUSTED);
}

const char *devName(const uint8_t mac[6], const char *fallback)
{
    const DevMemo *d = devFind(mac);
    return d && d->nick[0] ? d->nick : fallback;
}

void devSet(const uint8_t mac[6], uint8_t flags, const char *nick, bool wifi)
{
    load();
    int i = 0;
    while (i < count && memcmp(devs[i].mac, mac, 6))
        i++;
    if (i == count) {
        if (count == MAX_DEVS) // full: drop the oldest entry
            memmove(devs, devs + 1, sizeof(DevMemo) * --count);
        i = count++;
        memset(&devs[i], 0, sizeof(DevMemo));
        memcpy(devs[i].mac, mac, 6);
    }
    DevMemo &d = devs[i];
    d.flags = (uint8_t)((flags & ~DEV_WIFI) | (wifi ? DEV_WIFI : 0));
    if (nick) {
        strncpy(d.nick, nick, sizeof(d.nick) - 1);
        d.nick[sizeof(d.nick) - 1] = 0;
    }
    if (!(d.flags & ~DEV_WIFI) && !d.nick[0]) { // nothing left to remember
        memmove(&devs[i], &devs[i + 1], sizeof(DevMemo) * (count - i - 1));
        count--;
    }
    save();
}

int devCount()
{
    load();
    return count;
}

const DevMemo &devAt(int i)
{
    load();
    return devs[i < 0 ? 0 : i >= count ? count - 1 : i];
}

} // namespace badge

// ---- nickname editor -------------------------------------------------------------------------
#include "BadgeApps.h"
namespace badge
{
static uint8_t nickMac[6];
static bool nickWifi;
static void (*nickBack)(const uint8_t mac[6], bool wifi);
static void nickDone(const char *text)
{
    if (text) {
        const DevMemo *d = devFind(nickMac);
        devSet(nickMac, d ? d->flags : 0, text, nickWifi);
    }
    if (nickBack)
        nickBack(nickMac, nickWifi);
}
void deviceNicknameEdit(const uint8_t mac[6], bool wifi, void (*back)(const uint8_t mac[6], bool wifi))
{
    memcpy(nickMac, mac, 6);
    nickWifi = wifi;
    nickBack = back;
    const DevMemo *d = devFind(mac);
    openTextEditor("Nickname", d ? d->nick : "", 16, 1, nickDone);
}
} // namespace badge

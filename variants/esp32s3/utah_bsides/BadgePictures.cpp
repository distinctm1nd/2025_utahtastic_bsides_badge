#include "configuration.h"

#include "BadgePictures.h"

#include "FSCommon.h"
#include "SPILock.h"
#include <Arduino.h>
#include <Preferences.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace badge
{

static const char *DIR = "/badge/pics";
static const size_t HEADER = 4 + 8 + 24;
static const size_t MAX_FILE = 512 * 1024;

struct Entry {
    int num; // NN of NN.bpi
    char name[25];
};
static Entry entries[MAX_PICTURES];
static int count = -1; // -1: not listed yet
static uint8_t *loaded = nullptr;
static size_t loadedLen = 0;

static void path(char *out, size_t n, int num)
{
    snprintf(out, n, "%s/%02d.bpi", DIR, num);
}

static void list()
{
    count = 0;
    concurrency::LockGuard g(spiLock);
    File dir = FSCom.open(DIR, FILE_O_READ);
    if (!dir)
        return;
    for (File f = dir.openNextFile(); f && count < MAX_PICTURES; f = dir.openNextFile()) {
        const char *base = strrchr(f.name(), '/');
        base = base ? base + 1 : f.name();
        int num;
        uint8_t h[HEADER];
        if (sscanf(base, "%d.bpi", &num) == 1 && f.read(h, HEADER) == (int)HEADER && !memcmp(h, "BPI1", 4)) {
            entries[count].num = num;
            memcpy(entries[count].name, h + 12, 24);
            entries[count].name[24] = 0;
            count++;
        }
        f.close();
    }
    dir.close();
    for (int i = 1; i < count; i++) // in number order
        for (int j = i; j > 0 && entries[j - 1].num > entries[j].num; j--) {
            Entry t = entries[j];
            entries[j] = entries[j - 1];
            entries[j - 1] = t;
        }
}

int picCount()
{
    if (count < 0)
        list();
    return count;
}

const char *picName(int i)
{
    return i >= 0 && i < picCount() ? entries[i].name : "";
}

void picUnload()
{
    free(loaded);
    loaded = nullptr;
    loadedLen = 0;
}

bool picLoad(int i, BadgeImage &out)
{
    picUnload();
    if (i < 0 || i >= picCount())
        return false;
    char p[40];
    path(p, sizeof(p), entries[i].num);
    concurrency::LockGuard g(spiLock);
    File f = FSCom.open(p, FILE_O_READ);
    if (!f)
        return false;
    size_t n = f.size();
    if (n <= HEADER || n > MAX_FILE || !(loaded = (uint8_t *)malloc(n))) { // big: lands in PSRAM
        f.close();
        return false;
    }
    int got = f.read(loaded, n);
    f.close();
    loadedLen = got > 0 ? (size_t)got : 0;
    if (loadedLen <= HEADER) {
        picUnload();
        return false;
    }
    auto u16 = [&](int o) { return (uint16_t)(loaded[o] | (loaded[o + 1] << 8)); };
    out.name = entries[i].name;
    out.w = u16(4);
    out.h = u16(6);
    out.frames = u16(8);
    out.frameMs = u16(10);
    out.bits = loaded + HEADER;
    out.check = 0;
    if (out.w == 0 || out.w > 160 || out.h == 0 || out.h > 128 || out.frames == 0) {
        picUnload();
        return false;
    }
    return true;
}

const uint8_t *picDataEnd(const BadgeImage &im)
{
    return loaded && im.bits == loaded + HEADER ? loaded + loadedLen : nullptr;
}

const char *picSave(const uint8_t *data, size_t len)
{
    if (len <= HEADER || memcmp(data, "BPI1", 4))
        return "Not a converted picture";
    uint16_t w = data[4] | (data[5] << 8), h = data[6] | (data[7] << 8), frames = data[8] | (data[9] << 8);
    if (!w || w > 160 || !h || h > 128 || !frames || frames > 96)
        return "Picture size not supported";
    if (len > MAX_FILE)
        return "Picture too big (512 KB at most)";
    if (picCount() >= MAX_PICTURES)
        return "40 pictures already: delete one first";
    if (len + 64 * 1024 > picFreeSpace())
        return "Badge storage is full";
    int num = 0;
    for (int i = 0; i < count; i++)
        if (entries[i].num >= num)
            num = entries[i].num + 1;
    char p[40];
    path(p, sizeof(p), num);
    {
        concurrency::LockGuard g(spiLock);
        if (!FSCom.exists("/badge"))
            FSCom.mkdir("/badge");
        if (!FSCom.exists(DIR))
            FSCom.mkdir(DIR);
        File f = FSCom.open(p, FILE_O_WRITE);
        if (!f)
            return "Couldn't save the picture";
        size_t wrote = f.write(data, len);
        f.close();
        if (wrote != len) {
            FSCom.remove(p);
            return "Couldn't save the picture (storage full?)";
        }
    }
    count = -1;
    return nullptr;
}

bool picDelete(int i)
{
    if (i < 0 || i >= picCount())
        return false;
    char p[40];
    path(p, sizeof(p), entries[i].num);
    {
        concurrency::LockGuard g(spiLock);
        FSCom.remove(p);
    }
    count = -1;
    return true;
}

bool picMove(int i, int dir)
{
    int j = i + dir;
    if (i < 0 || j < 0 || i >= picCount() || j >= count)
        return false;
    char a[40], b[40], t[40];
    path(a, sizeof(a), entries[i].num);
    path(b, sizeof(b), entries[j].num);
    snprintf(t, sizeof(t), "%s/swap.tmp", DIR);
    {
        concurrency::LockGuard g(spiLock);
        FSCom.rename(a, t);
        FSCom.rename(b, a);
        FSCom.rename(t, b);
    }
    count = -1;
    return true;
}

size_t picFreeSpace()
{
    concurrency::LockGuard g(spiLock);
    size_t total = FSCom.totalBytes(), used = FSCom.usedBytes();
    return total > used ? total - used : 0;
}

const char *picStartup()
{
    static char what[25];
    Preferences p;
    what[0] = 0;
    if (p.begin("utah", true)) {
        p.getString("startpic", what, sizeof(what));
        p.end();
    }
    return what;
}

void picSetStartup(const char *what)
{
    Preferences p;
    if (p.begin("utah", false)) {
        p.putString("startpic", what ? what : "");
        p.end();
    }
}

} // namespace badge

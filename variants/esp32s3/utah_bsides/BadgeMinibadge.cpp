// Minibadge I2C host. See BadgeMinibadge.h. Protocol (from the minibadge repo):
//   read:  write [0x00 0x01], read 1 byte: 0x00 nothing, 0x01 button pressed,
//          0x02 text -> read 1 byte length, then read that many ASCII bytes
//   write: write [0x00 0x00], read 1 byte: 0x01 = accepts writes; then
//          write [0x01 hi lo] score, or [0x02 b] brightness 0..127
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeLeds.h"
#include "BadgeMinibadge.h"
#include "BadgeStats.h"

#include "concurrency/OSThread.h"
#include "graphics/Screen.h"
#include <Wire.h>
#include <stdio.h>
#include <string.h>


#define MB_SDA_PIN 5
#define MB_SCL_PIN 6

namespace badge
{

static const int MAXMB = 8;
static Minibadge mbs[MAXMB];
static int nmb = 0;
static TwoWire &bus = Wire1;

static bool present(uint8_t a)
{
    bus.beginTransmission(a);
    return bus.endTransmission() == 0;
}

static int readByte(uint8_t a)
{
    if (bus.requestFrom(a, (uint8_t)1) != 1)
        return -1;
    return bus.read();
}

static bool command(uint8_t a, uint8_t mode)
{
    bus.beginTransmission(a);
    bus.write(0x00);
    bus.write(mode);
    return bus.endTransmission() == 0;
}

static bool supportsWrite(uint8_t a)
{
    return command(a, 0x00) && readByte(a) == 0x01;
}

static void sendBrightness(Minibadge &m)
{
    if (!m.canWrite)
        return;
    static const uint8_t LEVEL[] = {10, 30, 60, 95, 127};
    int b = ledsBright();
    uint8_t v = LEVEL[(b >= 0 && b < 5) ? b : 2];
    if (!supportsWrite(m.addr))
        return;
    bus.beginTransmission(m.addr);
    bus.write(0x02);
    bus.write(v);
    bus.endTransmission();
}

static void poll(Minibadge &m)
{
    if (!command(m.addr, 0x01))
        return;
    int cmd = readByte(m.addr);
    if (cmd == 0x01) {
        m.presses++;
        ledsFlourish();
    } else if (cmd == 0x02) {
        int len = readByte(m.addr);
        if (len <= 0)
            return;
        char t[64];
        int got = 0;
        while (len > 0) { // read in chunks: the Arduino Wire buffer is limited
            uint8_t n = (uint8_t)(len > 32 ? 32 : len);
            if (bus.requestFrom(m.addr, n) != n)
                break;
            for (int i = 0; i < n; i++) {
                int c = bus.read();
                if (got < (int)sizeof(t) - 1)
                    t[got++] = (c >= 32 && c < 127) ? (char)c : ' ';
            }
            len -= n;
        }
        t[got] = 0;
        if (got && strcmp(t, m.text) != 0) {
            strncpy(m.text, t, sizeof(m.text) - 1);
            static char msg[96];
            snprintf(msg, sizeof(msg), "Minibadge says:\n%s", m.text);
            if (screen && !appActive() && !screen->isOverlayBannerShowing())
                screen->showSimpleBanner(msg, 4000);
        }
    }
}

class MinibadgeThread : public concurrency::OSThread
{
  public:
    MinibadgeThread() : concurrency::OSThread("Minibadge") {}

  protected:
    int32_t runOnce() override
    {
        uint32_t now = millis();
        if (now - lastScan > 10000 || lastScan == 0) {
            lastScan = now;
            scan();
        }
        for (int i = 0; i < nmb; i++)
            poll(mbs[i]);
        if (ledsBright() != lastBright) {
            lastBright = ledsBright();
            for (int i = 0; i < nmb; i++)
                sendBrightness(mbs[i]);
        }
        return 2000;
    }

  private:
    uint32_t lastScan = 0;
    int lastBright = -1;

    void scan()
    {
        Minibadge found[MAXMB];
        int n = 0;
        for (uint8_t a = 0x08; a < 0x78 && n < MAXMB; a++) {
            if (!present(a))
                continue;
            Minibadge m = {};
            m.addr = a;
            for (int i = 0; i < nmb; i++) // keep what we knew
                if (mbs[i].addr == a)
                    m = mbs[i];
            if (!m.canWrite)
                m.canWrite = supportsWrite(a);
            found[n++] = m;
        }
        bool added = n > nmb;
        memcpy(mbs, found, sizeof(Minibadge) * n);
        nmb = n;
        if (n)
            statsAchieve(ACH_MINIBADGE);
        if (added)
            for (int i = 0; i < nmb; i++)
                sendBrightness(mbs[i]);
    }
};

static MinibadgeThread *thread = nullptr;

void minibadgeStart()
{
    bus.begin(MB_SDA_PIN, MB_SCL_PIN, 100000);
    bus.setTimeOut(20);
    if (!thread)
        thread = new MinibadgeThread();
}

int minibadgeCount()
{
    return nmb;
}

const Minibadge &minibadgeAt(int i)
{
    return mbs[(i >= 0 && i < nmb) ? i : 0];
}

void minibadgeSendScore(uint16_t score)
{
    for (int i = 0; i < nmb; i++) {
        if (!mbs[i].canWrite || !supportsWrite(mbs[i].addr))
            continue;
        bus.beginTransmission(mbs[i].addr);
        bus.write(0x01);
        bus.write((uint8_t)(score >> 8));
        bus.write((uint8_t)(score & 0xFF));
        bus.endTransmission();
    }
}

} // namespace badge

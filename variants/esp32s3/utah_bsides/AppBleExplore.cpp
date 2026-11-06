// Bluetooth explorer: connect to one device from the scans and list what it offers (its
// services, and the everyday values anyone may read: name, maker, model, battery). Read only:
// it never writes, and reads that need pairing are skipped, not paired for.
//
//   joystick (or 2 / 8) scroll, # look again, * back
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeDeviceMenu.h"
#include "BadgeFollow.h"
#include "BadgeRadioScan.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <host/ble_gatt.h> // raw GATT client read (plainRead) + os_mbuf, independent of the wrapper
#include <stdio.h>
#include <string.h>

namespace badge
{

static const int MAX_LINES = 96, LINE_LEN = 44;
enum ExState { EX_IDLE, EX_BUSY, EX_DONE };
static volatile ExState exState = EX_IDLE;
static char (*exLines)[LINE_LEN] = nullptr; // PSRAM
static volatile int exCount = 0;
static char exStatus[48] = "";
static uint8_t exMac[6];
static bool exPublic = false;

static void addLine(const char *fmt, ...)
{
    if (exCount >= MAX_LINES)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(exLines[exCount], LINE_LEN, fmt, ap);
    va_end(ap);
    exCount++;
}

static const char *service16(uint16_t u)
{
    switch (u) {
    case 0x1800:
        return "Generic access";
    case 0x1801:
        return "Generic attribute";
    case 0x1802:
        return "Immediate alert (find me)";
    case 0x1803:
        return "Link loss alarm";
    case 0x1804:
        return "Transmit power";
    case 0x1805:
        return "Current time";
    case 0x1808:
        return "Glucose";
    case 0x1809:
        return "Thermometer";
    case 0x180A:
        return "Device information";
    case 0x180D:
        return "Heart rate";
    case 0x180E:
        return "Phone alerts";
    case 0x180F:
        return "Battery";
    case 0x1810:
        return "Blood pressure";
    case 0x1812:
        return "Keyboard / mouse (HID)";
    case 0x1814:
        return "Running speed";
    case 0x1816:
        return "Cycling speed";
    case 0x1818:
        return "Cycling power";
    case 0x1819:
        return "Location and navigation";
    case 0x181A:
        return "Environment sensor";
    case 0x181C:
        return "User data";
    case 0x181D:
        return "Weight scale";
    case 0x181E:
        return "Bond management";
    case 0x1822:
        return "Pulse oximeter";
    case 0x1826:
        return "Fitness machine";
    case 0x183B:
        return "Binary sensor";
    case 0x1843:
        return "Audio input";
    case 0x1844:
        return "Volume control";
    case 0x184E:
        return "Audio stream (LE Audio)";
    case 0xFD6F:
        return "Exposure notification";
    case 0xFE2C:
        return "Google Fast Pair";
    case 0xFE95:
        return "Xiaomi";
    case 0xFEAA:
        return "Eddystone beacon";
    case 0xFEED:
        return "Tile tracker";
    case 0xFE9F:
        return "Google";
    case 0xFD5A:
        return "Samsung SmartTag";
    default:
        return nullptr;
    }
}

static const char *service128(const BLEUUID &u)
{
    struct Known {
        const char *uuid, *name;
    };
    static const Known KNOWN[] = {
        {"7dfc9000-7d1c-4951-86aa-8d9728f8d66c", "AirTag sound"},
        {"15190001-12f4-c226-88ed-2ac5579f2a85", "Tracker alerts (DULT)"},
        {"6e400001-b5a3-f393-e0a9-e50e24dcca9e", "Serial (Nordic UART)"},
        {"6ba1b218-15a8-461f-9fa8-5dcae273eafd", "Meshtastic"},
        {"0000fe59-0000-1000-8000-00805f9b34fb", "Nordic firmware update"},
        {"8d53dc1d-1db7-4cd3-868b-8a527460aa84", "Firmware update (SMP)"},
        {"d0611e78-bbb4-4591-a5f8-487910ae4366", "Apple Continuity"},
        {"9fa480e0-4967-4542-9390-d343dc5d04ae", "Apple Nearby"},
    };
    for (const Known &k : KNOWN)
        if (u == BLEUUID(k.uuid))
            return k.name;
    return nullptr;
}

static const char *appearanceName(uint16_t v)
{
    static const char *CAT[] = {"unknown",
                                "phone",
                                "computer",
                                "watch",
                                "clock",
                                "display",
                                "remote control",
                                "glasses",
                                "tag",
                                "keyring",
                                "media player",
                                "barcode scanner",
                                "thermometer",
                                "heart rate sensor",
                                "blood pressure",
                                "keyboard / mouse",
                                "glucose meter",
                                "running sensor",
                                "cycling sensor",
                                "control device",
                                "network device",
                                "sensor",
                                "light",
                                "fan",
                                "heating / cooling",
                                "air conditioner",
                                "humidifier",
                                "heater",
                                "door / access",
                                "motorized device",
                                "power device",
                                "light source",
                                "window blind",
                                "speaker",
                                "audio source",
                                "vehicle",
                                "appliance",
                                "headphones / earbuds",
                                "aircraft",
                                "AV equipment",
                                "display equipment",
                                "hearing aid",
                                "game controller",
                                "signage"};
    unsigned c = v >> 6;
    return c < sizeof(CAT) / sizeof(CAT[0]) ? CAT[c] : "other";
}

// a plain GATT read: unlike NimBLE's readValue() it never starts pairing when a value is locked
struct ReadCtx {
    SemaphoreHandle_t done;
    volatile int status;
    uint8_t buf[48];
    volatile uint16_t len;
};
static ReadCtx rd;

static int onRead(uint16_t, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *)
{
    rd.status = err->status;
    rd.len = 0;
    if (err->status == 0 && attr) {
        uint16_t n = OS_MBUF_PKTLEN(attr->om);
        if (n > sizeof(rd.buf))
            n = sizeof(rd.buf);
        os_mbuf_copydata(attr->om, 0, n, rd.buf);
        rd.len = n;
    }
    xSemaphoreGive(rd.done);
    return 0;
}

// 0: read; otherwise the ATT / host error (locked values come back as an ATT error)
static int plainRead(BLEClient *c, uint16_t handle)
{
    xSemaphoreTake(rd.done, 0);
    if (ble_gattc_read(c->getConnId(), handle, onRead, nullptr) != 0)
        return -1;
    if (xSemaphoreTake(rd.done, pdMS_TO_TICKS(3000)) != pdTRUE)
        return -2;
    return rd.status;
}

static void valueText(const uint8_t *v, int n, char *out, size_t size)
{
    bool text = n > 0;
    for (int i = 0; i < n; i++)
        if (v[i] && (v[i] < 0x20 || v[i] > 0x7E))
            text = false;
    if (text) {
        snprintf(out, size, "%.*s", n, (const char *)v);
        return;
    }
    size_t o = 0;
    for (int i = 0; i < n && i < 10 && o + 3 < size; i++)
        o += snprintf(out + o, size - o, "%02X", v[i]);
}

// No client callbacks are installed: plainRead() uses a raw ble_gattc_read that never initiates
// pairing (a locked value just returns an ATT error), and connect() alone does not pair, so there
// is nothing to reject. (The framework BLEClientCallbacks has no passkey hooks to override anyway.)

static void readInfo(BLEClient *c)
{
    struct Field {
        uint16_t uuid;
        const char *label;
    };
    static const Field FIELDS[] = {{0x2A00, "Name"},     {0x2A29, "Maker"},    {0x2A24, "Model"},
                                   {0x2A25, "Serial"},   {0x2A27, "Hardware"}, {0x2A26, "Firmware"},
                                   {0x2A28, "Software"}, {0x2A19, "Battery"},  {0x2A01, "Looks like"}};
    std::map<std::string, BLERemoteService *> *svcs = c->getServices();
    int locked = 0;
    for (const Field &f : FIELDS)
        for (auto &kv : *svcs) {
            BLERemoteService *s = kv.second;
            BLERemoteCharacteristic *ch = s->getCharacteristic(BLEUUID(f.uuid));
            if (!ch || !ch->canRead())
                continue;
            int st = plainRead(c, ch->getHandle());
            if (st != 0) {
                locked += st > 0;
                break;
            }
            char v[40];
            if (f.uuid == 0x2A19 && rd.len >= 1)
                snprintf(v, sizeof(v), "%u%%", rd.buf[0]);
            else if (f.uuid == 0x2A01 && rd.len >= 2)
                snprintf(v, sizeof(v), "%s", appearanceName(rd.buf[0] | (rd.buf[1] << 8)));
            else
                valueText(rd.buf, rd.len, v, sizeof(v));
            if (v[0])
                addLine("%s: %s", f.label, v);
            break;
        }
    if (locked)
        addLine("(%d value%s locked: needs pairing)", locked, locked == 1 ? "" : "s");
}

// what a plain read returned: open, or which security the device demands (ATT errors come back
// as 0x0100 | att-code), so you can see if a value is exposed without encryption
static const char *secLabel(int st)
{
    if (st == 0)
        return "open - NO pairing";
    int att = (st >= 0x0100 && st < 0x0200) ? (st & 0xFF) : -1;
    switch (att) {
    case 0x0F:
        return "needs encryption";
    case 0x05:
        return "needs auth pairing (MITM)";
    case 0x08:
        return "needs authorization";
    case 0x0C:
        return "needs bigger key";
    case 0x02:
        return "not readable";
    default:
        return st < 0 ? "no answer" : "protected";
    }
}

static void listServices(BLEClient *c)
{
    std::map<std::string, BLERemoteService *> *svcs = c->getServices();
    addLine("Services (%d):", (int)svcs->size());
    int openReadable = 0, protReadable = 0, probes = 0;
    for (auto &kv : *svcs) {
        BLERemoteService *s = kv.second;
        BLEUUID u = s->getUUID();
        uint16_t u16 = u.bitSize() == 16 ? u.getNative()->u16.value : 0;
        if (u16 == 0x1800 || u16 == 0x1801) // every device has these
            continue;
        const char *name = u16 ? service16(u16) : service128(u);
        std::map<std::string, BLERemoteCharacteristic *> *chs = s->getCharacteristics();
        bool r = false, w = false, n = false;
        for (auto &ckv : *chs) {
            BLERemoteCharacteristic *ch = ckv.second;
            r |= ch->canRead();
            w |= ch->canWrite() || ch->canWriteNoResponse();
            n |= ch->canNotify() || ch->canIndicate();
        }
        char what[24];
        snprintf(what, sizeof(what), "%d: %s%s%s", (int)chs->size(), r ? "R" : "", w ? "W" : "", n ? "N" : "");
        if (name)
            addLine(" %s (%s)", name, what);
        else if (u16)
            addLine(" %04X (%s)", u16, what);
        else
            addLine(" %.8s.. (%s)", u.toString().c_str(), what);
        // probe each readable characteristic to see what security it demands
        for (auto &ckv : *chs) {
            BLERemoteCharacteristic *ch = ckv.second;
            if (!ch->canRead() || probes >= 50)
                continue;
            probes++;
            int st = plainRead(c, ch->getHandle());
            if (st == 0)
                openReadable++;
            else
                protReadable++;
            BLEUUID cu = ch->getUUID();
            uint16_t c16 = cu.bitSize() == 16 ? cu.getNative()->u16.value : 0;
            if (c16)
                addLine("  %04X: %s", c16, secLabel(st));
            else
                addLine("  %.8s..: %s", cu.toString().c_str(), secLabel(st));
        }
    }
    addLine("Readable: %d open, %d protected", openReadable, protReadable);
    if (openReadable)
        addLine("(open = readable with no pairing)");
}

static void exploreTask(void *)
{
    radioScanHold(true); // one radio: no scanning while connected
    vTaskDelay(pdMS_TO_TICKS(300));
    if (!rd.done)
        rd.done = xSemaphoreCreateBinary();
    char addr[18];
    snprintf(addr, sizeof(addr), "%02x:%02x:%02x:%02x:%02x:%02x", exMac[0], exMac[1], exMac[2], exMac[3], exMac[4], exMac[5]);
    bool ok = false;
    if (!BLEDevice::getInitialized()) {
        snprintf(exStatus, sizeof(exStatus), "Bluetooth is off");
    } else if (BLEClient *c = BLEDevice::createClient()) {
        const uint8_t t1 = exPublic ? BLE_ADDR_PUBLIC : BLE_ADDR_RANDOM;
        const uint8_t t2 = exPublic ? BLE_ADDR_RANDOM : BLE_ADDR_PUBLIC;
        // the scan's address type first; flagged devices may not know it (8 s connect timeout)
        ok = c->connect(BLEAddress(String(addr), t1), t1, 8000) || c->connect(BLEAddress(String(addr), t2), t2, 8000);
        if (ok) {
            snprintf(exStatus, sizeof(exStatus), "Reading...");
            readInfo(c);
            listServices(c);
            c->disconnect();
            snprintf(exStatus, sizeof(exStatus), "%s", exCount ? "" : "Connected, but it lists nothing");
        } else {
            snprintf(exStatus, sizeof(exStatus), "Couldn't connect (out of range, not connectable, or new address)");
        }
        delete c;
    }
    radioScanHold(false);
    exState = EX_DONE;
    vTaskDelete(nullptr);
}

class BleExploreApp : public App
{
  public:
    const char *name() const override { return "Explore"; }
    uint32_t frameMs() const override { return 300; }
    bool keepAwake() const override { return true; }
    void setTarget(const uint8_t mac[6], bool pub, const char *nm, void (*cb)(const uint8_t[6], bool))
    {
        memcpy(exMac, mac, 6);
        exPublic = pub;
        snprintf(title, sizeof(title), "%s", nm && *nm ? nm : "Explore");
        back = cb;
    }
    void begin() override
    {
        watchPause(true);
        top = 0;
        start();
    }
    void end() override { watchPause(false); }
    void key(int k, KeyAction a) override
    {
        if (k == AK_STAR && a == KA_RELEASE) {
            appClose();
            if (back)
                back(exMac, false);
            return;
        }
        if (a != KA_PRESS && a != KA_REPEAT)
            return;
        if ((k == AK_DOWN || k == AK_8) && top < exCount - 1)
            top++;
        else if ((k == AK_UP || k == AK_2) && top > 0)
            top--;
        else if ((k == AK_HASH || k == AK_5) && a == KA_PRESS && exState != EX_BUSY)
            start();
        appRedraw();
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        int lh = fontHeight(FONT_SMALL);
        drawTitle(d, x, y, title, exState == EX_BUSY ? "..." : "#again *back");
        int ry = y + lh + 2;
        if (exStatus[0]) { // word-wrapped status
            const char *m = exStatus;
            char line[48];
            d->setFont(FONT_SMALL);
            for (int row = 0; *m && row < 3; row++) {
                int n = strlen(m), cut = n;
                while (cut > 1 && d->getStringWidth(m, cut) > d->width() - 6) {
                    int sp = cut - 1;
                    while (sp > 0 && m[sp] != ' ')
                        sp--;
                    cut = sp > 0 ? sp : cut - 1;
                }
                snprintf(line, sizeof(line), "%.*s", cut, m);
                drawCentered(d, x, ry, line, FONT_SMALL);
                ry += lh;
                m += cut;
                while (*m == ' ')
                    m++;
            }
            ry += 2;
        }
        if (!exLines)
            return;
        d->setFont(FONT_SMALL);
        d->setTextAlignment(TEXT_ALIGN_LEFT);
        for (int i = top; i < exCount && ry + lh <= y + d->height(); i++, ry += lh)
            d->drawString(x + 2, ry, exLines[i]);
    }

  private:
    char title[40] = "Explore";
    int top = 0;
    void (*back)(const uint8_t[6], bool) = nullptr;
    void start()
    {
        bleExploreStart(exMac, exPublic);
        top = 0;
    }
};

static BleExploreApp &exploreApp()
{
    static BleExploreApp *p = psramNew<BleExploreApp>();
    return *p;
}

void openBleExplorer(const uint8_t mac[6], bool publicAddr, const char *name, void (*back)(const uint8_t mac[6], bool wifi))
{
    exploreApp().setTarget(mac, publicAddr, name, back);
    appOpen(&exploreApp());
}

// ---- standalone engine, so the Badge WiFi page can explore a device too --------------------
void bleExploreStart(const uint8_t mac[6], bool publicAddr)
{
    if (exState == EX_BUSY)
        return;
    if (!exLines)
        exLines = psramArray<char[LINE_LEN]>(MAX_LINES);
    memcpy(exMac, mac, 6);
    exPublic = publicAddr;
    exCount = 0;
    snprintf(exStatus, sizeof(exStatus), "Connecting...");
    exState = EX_BUSY;
    if (!exLines || xTaskCreate(exploreTask, "badgeexplore", 8192, nullptr, 1, nullptr) != pdPASS) {
        snprintf(exStatus, sizeof(exStatus), "Out of memory");
        exState = EX_DONE;
    }
}

int bleExploreState()
{
    return (int)exState; // 0 idle, 1 busy, 2 done
}
const char *bleExploreStatus()
{
    return exStatus;
}
int bleExploreCount()
{
    return exCount;
}
const char *bleExploreLine(int i)
{
    return exLines && i >= 0 && i < exCount ? exLines[i] : "";
}

} // namespace badge

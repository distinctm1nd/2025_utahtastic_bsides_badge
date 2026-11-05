// Bluetooth remote: the badge pairs with your own laptop or phone as an ordinary wireless
// mouse / keyboard, for moving a pointer, clicking through slides at a talk, or media keys.
// A standard BLE HID peripheral (appearance 0x03C0), added to Meshtastic's own Bluetooth server
// while this app is open and removed again on close. Nothing is injected without the computer
// pairing and bonding first, exactly like any shop-bought Bluetooth mouse.
//
//   joystick moves the pointer (mouse mode) / changes slides (clicker); # next mode; * done.
//   the keys for each mode are shown on the screen.
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeTextEntry.h"
#include "BluetoothCommon.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <stdio.h>
#include <string.h>
#include <string.h>

namespace badge
{

// one HID report map with three report IDs: mouse, keyboard, media keys
static const uint8_t REPORT_MAP[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00,
    0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x03, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
    0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06, 0xC0, 0xC0, // mouse (report 1)
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x02, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
    0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07,
    0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0, // keyboard (report 2)
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x03, 0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03, 0x75, 0x10,
    0x95, 0x01, 0x81, 0x00, 0xC0}; // media keys (report 3)

// USB HID usage codes
enum { K_ESC = 0x29, K_B = 0x05, K_F5 = 0x3E, K_PGUP = 0x4B, K_PGDN = 0x4E, K_RIGHT = 0x4F, K_LEFT = 0x50, K_DOWN = 0x51,
       K_UP = 0x52 };
enum { MED_PLAY = 0xCD, MED_NEXT = 0xB5, MED_PREV = 0xB6, MED_VUP = 0xE9, MED_VDN = 0xEA, MED_MUTE = 0xE2 };

static NimBLEService *disSvc = nullptr, *hidSvc = nullptr;
static NimBLECharacteristic *repMouse = nullptr, *repKeys = nullptr, *repMedia = nullptr;
static bool hidBuilt = false, hidAdded = false;
static volatile uint16_t hostConn = 0xFFFF; // the computer that turned our reports on

class ReportSub : public NimBLECharacteristicCallbacks
{
    void onSubscribe(NimBLECharacteristic *, ble_gap_conn_desc *desc, uint16_t subValue) override
    {
        if (subValue)
            hostConn = desc->conn_handle;
        else if (hostConn == desc->conn_handle)
            hostConn = 0xFFFF;
    }
};
static ReportSub reportSub;

static NimBLECharacteristic *inputReport(NimBLEService *s, uint8_t id)
{
    NimBLECharacteristic *c = s->createCharacteristic(
        (uint16_t)0x2A4D, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ_ENC);
    NimBLEDescriptor *ref = c->createDescriptor((uint16_t)0x2908, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC, 2);
    uint8_t v[2] = {id, 0x01}; // this report id, input
    ref->setValue(v, 2);
    c->setCallbacks(&reportSub);
    return c;
}

static void buildHid()
{
    if (hidBuilt)
        return;
    NimBLEServer *srv = NimBLEDevice::getServer();
    if (!srv)
        return;
    disSvc = srv->createService(NimBLEUUID((uint16_t)0x180A)); // device information
    static const uint8_t pnp[] = {0x02, 0xE5, 0x02, 0x01, 0x00, 0x01, 0x00};
    disSvc->createCharacteristic((uint16_t)0x2A50, NIMBLE_PROPERTY::READ)->setValue(pnp, sizeof(pnp));
    disSvc->createCharacteristic((uint16_t)0x2A29, NIMBLE_PROPERTY::READ)->setValue("Utah BSides badge");

    hidSvc = srv->createService(NimBLEUUID((uint16_t)0x1812)); // HID
    static const uint8_t info[] = {0x11, 0x01, 0x00, 0x02};
    hidSvc->createCharacteristic((uint16_t)0x2A4A, NIMBLE_PROPERTY::READ)->setValue(info, sizeof(info));
    hidSvc->createCharacteristic((uint16_t)0x2A4B, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC)
        ->setValue(REPORT_MAP, sizeof(REPORT_MAP));
    hidSvc->createCharacteristic((uint16_t)0x2A4C, NIMBLE_PROPERTY::WRITE_NR); // HID control point
    uint8_t proto = 1;
    hidSvc->createCharacteristic((uint16_t)0x2A4E, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE_NR)->setValue(&proto, 1);
    repMouse = inputReport(hidSvc, 1);
    repKeys = inputReport(hidSvc, 2);
    repMedia = inputReport(hidSvc, 3);
    hidBuilt = true;
    hidAdded = true; // createService() already registered them with the server
}

static void setHid(bool on)
{
    if (!hidBuilt)
        return;
    NimBLEServer *srv = NimBLEDevice::getServer();
    if (!srv)
        return;
    if (on && !hidAdded) {
        srv->addService(disSvc);
        srv->addService(hidSvc);
        hidAdded = true;
    } else if (!on && hidAdded) {
        hostConn = 0xFFFF;
        srv->removeService(hidSvc, false);
        srv->removeService(disSvc, false);
        hidAdded = false;
    }
    // put the HID appearance in the advert while it's offered, so phones show "mouse"
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->setAppearance(on ? 0x03C0 : 0);
    adv->stop();
    adv->start(0);
}

static void sendMouse(int8_t dx, int8_t dy, uint8_t buttons, int8_t wheel)
{
    if (hostConn == 0xFFFF || !repMouse)
        return;
    uint8_t r[4] = {buttons, (uint8_t)dx, (uint8_t)dy, (uint8_t)wheel};
    repMouse->setValue(r, 4);
    repMouse->notify();
}

static void tapKey(uint8_t usage, uint8_t mod = 0)
{
    if (hostConn == 0xFFFF || !repKeys)
        return;
    uint8_t down[8] = {mod, 0, usage, 0, 0, 0, 0, 0}, up[8] = {0};
    repKeys->setValue(down, 8);
    repKeys->notify();
    delay(12);
    repKeys->setValue(up, 8);
    repKeys->notify();
}

static void tapMedia(uint16_t usage)
{
    if (hostConn == 0xFFFF || !repMedia)
        return;
    uint8_t down[2] = {(uint8_t)usage, (uint8_t)(usage >> 8)}, up[2] = {0, 0};
    repMedia->setValue(down, 2);
    repMedia->notify();
    delay(12);
    repMedia->setValue(up, 2);
    repMedia->notify();
}

// one printable character -> its US-layout HID usage code (+ shift when needed); 0 = can't send
static uint8_t asciiHid(char c, uint8_t &mod)
{
    mod = 0;
    if (c >= 'a' && c <= 'z')
        return 0x04 + (c - 'a');
    if (c >= 'A' && c <= 'Z') {
        mod = 0x02;
        return 0x04 + (c - 'A');
    }
    if (c >= '1' && c <= '9')
        return 0x1E + (c - '1');
    switch (c) {
    case '0': return 0x27;
    case ' ': return 0x2C;
    case '\n': return 0x28;
    case '\t': return 0x2B;
    case '-': return 0x2D;
    case '=': return 0x2E;
    case '[': return 0x2F;
    case ']': return 0x30;
    case '\\': return 0x31;
    case ';': return 0x33;
    case '\'': return 0x34;
    case '`': return 0x35;
    case ',': return 0x36;
    case '.': return 0x37;
    case '/': return 0x38;
    }
    mod = 0x02; // shifted symbols
    switch (c) {
    case '!': return 0x1E;
    case '@': return 0x1F;
    case '#': return 0x20;
    case '$': return 0x21;
    case '%': return 0x22;
    case '^': return 0x23;
    case '&': return 0x24;
    case '*': return 0x25;
    case '(': return 0x26;
    case ')': return 0x27;
    case '_': return 0x2D;
    case '+': return 0x2E;
    case '{': return 0x2F;
    case '}': return 0x30;
    case '|': return 0x31;
    case ':': return 0x33;
    case '"': return 0x34;
    case '~': return 0x35;
    case '<': return 0x36;
    case '>': return 0x37;
    case '?': return 0x38;
    }
    mod = 0;
    return 0; // not on a US keyboard
}

enum Mode { MODE_MOUSE, MODE_CLICKER, MODE_MEDIA, MODE_TEXT, MODE_MACRO, MODE_COUNT };

// saved keystroke macros (NVS "utahmac" / "list"): one per line, "name: body". In the body,
// \n = Enter, \t = Tab, \d = a short pause, \\ = a literal backslash.
static void macrosLoad(char *out, size_t n)
{
    out[0] = 0;
    Preferences p;
    if (p.begin("utahmac", true)) {
        p.getString("list", out, n);
        p.end();
    }
}

void macrosGet(char *out, size_t n) { macrosLoad(out, n); }
void macrosSet(const char *val)
{
    Preferences p;
    if (p.begin("utahmac", false)) {
        p.putString("list", val ? val : "");
        p.end();
    }
}

class BleRemoteApp : public App
{
  public:
    const char *name() const override { return "BT remote"; }
    uint32_t frameMs() const override { return mode == MODE_MOUSE ? 40 : (mode == MODE_TEXT || mode == MODE_MACRO) ? 120 : 400; }
    bool keepAwake() const override { return true; }
    bool typingLights() const override { return mode == MODE_TEXT; }
    void begin() override
    {
        mode = MODE_MOUSE;
        held = 0;
        buttons = 0;
        speed = 6;
        error = NimBLEDevice::getInitialized() ? nullptr : "Turn on Bluetooth first\n(Mesh settings)";
        if (!error) {
            buildHid();
            setHid(true);
        }
    }
    void end() override { setHid(false); }
    void tick(uint32_t now) override
    {
        if (error)
            return;
        if (mode == MODE_TEXT) {
            te.tick(now);
            if (sending)
                sendNextChar();
            return;
        }
        if (mode == MODE_MACRO) {
            if (sending)
                sendNextChar();
            return;
        }
        if (mode != MODE_MOUSE)
            return;
        int8_t dx = 0, dy = 0;
        if (held & (1 << AK_LEFT))
            dx = -speed;
        if (held & (1 << AK_RIGHT))
            dx = speed;
        if (held & (1 << AK_UP))
            dy = -speed;
        if (held & (1 << AK_DOWN))
            dy = speed;
        if (dx || dy)
            sendMouse(dx, dy, buttons, 0);
    }
    void key(int k, KeyAction a) override
    {
        if (mode == MODE_TEXT && !error) {
            textKey(k, a);
            return;
        }
        if (mode == MODE_MACRO && !error) {
            macroKey(k, a);
            return;
        }
        if (k == AK_STAR && a == KA_RELEASE) {
            appClose();
            return;
        }
        if ((k == AK_HASH) && a == KA_PRESS) {
            mode = (Mode)((mode + 1) % MODE_COUNT);
            held = 0;
            buttons = 0;
            if (mode == MODE_TEXT) {
                te.reset("", 200, TextEntry::CASE_SENTENCE);
                sending = false;
                swallowHash = true; // this '#' press switched us in; ignore its release here
                note[0] = 0;
            }
            appRedraw();
            return;
        }
        if (error)
            return;
        bool down = a == KA_PRESS, up = a == KA_RELEASE;
        if (mode == MODE_MOUSE) {
            if (k == AK_LEFT || k == AK_RIGHT || k == AK_UP || k == AK_DOWN) {
                if (down)
                    held |= 1 << k;
                if (up)
                    held &= ~(1 << k);
            } else if (a == KA_PRESS && (k == AK_1 || k == AK_7)) { // left button: tap or hold-drag
                buttons ^= 1;
                sendMouse(0, 0, buttons, 0);
            } else if (a == KA_PRESS && (k == AK_3 || k == AK_9)) { // right click
                sendMouse(0, 0, 2, 0);
                sendMouse(0, 0, buttons, 0);
            } else if ((a == KA_PRESS || a == KA_REPEAT) && k == AK_2) {
                sendMouse(0, 0, buttons, 1); // wheel up
            } else if ((a == KA_PRESS || a == KA_REPEAT) && k == AK_8) {
                sendMouse(0, 0, buttons, -1); // wheel down
            } else if (a == KA_PRESS && k == AK_5) {
                speed = speed >= 12 ? 3 : speed + 3; // pointer speed
                appRedraw();
            }
        } else if (mode == MODE_CLICKER) {
            if (a != KA_PRESS && a != KA_REPEAT)
                return;
            if (k == AK_RIGHT || k == AK_DOWN || k == AK_6 || k == AK_HASH)
                tapKey(K_RIGHT); // next slide
            else if (k == AK_LEFT || k == AK_UP || k == AK_4)
                tapKey(K_LEFT); // previous slide
            else if (k == AK_5)
                tapKey(K_F5); // start the show
            else if (k == AK_0)
                tapKey(K_ESC); // end the show
            else if (k == AK_7)
                tapKey(K_B); // blank the screen
        } else { // media
            if (a != KA_PRESS && a != KA_REPEAT)
                return;
            if (k == AK_5)
                tapMedia(MED_PLAY);
            else if (k == AK_6 || k == AK_RIGHT)
                tapMedia(MED_NEXT);
            else if (k == AK_4 || k == AK_LEFT)
                tapMedia(MED_PREV);
            else if (k == AK_2 || k == AK_UP)
                tapMedia(MED_VUP);
            else if (k == AK_8 || k == AK_DOWN)
                tapMedia(MED_VDN);
            else if (k == AK_0)
                tapMedia(MED_MUTE);
        }
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        static const char *NAMES[] = {"Mouse", "Slide clicker", "Media", "Type text", "Macros"};
        int lh = fontHeight(FONT_SMALL);
        if (!error && mode == MODE_TEXT) {
            drawText(d, x, y);
            return;
        }
        if (!error && mode == MODE_MACRO) {
            drawMacro(d, x, y);
            return;
        }
        char title[32];
        snprintf(title, sizeof(title), "BT remote: %s", NAMES[mode]);
        drawTitle(d, x, y, title, "#mode *done");
        if (error) {
            drawCentered(d, x, y + 2 * lh, error, FONT_SMALL);
            return;
        }
        bool linked = hostConn != 0xFFFF;
        drawCentered(d, x, y + lh + 2, linked ? "Connected" : "Pair \"badge\" on your computer", FONT_SMALL);
        const char *help[4] = {};
        if (mode == MODE_MOUSE) {
            help[0] = "joystick: move pointer";
            help[1] = "1 left click/drag  3 right";
            help[2] = "2/8 scroll  5 speed";
            char sp[20];
            snprintf(sp, sizeof(sp), "speed %d%s", speed, buttons ? "  (dragging)" : "");
            d->setFont(FONT_SMALL);
            d->setTextAlignment(TEXT_ALIGN_CENTER);
            d->drawString(x + d->width() / 2, y + d->height() - lh - 1, sp);
        } else if (mode == MODE_CLICKER) {
            help[0] = "right/down/6: next slide";
            help[1] = "left/up/4: previous";
            help[2] = "5 start show  0 end  7 blank";
        } else {
            help[0] = "5 play/pause";
            help[1] = "4/6 previous/next track";
            help[2] = "2/8 volume  0 mute";
            help[3] = "# -> Type text / Macros";
        }
        d->setFont(FONT_SMALL);
        d->setTextAlignment(TEXT_ALIGN_LEFT);
        for (int i = 0; i < 4 && help[i]; i++)
            d->drawString(x + 2, y + (3 + i) * lh, help[i]);
    }

  private:
    Mode mode = MODE_MOUSE;
    uint32_t held = 0;
    uint8_t buttons = 0;
    int speed = 6;
    const char *error = nullptr;
    TextEntry te;
    char outbuf[201] = "";
    int outPos = 0;
    bool sending = false, swallowHash = false, interpretEsc = false;
    char note[28] = "";
    // macros
    static const int MAXMAC = 16;
    char macRaw[1024] = "";
    const char *macName[MAXMAC];
    const char *macBody[MAXMAC];
    int macN = 0, macSel = 0;

    void enterMacros()
    {
        mode = MODE_MACRO;
        macSel = 0;
        sending = false;
        note[0] = 0;
        parseMacros();
    }
    void parseMacros()
    {
        macrosGet(macRaw, sizeof(macRaw));
        macN = 0;
        char *p = macRaw;
        while (*p && macN < MAXMAC) {
            char *nl = strchr(p, '\n');
            if (nl)
                *nl = 0;
            char *colon = strchr(p, ':');
            if (colon && colon > p) {
                *colon = 0;
                char *body = colon + 1;
                while (*body == ' ')
                    body++;
                macName[macN] = p;
                macBody[macN] = body;
                macN++;
            }
            p = nl ? nl + 1 : p + strlen(p);
        }
    }
    void macroKey(int k, KeyAction a)
    {
        if (sending)
            return;
        if (k == AK_STAR && a == KA_RELEASE) { // on to the action modes
            mode = MODE_MOUSE;
            appRedraw();
            return;
        }
        if (a != KA_PRESS && a != KA_REPEAT)
            return;
        if ((k == AK_UP || k == AK_2) && macSel > 0)
            macSel--;
        else if ((k == AK_DOWN || k == AK_8) && macSel < macN - 1)
            macSel++;
        else if ((k == AK_HASH || k == AK_5) && a == KA_PRESS && macN) {
            if (hostConn == 0xFFFF)
                snprintf(note, sizeof(note), "Pair a computer first");
            else {
                snprintf(outbuf, sizeof(outbuf), "%s", macBody[macSel]);
                outPos = 0;
                sending = true;
                interpretEsc = true;
                snprintf(note, sizeof(note), "Sending...");
            }
        }
        appRedraw();
    }
    void drawMacro(OLEDDisplay *d, int16_t x, int16_t y)
    {
        int lh = fontHeight(FONT_SMALL), H = d->height();
        drawTitle(d, x, y, "BT remote: Macros", hostConn != 0xFFFF ? "linked" : "pair PC");
        if (!macN) {
            drawCentered(d, x, y + 2 * lh, "No macros yet", FONT_SMALL);
            drawCentered(d, x, y + 3 * lh + 2, "Add them on the", FONT_SMALL);
            drawCentered(d, x, y + 4 * lh + 2, "Badge WiFi page", FONT_SMALL);
            drawCentered(d, x, y + H - lh, "* back", FONT_SMALL);
            return;
        }
        d->setFont(FONT_SMALL);
        d->setTextAlignment(TEXT_ALIGN_LEFT);
        int rows = (H - 2 * lh - 2) / lh, top = macSel - rows + 1 > 0 ? macSel - rows + 1 : 0;
        for (int i = 0; i < rows && top + i < macN; i++) {
            int ry = y + lh + 2 + i * lh;
            if (top + i == macSel) {
                d->fillRect(x, ry, d->width(), lh);
                d->setColor(BLACK);
            }
            d->drawString(x + 2, ry, macName[top + i]);
            d->setColor(WHITE);
        }
        drawCentered(d, x, y + H - lh, note[0] ? note : "2/8 pick  # send  * back", FONT_SMALL);
    }

    void textKey(int k, KeyAction a)
    {
        if (swallowHash && k == AK_HASH) { // eat the '#' that brought us into text mode
            if (a == KA_RELEASE)
                swallowHash = false;
            return;
        }
        if (sending) // busy typing: ignore keys until it's done
            return;
        if (a == KA_PRESS && k != AK_HASH)
            note[0] = 0; // a fresh keystroke clears the last "Sent"/error line
        uint32_t now = millis();
        TextEntry::Result r = te.key(k, a, now);
        if (r == TextEntry::TE_DONE) { // '#': type it to the computer
            if (hostConn == 0xFFFF)
                snprintf(note, sizeof(note), "Pair a computer first");
            else if (te.length()) {
                snprintf(outbuf, sizeof(outbuf), "%s", te.text());
                outPos = 0;
                sending = true;
                interpretEsc = false; // typed text is literal
                snprintf(note, sizeof(note), "Typing...");
            }
        } else if (r == TextEntry::TE_CANCEL) { // '*' on empty: on to the macros screen
            enterMacros();
        }
        appRedraw();
    }
    void sendNextChar()
    {
        if (hostConn == 0xFFFF) {
            sending = false;
            snprintf(note, sizeof(note), "Lost the computer");
            appRedraw();
            return;
        }
        char c = outbuf[outPos];
        if (!c) {
            sending = false;
            snprintf(note, sizeof(note), "Sent to computer");
            appRedraw();
            return;
        }
        if (interpretEsc && c == '\\' && outbuf[outPos + 1]) { // a macro escape
            char e = outbuf[outPos + 1];
            outPos += 2;
            if (e == 'd') {
                delay(300); // a pause for the computer to keep up
                return;
            }
            char real = e == 'n' ? '\n' : e == 't' ? '\t' : e == '\\' ? '\\' : 0;
            if (real)
                tapHid(real);
            return;
        }
        tapHid(c);
        outPos++;
    }
    void tapHid(char c)
    {
        uint8_t mod, usage = asciiHid(c, mod);
        if (usage && repKeys) {
            uint8_t down[8] = {mod, 0, usage, 0, 0, 0, 0, 0}, up[8] = {0};
            repKeys->setValue(down, 8);
            repKeys->notify();
            delay(5);
            repKeys->setValue(up, 8);
            repKeys->notify();
        }
    }
    void drawText(OLEDDisplay *d, int16_t x, int16_t y)
    {
        int lh = fontHeight(FONT_SMALL);
        drawTitle(d, x, y, "BT remote: Type text", hostConn != 0xFFFF ? "linked" : "pair PC");
        // the typed text, wrapped over the middle rows
        d->setFont(FONT_SMALL);
        d->setTextAlignment(TEXT_ALIGN_LEFT);
        const char *t = te.text();
        char line[48];
        int row = y + lh + 3, w = d->width() - 4, start = 0, n = (int)strlen(t);
        for (int i = 0; i <= n && row + lh < y + d->height() - 2 * lh; i++) {
            if (i - start >= 44 || i == n) {
                int len = i - start;
                snprintf(line, sizeof(line), "%.*s", len, t + start);
                d->drawString(x + 2, row, line);
                row += lh;
                start = i;
            }
        }
        if (!n)
            drawCentered(d, x, y + lh + 3, "(type a message, then #)", FONT_SMALL);
        // the key's letters strip
        const char *letters = te.pendingLetters();
        if (letters) {
            int ln = (int)strlen(letters), cell = 12, sw = ln * cell, sx = x + (d->width() - sw) / 2,
                sy = y + d->height() - 2 * lh;
            for (int i = 0; i < ln; i++) {
                char s[2] = {te.applyCase(letters[i]), 0};
                if (s[0] == ' ')
                    s[0] = '_';
                if (i == te.pendingIndex()) {
                    d->fillRect(sx + i * cell, sy, cell, lh + 1);
                    d->setColor(BLACK);
                }
                d->setTextAlignment(TEXT_ALIGN_CENTER);
                d->drawString(sx + i * cell + cell / 2, sy, s);
                d->setColor(WHITE);
            }
            d->setTextAlignment(TEXT_ALIGN_LEFT);
        }
        drawCentered(d, x, y + d->height() - lh, note[0] ? note : "#type it  *del  *(empty)exit", FONT_SMALL);
    }
};

static BleRemoteApp &remoteApp()
{
    static BleRemoteApp *p = psramNew<BleRemoteApp>();
    return *p;
}

void openBleRemote()
{
    appOpen(&remoteApp());
}

} // namespace badge

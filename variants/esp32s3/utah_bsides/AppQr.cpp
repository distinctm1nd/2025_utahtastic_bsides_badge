// QR code on the badge screen: show your contact card, a link, or any text for people to scan.
// "Me" is built from your name tag; "Link" and "Text" you set here (or from the Badge WiFi page).
// Stored in NVS ("utahqr").
//
//   joystick left / right (or 4 / 6) switch card, # edit this card's text, * done
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeTextEntry.h"
#include "qrcodegen.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <Arduino.h>
#include <Preferences.h>
#include <stdio.h>
#include <string.h>

namespace badge
{

const char *nametagName(); // AppNameTag.cpp

enum QrItem { Q_ME, Q_LINK, Q_TEXT, Q_COUNT };
static const char *const Q_LABEL[] = {"My contact", "Link", "Text"};
static const char *const Q_KEY[] = {"", "link", "text"};

// load/save the editable payloads (Link, Text)
static void qrLoad(char link[200], char text[200])
{
    link[0] = text[0] = 0;
    Preferences p;
    if (p.begin("utahqr", true)) {
        p.getString("link", link, 200);
        p.getString("text", text, 200);
        p.end();
    }
}
static void qrSave(const char *keyName, const char *val)
{
    Preferences p;
    if (p.begin("utahqr", false)) {
        p.putString(keyName, val ? val : "");
        p.end();
    }
}

// used by the Badge WiFi page so the Link / Text can be set from a phone too
void qrGet(bool link, char *out, size_t n)
{
    char l[200], t[200];
    qrLoad(l, t);
    snprintf(out, n, "%s", link ? l : t);
}
void qrSet(bool link, const char *val)
{
    qrSave(link ? "link" : "text", val);
}

class QrApp : public App
{
  public:
    bool numberShortcuts() const override { return !editing; }
    bool typingLights() const override { return editing; }
    const char *name() const override { return "QR code"; }
    uint32_t frameMs() const override { return editing ? 120 : 1000; }
    bool keepAwake() const override { return true; } // keep it on screen to be scanned
    void begin() override
    {
        item = Q_ME;
        editing = false;
        reload();
    }
    void tick(uint32_t now) override
    {
        if (editing)
            te.tick(now);
    }
    void key(int k, KeyAction a) override
    {
        if (editing) {
            TextEntry::Result r = te.key(k, a, millis());
            if (r == TextEntry::TE_DONE || r == TextEntry::TE_CANCEL) {
                if (r == TextEntry::TE_DONE)
                    qrSave(Q_KEY[item], te.text());
                editing = false;
                reload();
            }
            appRedraw();
            return;
        }
        if (k == AK_STAR && a == KA_RELEASE) {
            appClose();
            return;
        }
        if (a != KA_PRESS && a != KA_REPEAT)
            return;
        if (k == AK_LEFT || k == AK_4)
            item = (item + Q_COUNT - 1) % Q_COUNT, reload();
        else if (k == AK_RIGHT || k == AK_6)
            item = (item + 1) % Q_COUNT, reload();
        else if (a == KA_PRESS && (k == AK_HASH || k == AK_5) && item != Q_ME)
            startEdit();
        appRedraw();
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        int lh = fontHeight(FONT_SMALL), H = d->height(), W = d->width();
        if (editing) {
            drawTitle(d, x, y, item == Q_LINK ? "Link (URL)" : "Text", te.modeName());
            d->setFont(FONT_SMALL);
            d->setTextAlignment(TEXT_ALIGN_LEFT);
            char buf[64];
            snprintf(buf, sizeof(buf), "%s", te.text());
            d->drawString(x + 2, y + lh + 4, buf);
            const char *letters = te.pendingLetters();
            if (letters)
                drawStrip(d, x, y, letters);
            else
                drawCentered(d, x, y + H - lh, "# save   * delete", FONT_SMALL);
            return;
        }
        char title[24];
        snprintf(title, sizeof(title), "%s  %d/%d", Q_LABEL[item], item + 1, Q_COUNT);
        drawTitle(d, x, y, title, "< >");
        if (!payload[0]) {
            drawCentered(d, x, y + 2 * lh + 6, item == Q_ME ? "Set your name first" : "Empty", FONT_MEDIUM);
            drawCentered(d, x, y + H - lh, item == Q_ME ? "(name tag)" : "# to set it", FONT_SMALL);
            return;
        }
        drawQr(d, x, y);
    }

  private:
    int item = Q_ME;
    bool editing = false;
    TextEntry te;
    char payload[220] = "";
    uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(12)];
    bool haveQr = false;

    void reload()
    {
        char link[200], text[200];
        qrLoad(link, text);
        if (item == Q_ME) {
            const char *nm = nametagName();
            if (nm && nm[0])
                snprintf(payload, sizeof(payload), "MECARD:N:%s;;", nm); // scanning adds a contact
            else
                payload[0] = 0;
        } else {
            snprintf(payload, sizeof(payload), "%s", item == Q_LINK ? link : text);
        }
        static uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(12)];
        haveQr = payload[0] &&
                 qrcodegen_encodeText(payload, tmp, qr, qrcodegen_Ecc_LOW, 1, 12, qrcodegen_Mask_AUTO, true);
    }
    void startEdit()
    {
        char link[200], text[200];
        qrLoad(link, text);
        te.reset(item == Q_LINK ? link : text, 199, TextEntry::CASE_LOWER);
        editing = true;
    }
    void drawStrip(OLEDDisplay *d, int16_t x, int16_t y, const char *letters)
    {
        int lh = fontHeight(FONT_SMALL), n = (int)strlen(letters), cell = 12, sw = n * cell;
        int sx = x + (d->width() - sw) / 2, sy = y + d->height() - lh - 1;
        for (int i = 0; i < n; i++) {
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
    void drawQr(OLEDDisplay *d, int16_t x, int16_t y)
    {
        if (!haveQr) {
            drawCentered(d, x, y + d->height() / 2 - 6, "Too long for a QR", FONT_SMALL);
            return;
        }
        int size = qrcodegen_getSize(qr), H = d->height();
        int scale = (H - fontHeight(FONT_SMALL) - 4) / (size + 4);
        if (scale < 1)
            scale = 1;
        int side = (size + 4) * scale, qx = x + (d->width() - side) / 2, qy = y + fontHeight(FONT_SMALL) + 2;
        d->setColor(WHITE);
        d->fillRect(qx, qy, side, side);
        d->setColor(BLACK);
        for (int r = 0; r < size; r++)
            for (int c = 0; c < size; c++)
                if (qrcodegen_getModule(qr, c, r))
                    d->fillRect(qx + (c + 2) * scale, qy + (r + 2) * scale, scale, scale);
        d->setColor(WHITE);
    }
};

static QrApp &qrApp()
{
    static QrApp *p = psramNew<QrApp>();
    return *p;
}

void openQrCode()
{
    appOpen(&qrApp());
}

} // namespace badge

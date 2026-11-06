// The device menu (hold * on a device in the scans or the flagged devices screen): nickname,
// trusted, the watchlist, make it beep, explore (Bluetooth: what it offers, read only). Drawn over the screen it was opened from.
#pragma once
#include "BadgeApp.h"
#include "BadgeBeep.h"
#include "BadgeDevices.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <stdio.h>
#include <string.h>

namespace badge
{

class DeviceMenu
{
  public:
    enum Act { A_NONE, A_NICKNAME, A_EXPLORE };
    bool open = false;
    uint8_t mac[6] = {};
    bool wifi = false, canBeep = false, publicAddr = false;

    void show(const uint8_t m[6], bool isWifi, bool tracker, bool isPublic, const char *name)
    {
        memcpy(mac, m, 6);
        wifi = isWifi;
        canBeep = tracker && !isWifi;
        publicAddr = isPublic;
        snprintf(title, sizeof(title), "%s", name && *name ? name : "Device");
        sel = 0;
        open = true;
        showBeep = false;
    }
    // a key while open; A_NICKNAME: the caller opens the editor
    Act key(int k, KeyAction a)
    {
        if (showBeep) { // * exits any time (even mid-beep); any other key closes it once the beep is done
            if (a == KA_RELEASE && (k == AK_STAR || beepState() != BEEP_BUSY)) {
                showBeep = false;
                beepClear(); // if still busy, the request just finishes in the background
            }
            return A_NONE;
        }
        int n = itemCount();
        if ((a == KA_PRESS || a == KA_REPEAT) && (k == AK_UP || k == AK_DOWN || k == AK_2 || k == AK_8)) {
            sel = (sel + ((k == AK_UP || k == AK_2) ? n - 1 : 1)) % n;
            return A_NONE;
        }
        if (a != KA_RELEASE)
            return A_NONE;
        if (k == AK_STAR) {
            open = false;
            return A_NONE;
        }
        if (k != AK_HASH && k != AK_5)
            return A_NONE;
        const DevMemo *d = devFind(mac);
        uint8_t f = d ? d->flags : 0;
        switch (item(sel)) {
        case I_NICK:
            open = false;
            return A_NICKNAME;
        case I_EXPLORE:
            open = false;
            return A_EXPLORE;
        case I_TRUST:
            devSet(mac, f ^ DEV_TRUSTED, nullptr, wifi);
            break;
        case I_HERE:
            devSet(mac, f ^ DEV_WATCH_HERE, nullptr, wifi);
            break;
        case I_GONE:
            devSet(mac, f ^ DEV_WATCH_GONE, nullptr, wifi);
            break;
        case I_BEEP:
            beepStart(mac, publicAddr);
            showBeep = true;
            break;
        default:
            open = false;
            break;
        }
        return A_NONE;
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y)
    {
        int lh = fontHeight(FONT_SMALL), W = d->width(), H = d->height();
        d->setColor(BLACK);
        d->fillRect(x, y, W, H);
        d->setColor(WHITE);
        if (showBeep) {
            drawTitle(d, x, y, "Make it beep");
            const char *m = beepMessage();
            char line[40];
            int row = 0; // word-wrap the result over a few lines
            while (*m && row < 5) {
                int n = strlen(m), cut = n;
                d->setFont(FONT_SMALL);
                while (cut > 1 && d->getStringWidth(m, cut) > W - 6) {
                    int sp = cut - 1;
                    while (sp > 0 && m[sp] != ' ')
                        sp--;
                    cut = sp > 0 ? sp : cut - 1;
                }
                snprintf(line, sizeof(line), "%.*s", cut, m);
                drawCentered(d, x, y + (2 + row++) * lh, line, FONT_SMALL);
                m += cut;
                while (*m == ' ')
                    m++;
            }
            drawCentered(d, x, y + H - lh - 2, beepState() == BEEP_BUSY ? "* back" : "any key: back", FONT_SMALL);
            return;
        }
        drawTitle(d, x, y, title, "#ok *back");
        const DevMemo *m = devFind(mac);
        uint8_t f = m ? m->flags : 0;
        char b[48];
        int n = itemCount();
        for (int i = 0; i < n; i++) {
            switch (item(i)) {
            case I_NICK:
                snprintf(b, sizeof(b), "Nickname: %s", m && m->nick[0] ? m->nick : "(none)");
                break;
            case I_TRUST:
                snprintf(b, sizeof(b), "Trusted (no alerts): %s", f & DEV_TRUSTED ? "yes" : "no");
                break;
            case I_HERE:
                snprintf(b, sizeof(b), "Alert when it turns up: %s", f & DEV_WATCH_HERE ? "on" : "off");
                break;
            case I_GONE:
                snprintf(b, sizeof(b), "Alert when it's gone: %s", f & DEV_WATCH_GONE ? "on" : "off");
                break;
            case I_BEEP:
                snprintf(b, sizeof(b), "Make it beep");
                break;
            case I_EXPLORE:
                snprintf(b, sizeof(b), "Explore (read only)");
                break;
            default:
                snprintf(b, sizeof(b), "Back");
                break;
            }
            int ry = y + lh + 2 + i * lh;
            if (i == sel) {
                d->fillRect(x, ry, W, lh);
                d->setColor(BLACK);
            }
            d->setFont(FONT_SMALL);
            d->setTextAlignment(TEXT_ALIGN_LEFT);
            d->drawString(x + 2, ry, b);
            d->setColor(WHITE);
        }
    }

  private:
    enum Item { I_NICK, I_TRUST, I_HERE, I_GONE, I_BEEP, I_EXPLORE, I_BACK };
    char title[40] = {};
    int sel = 0;
    bool showBeep = false;
    int itemCount() const { return wifi ? 5 : canBeep ? 7 : 6; }
    Item item(int i) const
    {
        static const Item withBeep[] = {I_NICK, I_TRUST, I_HERE, I_GONE, I_BEEP, I_EXPLORE, I_BACK};
        static const Item bt[] = {I_NICK, I_TRUST, I_HERE, I_GONE, I_EXPLORE, I_BACK};
        static const Item noBt[] = {I_NICK, I_TRUST, I_HERE, I_GONE, I_BACK};
        return wifi ? noBt[i] : canBeep ? withBeep[i] : bt[i];
    }
};

// what a Bluetooth device offers (connects, reads, never writes), then back to where it came from
void openBleExplorer(const uint8_t mac[6], bool publicAddr, const char *name, void (*back)(const uint8_t mac[6], bool wifi));

// the explore engine on its own (used by the Badge WiFi page): start, then poll state / lines
void bleExploreStart(const uint8_t mac[6], bool publicAddr);
int bleExploreState(); // 0 idle, 1 busy, 2 done
const char *bleExploreStatus();
int bleExploreCount();
const char *bleExploreLine(int i);

// the nickname editor, then back to where it came from
void deviceNicknameEdit(const uint8_t mac[6], bool wifi, void (*back)(const uint8_t mac[6], bool wifi));

} // namespace badge

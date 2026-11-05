// "Badges & achievements": badges you've met, achievements, scavenger-hunt flags and high
// scores, one page each. Also the minibadge list.
//   joystick up/left, down/right  scroll      centre (or #)  next page      *  back
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeHunt.h"
#include "BadgeMesh.h"
#include "BadgeMinibadge.h"
#include "BadgeStats.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <Wire.h>
#include <stdio.h>

namespace badge
{

class ListApp : public App
{
  public:
    bool numberShortcuts() const override { return true; }
    uint32_t frameMs() const override { return 1000; }
    void key(int k, KeyAction a) override
    {
        if (k == AK_STAR && a == KA_RELEASE) {
            appClose();
            return;
        }
        if (a != KA_PRESS && a != KA_REPEAT)
            return;
        int pages = pageCount();
        if (k == AK_HASH && a == KA_PRESS)
            page = (page + 1) % pages, scroll = 0;
        else if (k == AK_DOWN || k == AK_RIGHT)
            scroll++;
        else if ((k == AK_UP || k == AK_LEFT) && scroll > 0)
            scroll--;
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        char right[16];
        snprintf(right, sizeof(right), "%d/%d", page + 1, pageCount());
        drawTitle(d, x, y, pageTitle(page), right);
        int lh = fontHeight(FONT_SMALL);
        int rows = (d->height() - lh - 2) / lh;
        int n = lineCount(page);
        if (scroll > n - rows)
            scroll = n - rows > 0 ? n - rows : 0;
        char buf[80];
        for (int i = 0; i < rows && scroll + i < n; i++) {
            line(page, scroll + i, buf, sizeof(buf));
            d->drawString(x + 2, y + lh + 2 + i * lh, buf);
        }
    }

  protected:
    int page = 0, scroll = 0;
    virtual int pageCount() const = 0;
    virtual const char *pageTitle(int p) const = 0;
    virtual int lineCount(int p) const = 0;
    virtual void line(int p, int i, char *b, size_t n) = 0;
};

class BadgesApp : public ListApp
{
  public:
    const char *name() const override { return "Badges"; }
    void begin() override { page = scroll = 0; }

  protected:
    int pageCount() const override { return 4; }
    const char *pageTitle(int p) const override
    {
        static const char *T[] = {"Badges met", "Achievements", "Hunt flags", "High scores"};
        return T[p];
    }
    int lineCount(int p) const override
    {
        switch (p) {
        case 0: return 1 + statsBadgeCount();
        case 1: return 1 + ACH_COUNT * 2;
        case 2: return 1 + huntFlagCount();
        default: return GAME_COUNT;
        }
    }
    void line(int p, int i, char *b, size_t n) override
    {
        if (p == 0) {
            if (i == 0) {
                snprintf(b, n, "%d badges met", statsBadgeCount());
                return;
            }
            char name[40];
            snprintf(b, n, " %s", meshNodeName(statsBadgeAt(i - 1), name, sizeof(name)));
        } else if (p == 1) {
            if (i == 0) {
                snprintf(b, n, "%d of %d unlocked", statsAchievedCount(), (int)ACH_COUNT);
                return;
            }
            int a = (i - 1) / 2;
            if ((i - 1) % 2 == 0)
                snprintf(b, n, "[%c] %s", statsHas(a) ? 'X' : ' ', achievementName(a));
            else
                snprintf(b, n, "     %s", achievementHint(a));
        } else if (p == 2) {
            if (i == 0) {
                snprintf(b, n, "%d of %d found", statsFlagCount(), huntFlagCount());
                return;
            }
            int f = i - 1;
            snprintf(b, n, "[%c] %s", statsHasFlag(f) ? 'X' : ' ', statsHasFlag(f) ? huntFlag(f).name : "???");
        } else {
            static const char *G[] = {"Simon level", "Whack-a-mole", "Snake", "Blocks lines", "Tic-tac-toe wins"};
            snprintf(b, n, "%-16s %u", G[i], statsHigh(i));
        }
    }
};

// a guess at what sits at a given 7-bit I2C address (common parts)
static const char *i2cGuess(uint8_t a)
{
    switch (a) {
    case 0x20: return "keypad (TCA9535)";
    case 0x3C:
    case 0x3D: return "OLED (SSD1306)";
    case 0x29: return "ToF / colour sensor";
    case 0x40: return "PCA / INA / HTU";
    case 0x44:
    case 0x45: return "SHT3x temp/humidity";
    case 0x48: return "ADS1115 / temp";
    case 0x4A:
    case 0x4B: return "ADS1115";
    case 0x53: return "accelerometer";
    case 0x57: return "MAX3010x / EEPROM";
    case 0x5A: return "MLX90614 / MPR121";
    case 0x5C: return "AM2320 / BH1750";
    case 0x68:
    case 0x69: return "RTC / IMU (MPU)";
    case 0x6A:
    case 0x6B: return "IMU (LSM6)";
    case 0x76:
    case 0x77: return "BME/BMP280";
    default: return "";
    }
}

class MinibadgesApp : public ListApp
{
  public:
    const char *name() const override { return "I2C & minibadges"; }
    void begin() override
    {
        page = scroll = 0;
        scanMain();
    }

  protected:
    int pageCount() const override { return 2; }
    const char *pageTitle(int p) const override { return p == 0 ? "Minibadges (GPIO5/6)" : "Main I2C (GPIO1/2)"; }
    int lineCount(int p) const override
    {
        if (p == 0)
            return minibadgeCount() ? 1 + minibadgeCount() * 2 : 3;
        return mainN ? 1 + mainN : 2; // "N found" + each, or a 2-line "none / # rescans"
    }
    void line(int p, int i, char *b, size_t n) override
    {
        if (p == 1) { // the main I2C bus (shared with the keypad)
            if (!mainN) {
                static const char *L[] = {"Nothing found.", "# rescans the bus."};
                snprintf(b, n, "%s", L[i]);
            } else if (i == 0) {
                snprintf(b, n, "%d found  (# rescans)", mainN);
            } else {
                uint8_t a = mainAddr[i - 1];
                const char *g = i2cGuess(a);
                snprintf(b, n, "0x%02X%s%s", a, g[0] ? "  " : "", g);
            }
            return;
        }
        if (!minibadgeCount()) {
            static const char *L[] = {"None talking on I2C.", "Plain LED minibadges", "still blink with CLK."};
            snprintf(b, n, "%s", L[i]);
            return;
        }
        if (i == 0) {
            snprintf(b, n, "%d found", minibadgeCount());
            return;
        }
        const Minibadge &m = minibadgeAt((i - 1) / 2);
        if ((i - 1) % 2 == 0)
            snprintf(b, n, "0x%02X%s  presses %u", m.addr, m.canWrite ? " (rw)" : "", m.presses);
        else
            snprintf(b, n, "  %s", m.text[0] ? m.text : "-");
    }
    // rescan the main bus each time we land on that page
    void key(int k, KeyAction a) override
    {
        int before = page;
        ListApp::key(k, a);
        if (page == 1 && before != 1)
            scanMain();
    }

  private:
    uint8_t mainAddr[32];
    int mainN = 0;
    void scanMain()
    {
        mainN = 0;
        for (uint8_t a = 1; a < 127 && mainN < 32; a++) { // same thread as the keypad reads, so no clash
            Wire.beginTransmission(a);
            if (Wire.endTransmission() == 0)
                mainAddr[mainN++] = a;
        }
    }
};

static BadgesApp badgesApp;
static MinibadgesApp minibadgesApp;

void openBadgesApp()
{
    appOpen(&badgesApp);
}

void openMinibadgesApp()
{
    appOpen(&minibadgesApp);
}

} // namespace badge

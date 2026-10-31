// "Badges & achievements": badges you've met, achievements, scavenger-hunt flags and high
// scores, one page each. Also the minibadge list.
//   joystick left/right  page      up/down  scroll      *  done
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeHunt.h"
#include "BadgeMesh.h"
#include "BadgeMinibadge.h"
#include "BadgeStats.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <stdio.h>

namespace badge
{

class ListApp : public App
{
  public:
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
        if (k == AK_RIGHT)
            page = (page + 1) % pages, scroll = 0;
        else if (k == AK_LEFT)
            page = (page + pages - 1) % pages, scroll = 0;
        else if (k == AK_DOWN)
            scroll++;
        else if (k == AK_UP && scroll > 0)
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

class MinibadgesApp : public ListApp
{
  public:
    const char *name() const override { return "Minibadges"; }
    void begin() override { page = scroll = 0; }

  protected:
    int pageCount() const override { return 1; }
    const char *pageTitle(int) const override { return "Minibadges"; }
    int lineCount(int) const override { return minibadgeCount() ? 1 + minibadgeCount() * 2 : 3; }
    void line(int, int i, char *b, size_t n) override
    {
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

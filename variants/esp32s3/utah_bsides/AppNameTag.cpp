// Name tag and pictures.
//
// Page 1 is a "HELLO my name is" tag with your Meshtastic name (set it in the phone app),
// your short name and a tagline. The pages after it are the pictures from images/ (built
// into the firmware by tools/make_images.py; animated GIFs play).
//
//   joystick         previous / next page      #       slideshow on/off (8 s per page)
//   hold #           edit the tagline          *       done
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeImagesData.h"
#include "BadgeStats.h"

#include "NodeDB.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

namespace badge
{

static bool keepOnSetting()
{
    Preferences p;
    bool on = true;
    if (p.begin("utah", true)) {
        on = p.getBool("tagawake", true);
        p.end();
    }
    return on;
}

class NameTagApp : public App
{
  public:
    const char *name() const override { return "Name tag"; }
    bool keepAwake() const override { return keepOn; }
    uint32_t frameMs() const override
    {
        if (page > 0 && BADGE_IMAGES[page - 1].frames > 1)
            return BADGE_IMAGES[page - 1].frameMs;
        return 1000;
    }
    void begin() override
    {
        keepOn = keepOnSetting();
        openedAt = millis();
        pageAt = openedAt;
        frame = 0;
    }
    void tick(uint32_t now) override
    {
        if (page > 0) {
            const BadgeImage &im = BADGE_IMAGES[page - 1];
            if (im.frames > 1 && now - frameAt >= im.frameMs) {
                frameAt = now;
                frame = (frame + 1) % im.frames;
            }
        }
        if (slideshow && now - pageAt > 8000)
            go(+1);
        if (now - openedAt > 10UL * 60 * 1000)
            statsAchieve(ACH_NAMETAG);
    }
    void key(int k, KeyAction a) override
    {
        if (k == AK_STAR && a == KA_RELEASE) {
            appClose();
        } else if (k == AK_HASH && a == KA_HOLD) {
            editing = true;
        } else if (k == AK_HASH && a == KA_RELEASE) {
            if (editing) {
                editing = false;
                openTaglineEditor();
            } else {
                slideshow = !slideshow;
                pageAt = millis();
            }
        } else if (a == KA_PRESS || a == KA_REPEAT) {
            if (k == AK_RIGHT || k == AK_DOWN)
                go(+1);
            else if (k == AK_LEFT || k == AK_UP)
                go(-1);
        }
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        if (page == 0)
            drawTag(d, x, y);
        else
            drawImage(d, x, y, BADGE_IMAGES[page - 1]);
    }

  private:
    int page = 0;
    int frame = 0;
    bool slideshow = false, editing = false, keepOn = true;
    uint32_t openedAt = 0, pageAt = 0, frameAt = 0;

    void go(int dir)
    {
        int n = BADGE_IMAGE_COUNT + 1;
        page = (page + dir + n) % n;
        frame = 0;
        pageAt = frameAt = millis();
    }

    void drawImage(OLEDDisplay *d, int16_t x, int16_t y, const BadgeImage &im)
    {
        size_t frameBytes = (size_t)((im.w + 7) / 8) * im.h;
        d->drawXbm(x + (d->width() - im.w) / 2, y + (d->height() - im.h) / 2, im.w, im.h, im.bits + frameBytes * frame);
    }

    void drawTag(OLEDDisplay *d, int16_t x, int16_t y)
    {
        int W = d->width(), H = d->height();
        // red-sticker style band at the top
        int band = fontHeight(FONT_MEDIUM) + fontHeight(FONT_SMALL) + 2;
        d->fillRect(x, y, W, band);
        d->setColor(BLACK);
        drawCentered(d, x, y, "HELLO", FONT_MEDIUM);
        drawCentered(d, x, y + fontHeight(FONT_MEDIUM) - 1, "my name is", FONT_SMALL);
        d->setColor(WHITE);
        d->drawRect(x, y, W, H);

        const char *longName = owner.long_name[0] ? owner.long_name : "Badge Owner";
        const uint8_t *fonts[] = {FONT_LARGE, FONT_MEDIUM, FONT_SMALL};
        const uint8_t *f = FONT_SMALL;
        for (const uint8_t *cand : fonts) {
            d->setFont(cand);
            if (d->getStringWidth(longName, (uint16_t)strlen(longName)) <= W - 6) {
                f = cand;
                break;
            }
        }
        int nameY = y + band + 4;
        drawCentered(d, x, nameY, longName, f);

        char line[48];
        snprintf(line, sizeof(line), "[%s]", owner.short_name[0] ? owner.short_name : "??");
        int sy = nameY + fontHeight(f) + 1;
        drawCentered(d, x, sy, line, FONT_SMALL);
        drawWrappedCentered(d, x, sy + fontHeight(FONT_SMALL) + 1, W - 6, H - 2, taglineGet());
        if (slideshow)
            d->fillCircle(x + W - 5, y + H - 5, 2);
    }

    // up to two centred lines, breaking at spaces
    void drawWrappedCentered(OLEDDisplay *d, int16_t x, int y, int w, int bottom, const char *t)
    {
        d->setFont(FONT_SMALL);
        int lh = fontHeight(FONT_SMALL);
        char buf[64];
        strncpy(buf, t, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        char *s = buf;
        for (int line = 0; line < 2 && *s && y + lh <= bottom; line++) {
            int n = (int)strlen(s), cut = n;
            while (cut > 0 && d->getStringWidth(s, (uint16_t)cut) > w) {
                int sp = cut - 1;
                while (sp > 0 && s[sp] != ' ')
                    sp--;
                cut = sp > 0 ? sp : cut - 1;
            }
            char keep = s[cut];
            s[cut] = 0;
            drawCentered(d, x, y, s, FONT_SMALL);
            s[cut] = keep;
            s += cut;
            while (*s == ' ')
                s++;
            y += lh;
        }
    }
};

static NameTagApp nameTag;

void openNameTag()
{
    appOpen(&nameTag);
}

} // namespace badge

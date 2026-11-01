// Name tag and pictures.
//
// Page 1 is your name in big letters (type it on the badge; it starts as your Meshtastic
// name) with an optional tagline under it. The pages after it are the pictures from images/
// (built into the firmware by tools/make_images.py; animated GIFs play).
//
//   joystick         previous / next page      #       slideshow on/off (8 s per page)
//   hold #           edit your name            *       done
//   1                next font                 2       next size (on the name page)
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeFonts.h"
#include "BadgeImageCodec.h"
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

// ---- name font and size (NVS "utah": "tagfont", "tagsize") ----------------------------------
// font 0 = Meshtastic's own (Arial), 1.. = BadgeFonts.h; size 0 = as big as fits, 1 big,
// 2 medium, 3 small
static const char *SIZE_NAMES[] = {"Auto", "Big", "Medium", "Small"};
static int tagFont = -1, tagSize = -1;

static void loadStyle()
{
    if (tagFont >= 0)
        return;
    tagFont = 0;
    tagSize = 0;
    Preferences p;
    if (p.begin("utah", true)) {
        tagFont = p.getInt("tagfont", 1); // Wood type: the badge's Old West theme
        tagSize = p.getInt("tagsize", 0);
        p.end();
    }
    if (tagFont < 0 || tagFont >= nametagFontCount())
        tagFont = 0;
    if (tagSize < 0 || tagSize > 3)
        tagSize = 0;
}
static void saveStyle()
{
    Preferences p;
    if (p.begin("utah", false)) {
        p.putInt("tagfont", tagFont);
        p.putInt("tagsize", tagSize);
        p.end();
    }
}
int nametagFontCount()
{
    return 1 + BADGE_FONT_FAMILIES;
}
const char *nametagFontName(int i)
{
    if (i <= 0 || i >= nametagFontCount())
        return "Classic";
    return BADGE_FONT_NAMES[i - 1];
}
int nametagFont()
{
    loadStyle();
    return tagFont;
}
void nametagSetFont(int i)
{
    loadStyle();
    tagFont = (i >= 0 && i < nametagFontCount()) ? i : 0;
    saveStyle();
}
const char *nametagSizeName(int i)
{
    return SIZE_NAMES[(i >= 0 && i < 4) ? i : 0];
}
int nametagSize()
{
    loadStyle();
    return tagSize;
}
void nametagSetSize(int i)
{
    loadStyle();
    tagSize = (i >= 0 && i < 4) ? i : 0;
    saveStyle();
}
// the font for size 0 big, 1 medium, 2 small in the chosen family
static const uint8_t *styleFont(int which)
{
    if (tagFont <= 0) {
        static const uint8_t *const classic[3] = {FONT_LARGE, FONT_MEDIUM, FONT_SMALL};
        return classic[which];
    }
    return BADGE_FONTS[tagFont - 1][which];
}

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
        page = 0; // always start on the name
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
                openNameEditor();
            } else {
                slideshow = !slideshow;
                pageAt = millis();
            }
        } else if (a == KA_PRESS && page == 0 && appKeyDigit(k) == 1) {
            nametagSetFont((nametagFont() + 1) % nametagFontCount());
            showStyleUntil = millis() + 1500;
            appRedraw();
        } else if (a == KA_PRESS && page == 0 && appKeyDigit(k) == 2) {
            nametagSetSize((nametagSize() + 1) % 4);
            showStyleUntil = millis() + 1500;
            appRedraw();
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
    uint32_t openedAt = 0, pageAt = 0, frameAt = 0, showStyleUntil = 0;

    void go(int dir)
    {
        int n = BADGE_IMAGE_COUNT + 1;
        page = (page + dir + n) % n;
        frame = 0;
        pageAt = frameAt = millis();
    }

    // frames are packed, each relative to the one before (BadgeImageCodec.h): unpack up to the
    // frame wanted, starting again from the first frame when the animation loops
    uint8_t frameBuf[(160 + 7) / 8 * 128];
    const uint8_t *nextSrc = nullptr;
    const BadgeImage *unpacked = nullptr;
    int unpackedFrame = -1;

    void drawImage(OLEDDisplay *d, int16_t x, int16_t y, const BadgeImage &im)
    {
        int n = (im.w + 7) / 8 * im.h;
        if (n > (int)sizeof(frameBuf))
            return;
        if (unpacked != &im || frame < unpackedFrame) {
            memset(frameBuf, 0, sizeof(frameBuf));
            nextSrc = im.bits;
            unpacked = &im;
            unpackedFrame = -1;
        }
        while (unpackedFrame < frame && unpackedFrame + 1 < im.frames) {
            nextSrc = unpackFrame(nextSrc, frameBuf, n);
            unpackedFrame++;
        }
        d->drawXbm(x + (d->width() - im.w) / 2, y + (d->height() - im.h) / 2, im.w, im.h, frameBuf);
    }

    // your name, as big as it fits, in the middle of the screen; the tagline (if any) below
    void drawTag(OLEDDisplay *d, int16_t x, int16_t y)
    {
        int W = d->width(), H = d->height();
        const char *nm = nametagName();
        if (!nm[0])
            nm = "Hold # to add your name";
        const char *tag = taglineGet();
        int tagH = tag[0] ? fontHeight(FONT_SMALL) * 2 + 2 : 0;

        loadStyle();
        // Auto: the biggest that fits on one line; a chosen size: that size
        int firstSize = tagSize == 0 ? 0 : tagSize - 1, lastSize = tagSize == 0 ? 2 : tagSize - 1;
        const uint8_t *f = nullptr;
        for (int s = firstSize; s <= lastSize && !f; s++) {
            d->setFont(styleFont(s));
            if (d->getStringWidth(nm, (uint16_t)strlen(nm)) <= W - 4)
                f = styleFont(s);
        }
        if (f) {
            int ny = y + (H - tagH - fontHeight(f)) / 2;
            drawCentered(d, x, ny, nm, f);
        } else { // too long for one line: two lines, as big as fits
            const uint8_t *two = styleFont(lastSize);
            if (tagSize == 0) // Auto: the biggest whose two lines fit
                for (int s = 0; s <= 2; s++)
                    if (2 * fontHeight(styleFont(s)) <= H - tagH && fitsTwoLines(d, nm, styleFont(s), W - 4)) {
                        two = styleFont(s);
                        break;
                    }
            int lh = fontHeight(two);
            int ny = y + (H - tagH - 2 * lh) / 2;
            drawWrappedCentered(d, x, ny, W - 4, ny + 2 * lh, nm, two);
        }
        if (millis() < showStyleUntil) { // which font/size, after pressing 1 or 2
            char b[40];
            snprintf(b, sizeof(b), "%s, %s", nametagFontName(tagFont), nametagSizeName(tagSize));
            int lh = fontHeight(FONT_SMALL);
            d->setColor(BLACK);
            d->fillRect(x, y, W, lh + 1);
            d->setColor(WHITE);
            drawCentered(d, x, y, b, FONT_SMALL);
        }
        if (tag[0])
            drawWrappedCentered(d, x, y + H - tagH, W - 6, y + H, tag, FONT_SMALL);
        if (slideshow)
            d->fillCircle(x + W - 4, y + H - 4, 2);
    }

    // would the name fit in two lines of this font (breaking at a space)?
    static bool fitsTwoLines(OLEDDisplay *d, const char *t, const uint8_t *font, int w)
    {
        d->setFont(font);
        int n = (int)strlen(t);
        for (int i = 1; i < n; i++)
            if (t[i] == ' ' && d->getStringWidth(t, (uint16_t)i) <= w &&
                d->getStringWidth(t + i + 1, (uint16_t)(n - i - 1)) <= w)
                return true;
        return false;
    }

    // up to two centred lines, breaking at spaces
    void drawWrappedCentered(OLEDDisplay *d, int16_t x, int y, int w, int bottom, const char *t, const uint8_t *font)
    {
        d->setFont(font);
        int lh = fontHeight(font);
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
            drawCentered(d, x, y, s, font);
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

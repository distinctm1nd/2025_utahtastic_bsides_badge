// Name tag and pictures.
//
// Page 1 is your name in big letters (type it on the badge; it starts as your Meshtastic
// name) with an optional tagline under it. The pages after it are the pictures from images/
// (built into the firmware by tools/make_images.py; animated GIFs play), then the pictures
// uploaded from the badge web page (BadgePictures.h).
//
//   joystick         previous / next page      #       slideshow on/off (8 s per page)
//   hold #           edit your name            *       done
//   1                next font                 2       next size (on the name page)
//   0                favorite on/off (on a picture: "Cycle: favorites" on the lock screen)
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeFonts.h"
#include "BadgeImageCodec.h"
#include "BadgeImagesData.h"
#include "BadgeMenu.h"
#include "BadgePictures.h"
#include "BadgePortal.h"
#include "BadgeStats.h"

#include "NodeDB.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

namespace graphics
{
extern bool hasUnreadMessage; // src/graphics/SharedUIDisplay.cpp: drives Meshtastic's mail icon
}

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
// ---- favorite pictures (NVS "utah": "favs", names joined by '|') ---------------------------
static char favs[1600];
static bool favsLoaded = false;

static void loadFavs()
{
    if (favsLoaded)
        return;
    favsLoaded = true;
    favs[0] = 0;
    Preferences p;
    if (p.begin("utah", true)) {
        if (p.isKey("favs"))
            p.getString("favs", favs, sizeof(favs));
        p.end();
    }
}

bool nametagIsFavorite(const char *name)
{
    loadFavs();
    size_t n = strlen(name);
    for (const char *c = favs; n && *c;) {
        const char *e = strchr(c, '|');
        size_t len = e ? (size_t)(e - c) : strlen(c);
        if (len == n && !strncmp(c, name, n))
            return true;
        c += len + (e ? 1 : 0);
    }
    return false;
}

int nametagFavoriteCount()
{
    loadFavs();
    int n = 0;
    for (int i = 1; i < nametagChoiceCount(); i++)
        n += nametagIsFavorite(nametagChoice(i));
    return n;
}

// false when there's no room left for another one
bool nametagSetFavorite(const char *name, bool on)
{
    loadFavs();
    if (on == nametagIsFavorite(name))
        return true;
    static char out[sizeof(favs)];
    out[0] = 0;
    size_t used = 0;
    for (const char *c = favs; *c;) { // keep the others, and only those still on the badge
        const char *e = strchr(c, '|');
        size_t len = e ? (size_t)(e - c) : strlen(c);
        char one[40];
        snprintf(one, sizeof(one), "%.*s", (int)len, c);
        bool exists = false;
        for (int i = 1; i < nametagChoiceCount() && !exists; i++)
            exists = !strcmp(nametagChoice(i), one);
        if (exists && strcmp(one, name) && used + len + 2 < sizeof(out))
            used += snprintf(out + used, sizeof(out) - used, "%s%s", used ? "|" : "", one);
        c += len + (e ? 1 : 0);
    }
    if (on) {
        if (used + strlen(name) + 2 >= sizeof(out))
            return false;
        snprintf(out + used, sizeof(out) - used, "%s%s", used ? "|" : "", name);
    }
    snprintf(favs, sizeof(favs), "%s", out);
    Preferences p;
    if (p.begin("utah", false)) {
        p.putString("favs", favs);
        p.end();
    }
    return true;
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

// long names: false = two lines, true = one line scrolling across
static int tagScroll = -1;
bool nametagScroll()
{
    if (tagScroll < 0) {
        Preferences p;
        tagScroll = 0;
        if (p.begin("utah", true)) {
            tagScroll = p.getBool("tagscroll", false);
            p.end();
        }
    }
    return tagScroll;
}
void nametagSetScroll(bool on)
{
    tagScroll = on;
    Preferences p;
    if (p.begin("utah", false)) {
        p.putBool("tagscroll", on);
        p.end();
    }
}

const uint8_t *nametagStyleFont(int which)
{
    loadStyle();
    return styleFont(which < 0 || which > 2 ? 0 : which);
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
    bool numberShortcuts() const override { return true; }
    const char *name() const override { return "Name tag"; }
    bool reclaimScreen() const override { return false; } // heavy animation; don't re-assert in a loop
    bool keepAwake() const override { return keepOn; }
    uint32_t frameMs() const override
    {
        const BadgeImage *im = pageImage();
        if (im && im->frames > 1)
            return im->frameMs;
        return scrollingName ? 40 : 1000;
    }
    void startAt(int p) { startPage = p; }
    // the boot intro: play page p once through, then the welcome tips
    void intro(int p)
    {
        startPage = p;
        const BadgeImage &im = BADGE_IMAGES[p - 1];
        introMs = (uint32_t)im.frames * im.frameMs;
    }
    void begin() override
    {
        keepOn = keepOnSetting();
        page = 0; // start on the name, unless the startup setting picked a picture
        upl = -1;
        if (startPage > 0 && startPage < pageCount())
            page = startPage;
        startPage = 0;
        introUntil = introMs ? millis() + introMs : 0;
        introMs = 0;
        loadPage();
        openedAt = millis();
        pageAt = openedAt;
        frame = 0;
    }
    void tick(uint32_t now) override
    {
        if (introUntil && (int32_t)(now - introUntil) >= 0) { // the boot intro has played once
            endIntro();
            return;
        }
        if (const BadgeImage *im = pageImage()) {
            if (im->frames > 1 && now - frameAt >= im->frameMs) {
                frameAt = now;
                frame = (frame + 1) % im->frames;
            }
        }
        if ((slideshow || viewCycle) && now - pageAt > 8000)
            go(+1);
        if (now - openedAt > 10UL * 60 * 1000)
            statsAchieve(ACH_NAMETAG);
    }
    void key(int k, KeyAction a) override
    {
        if (introUntil) { // any key skips the boot intro
            if (a == KA_PRESS)
                endIntro();
            return;
        }
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
        } else if (a == KA_PRESS && page > 0 && appKeyDigit(k) == 0) {
            const char *nm = nametagChoice(page);
            bool on = !nametagIsFavorite(nm);
            favNote = nametagSetFavorite(nm, on) ? (on ? "* Favorite" : "Not a favorite") : "Too many favorites";
            favNoteUntil = millis() + 1500;
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
        if (const BadgeImage *im = pageImage()) {
            drawImage(d, x, y, *im);
            if (millis() < favNoteUntil) {
                int lh = fontHeight(FONT_SMALL);
                d->setColor(BLACK);
                d->fillRect(x, y, d->width(), lh + 1);
                d->setColor(WHITE);
                drawCentered(d, x, y, favNote, FONT_SMALL);
            }
        } else {
            drawTag(d, x, y);
            drawStatus(d, x, y);
        }
        if (portalRunning())
            drawWifiChip(d, x, y);
    }
    void end() override
    {
        picUnload();
        upl = -1;
        unpacked = nullptr;
    }

    // the lock screen's view: cycle every page (or only the pictures) instead of staying put
    void setCycle(bool cycle, bool noName, bool favsOnly = false)
    {
        viewCycle = cycle;
        viewNoName = noName;
        viewFavs = favsOnly && nametagFavoriteCount() > 0; // none marked yet: all the pictures
    }
    void drawName(OLEDDisplay *d, int16_t x, int16_t y) { drawTag(d, x, y); }

  private:
    bool viewCycle = false, viewNoName = false, viewFavs = false;
    const char *favNote = "";
    uint32_t favNoteUntil = 0;
    bool scrollingName = false;
    int page = 0, startPage = 0;
    uint32_t introMs = 0, introUntil = 0;

    void endIntro()
    {
        introUntil = 0;
        appClose(); // BadgeStartup watches for this close, then brings up the lock/attract screen
    }
    int frame = 0;
    BadgeImage uploaded = {};
    int upl = -1; // which uploaded picture is in `uploaded`

    static int pageCount() { return 1 + BADGE_IMAGE_COUNT + picCount(); }
    // the picture on this page (built in or uploaded), nullptr for the name page
    const BadgeImage *pageImage() const
    {
        if (page <= 0)
            return nullptr;
        if (page <= BADGE_IMAGE_COUNT)
            return &BADGE_IMAGES[page - 1];
        return upl == page - 1 - BADGE_IMAGE_COUNT ? &uploaded : nullptr;
    }
    void loadPage()
    {
        int i = page - 1 - BADGE_IMAGE_COUNT;
        if (i >= 0 && upl != i) {
            unpacked = nullptr; // the old buffer is about to go
            upl = picLoad(i, uploaded) ? i : -1;
        }
    }
    bool slideshow = false, editing = false, keepOn = true;
    uint32_t openedAt = 0, pageAt = 0, frameAt = 0, showStyleUntil = 0;

    void go(int dir)
    {
        int n = pageCount();
        page = (page + dir + n) % n;
        if (page == 0 && viewNoName && n > 1) // cycling pictures only
            page = (page + dir + n) % n;
        for (int i = 0; viewFavs && i < n && !(page > 0 && nametagIsFavorite(nametagChoice(page))); i++)
            page = (page + dir + n) % n;
        frame = 0;
        loadPage();
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
            nextSrc = unpackFrame(nextSrc, frameBuf, n, picDataEnd(im));
            unpackedFrame++;
        }
        d->drawXbm(x + (d->width() - im.w) / 2, y + (d->height() - im.h) / 2, im.w, im.h, frameBuf);
    }

    // corners of the name page: nodes heard recently, and an envelope while a message is unread
    void drawStatus(OLEDDisplay *d, int16_t x, int16_t y)
    {
        char b[16];
        snprintf(b, sizeof(b), "%u near", nodeDB ? (unsigned)nodeDB->getNumOnlineMeshNodes(true) : 0u);
        d->setFont(FONT_SMALL);
        d->setTextAlignment(TEXT_ALIGN_LEFT);
        d->drawString(x + 2, y, b);
        if (graphics::hasUnreadMessage) {
            int ex = x + d->width() - 15, ey = y + 3;
            d->drawRect(ex, ey, 12, 8);
            d->drawLine(ex, ey, ex + 6, ey + 4);
            d->drawLine(ex + 11, ey, ex + 5, ey + 4);
        }
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
        scrollingName = !f && nametagScroll();
        if (f) {
            int ny = y + (H - tagH - fontHeight(f)) / 2;
            drawCentered(d, x, ny, nm, f);
        } else if (scrollingName) { // too long for one line: it scrolls across, at the chosen size
            f = styleFont(tagSize == 0 ? 0 : tagSize - 1);
            d->setFont(f);
            int tw = d->getStringWidth(nm, (uint16_t)strlen(nm)), gap = W / 2, loop = tw + gap;
            uint32_t t = millis() - pageAt;
            int off = t < 1500 ? 0 : (int)(((t - 1500) / 25) % (uint32_t)loop); // a moment to start reading
            int ny = y + (H - tagH - fontHeight(f)) / 2;
            d->setTextAlignment(TEXT_ALIGN_LEFT);
            d->drawString(x + 2 - off, ny, nm);
            d->drawString(x + 2 - off + loop, ny, nm);
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
            if (t[i] == ' ' && d->getStringWidth(t, (uint16_t)i) <= w && d->getStringWidth(t + i + 1, (uint16_t)(n - i - 1)) <= w)
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

static NameTagApp &nameTag()
{
    static NameTagApp *p = psramNew<NameTagApp>();
    return *p;
}

void openNameTag()
{
    nameTag().setCycle(false, false);
    appOpen(&nameTag());
}

// the lock screen shows a name tag page: "@tag" (or "") the name, else a picture's name
static int pageOf(const char *what)
{
    for (int i = 0; i < BADGE_IMAGE_COUNT; i++)
        if (!strcmp(BADGE_IMAGES[i].name, what))
            return 1 + i;
    for (int i = 0; i < picCount(); i++)
        if (!strcmp(picName(i), what))
            return 1 + BADGE_IMAGE_COUNT + i;
    return 0;
}

App *nametagView(const char *what)
{
    bool all = what && !strcmp(what, "@cycle"), favs = what && !strcmp(what, "@cyclefavs");
    bool pics = favs || (what && !strcmp(what, "@cyclepics"));
    bool cycle = all || pics;
    bool noName = pics || (all && !nametagNameSet()); // only cycle the name once it's been changed
    int first = noName ? 1 : 0;
    for (int i = 1; favs && i < nametagChoiceCount(); i++)
        if (nametagIsFavorite(nametagChoice(i))) {
            first = i;
            break;
        }
    nameTag().startAt(cycle ? first : (what ? pageOf(what) : 0));
    nameTag().setCycle(cycle, noName, favs);
    return &nameTag();
}

// ---- font picker: your name drawn in each font, so you see what you get ---------------------
class FontPickApp : public App
{
  public:
    bool numberShortcuts() const override { return true; }
    const char *name() const override { return "Name font"; }
    void begin() override { choice = nametagFont(); }
    void key(int k, KeyAction a) override
    {
        if (a == KA_PRESS || a == KA_REPEAT) {
            int n = nametagFontCount();
            if (k == AK_RIGHT || k == AK_DOWN)
                choice = (choice + 1) % n;
            else if (k == AK_LEFT || k == AK_UP)
                choice = (choice + n - 1) % n;
        }
        if (a != KA_RELEASE)
            return;
        if (k == AK_HASH) {
            nametagSetFont(choice);
            appClose();
            menuShowSettings();
        } else if (k == AK_STAR) {
            appClose();
            menuShowSettings();
        }
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        int keep = tagFont;
        tagFont = choice; // draw the name tag as it would look in this font
        nameTag().drawName(d, x, y);
        tagFont = keep;
        char b[48];
        snprintf(b, sizeof(b), "< %s >  %d/%d", nametagFontName(choice), choice + 1, nametagFontCount());
        int lh = fontHeight(FONT_SMALL), W = d->width();
        d->setColor(BLACK);
        d->fillRect(x, y, W, lh + 1);
        d->fillRect(x, y + d->height() - lh - 1, W, lh + 1);
        d->setColor(WHITE);
        drawCentered(d, x, y, b, FONT_SMALL);
        drawCentered(d, x, y + d->height() - lh - 1, "#: use it   *: back", FONT_SMALL);
    }

  private:
    int choice = 0;
};

static FontPickApp fontPick;

void openFontPicker()
{
    loadStyle();
    appOpen(&fontPick);
}

int nametagChoiceCount()
{
    return 1 + BADGE_IMAGE_COUNT + picCount();
}

const char *nametagChoice(int i)
{
    if (i <= 0)
        return "@tag";
    if (i <= BADGE_IMAGE_COUNT)
        return BADGE_IMAGES[i - 1].name;
    return picName(i - 1 - BADGE_IMAGE_COUNT);
}

bool nametagShowStartup()
{
    const char *what = picStartup();
    if (!what[0]) { // no startup picture chosen: play the animated raven once
        for (int i = 0; i < BADGE_IMAGE_COUNT; i++)
            if (!strcmp(BADGE_IMAGES[i].name, "99_utahtastic") && BADGE_IMAGES[i].frames > 1) {
                nameTag().intro(1 + i);
                appOpen(&nameTag());
                return true;
            }
        return false;
    }
    int p = 0; // "@tag": the name
    for (int i = 0; i < BADGE_IMAGE_COUNT && !p; i++)
        if (!strcmp(BADGE_IMAGES[i].name, what))
            p = 1 + i;
    for (int i = 0; i < picCount() && !p; i++)
        if (!strcmp(picName(i), what))
            p = 1 + BADGE_IMAGE_COUNT + i;
    if (!p && strcmp(what, "@tag"))
        return false; // that picture is gone
    nameTag().startAt(p);
    appOpen(&nameTag());
    return true;
}

} // namespace badge

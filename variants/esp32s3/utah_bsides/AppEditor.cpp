// The badge's text editor: new messages (handed to Meshtastic to send) and the name tag's
// name tag's name and tagline. Shows the letters of the key you're tapping in a strip at the bottom, the case
// mode (abc / Abc / ABC) and a character count.
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeKeypad.h"
#include "BadgeMesh.h"
#include "BadgeMeshSettings.h"
#include "BadgeStats.h"
#include "BadgeTextEntry.h"

#include "NodeDB.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

namespace badge
{

class EditorApp : public App
{
  public:
    enum Purpose { ED_MESSAGE, ED_TAGLINE, ED_NAME, ED_CUSTOM };

    // replies go to one person or channel instead of the "Send messages to" channel
    bool replying = false;
    uint32_t replyDest = 0;
    uint8_t replyCh = 0;
    char replyName[24] = {};

    void setup(Purpose p, int firstKey)
    {
        purpose = p;
        replying = false;
        if (p == ED_MESSAGE)
            te.reset("", 200, TextEntry::CASE_SENTENCE);
        else if (p == ED_NAME)
            te.reset(nametagName(), 30, TextEntry::CASE_SENTENCE);
        else if (p == ED_TAGLINE)
            te.reset(taglineGet(), 60, TextEntry::CASE_SENTENCE);
        first = firstKey;
    }
    void setupCustom(const char *title, const char *initial, int maxLen, int caseMode, void (*done)(const char *))
    {
        purpose = ED_CUSTOM;
        customTitle = title;
        customLimit = maxLen > TextEntry::MAX ? TextEntry::MAX : maxLen;
        customDone = done;
        te.reset(initial ? initial : "", customLimit, (TextEntry::CaseMode)caseMode);
        first = -1;
    }
    const char *name() const override
    {
        return purpose == ED_MESSAGE  ? "New message"
               : purpose == ED_NAME   ? "Name"
               : purpose == ED_CUSTOM ? customTitle
                                      : "Tagline";
    }
    int limit() const { return purpose == ED_MESSAGE ? 200 : purpose == ED_NAME ? 30 : purpose == ED_CUSTOM ? customLimit : 60; }
    void begin() override
    {
        if (first >= 0) { // the key that opened the editor is the first letter
            uint32_t now = millis();
            te.key(first, KA_PRESS, now);
            te.key(first, KA_RELEASE, now);
            first = -1;
        }
    }
    uint32_t frameMs() const override { return 120; }
    bool typingLights() const override { return true; }
    bool holdsKeys() const override { return true; } // a text editor: keep keys off Meshtastic
    void tick(uint32_t now) override
    {
        bool before = te.pendingLetters() != nullptr;
        te.tick(now);
        if (before && !te.pendingLetters())
            appRedraw();
        // blink the cursor
        if (now - blinkAt > 500) {
            blinkAt = now;
            blink = !blink;
            appRedraw();
        }
    }
    void key(int k, KeyAction a) override
    {
        TextEntry::Result r = te.key(k, a, millis());
        blink = true;
        if (r == TextEntry::TE_CANCEL) {
            appClose();
            if (purpose == ED_CUSTOM) {
                if (customDone)
                    customDone(nullptr);
            } else if (purpose != ED_MESSAGE)
                openNameTag();
        } else if (r == TextEntry::TE_DONE) {
            finish();
        }
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        char right[16];
        snprintf(right, sizeof(right), "%s %d", te.modeName(), limit() - te.length());
        char title[32];
        if (purpose == ED_MESSAGE)
            snprintf(title, sizeof(title), "To %s", replying ? replyName : meshSendChannelName());
        else
            snprintf(title, sizeof(title), "%s",
                     purpose == ED_NAME     ? "Your name"
                     : purpose == ED_CUSTOM ? customTitle
                                            : "Tagline");
        drawTitle(d, x, y, title, right);
        int lh = fontHeight(FONT_SMALL);
        int top = y + lh + 3;
        int bottom = y + d->height() - lh - 2; // strip/footer row below this
        drawWrapped(d, x + 2, top, d->width() - 4, bottom - top, lh);

        const char *letters = te.pendingLetters();
        if (letters) {
            // letter strip: the letters on this key, the current one boxed
            int n = (int)strlen(letters);
            int cell = 12, w = n * cell;
            int sx = x + (d->width() - w) / 2, sy = y + d->height() - lh - 1;
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
        } else {
            drawFooter(d, x, y, purpose == ED_MESSAGE ? "#send *del hold:*#" : "#ok *del hold:*#");
        }
    }

  private:
    TextEntry te;
    Purpose purpose = ED_MESSAGE;
    int first = -1;
    const char *customTitle = "";
    int customLimit = 60;
    void (*customDone)(const char *) = nullptr;
    uint32_t blinkAt = 0;
    bool blink = true;

    void finish()
    {
        if (purpose == ED_CUSTOM) {
            static char out[TextEntry::MAX + 1];
            strncpy(out, te.text(), sizeof(out));
            out[sizeof(out) - 1] = 0;
            void (*done)(const char *) = customDone;
            appClose();
            if (done)
                done(out);
            return;
        }
        if (purpose != ED_MESSAGE) {
            if (purpose == ED_NAME)
                nametagSetName(te.text());
            else
                taglineSet(te.text());
            appClose();
            openNameTag(); // back to the name tag to see it
            return;
        }
        if (te.length() == 0) {
            appClose();
            return;
        }
        static char msg[TextEntry::MAX + 1];
        strncpy(msg, te.text(), sizeof(msg));
        msg[sizeof(msg) - 1] = 0;
        appClose();
        if (replying)
            meshSendTextDM(replyDest, msg, replyCh);
        else
            meshSendTextDefault(msg); // to the channel picked in Mesh settings; no on-screen keyboard
        statsMessageSent();
    }

    // word-wrapped text with a blinking cursor
    void drawWrapped(OLEDDisplay *d, int x, int y, int w, int h, int lh)
    {
        const char *t = te.text();
        int len = te.length(), cur = te.cursor();
        int maxLines = h / lh;
        // break into lines
        int starts[24], nl = 0, i = 0;
        starts[nl++] = 0;
        int lineStart = 0, lastSpace = -1;
        while (i < len && nl < 24) {
            if (t[i] == ' ')
                lastSpace = i;
            int width = d->getStringWidth(t + lineStart, (uint16_t)(i - lineStart + 1));
            if (width > w && i > lineStart) {
                lineStart = (lastSpace > lineStart) ? lastSpace + 1 : i;
                starts[nl++] = lineStart;
                lastSpace = -1;
                i = lineStart;
                continue;
            }
            i++;
        }
        // which line holds the cursor; scroll so it is visible
        int curLine = 0;
        for (int l = 0; l < nl; l++)
            if (cur >= starts[l])
                curLine = l;
        int firstLine = curLine >= maxLines ? curLine - maxLines + 1 : 0;
        for (int l = firstLine; l < nl && l < firstLine + maxLines; l++) {
            int s = starts[l], e = (l + 1 < nl) ? starts[l + 1] : len;
            char line[TextEntry::MAX + 1];
            int n = e - s;
            memcpy(line, t + s, (size_t)n);
            line[n] = 0;
            int ly = y + (l - firstLine) * lh;
            d->drawString(x, ly, line);
            if (l == curLine && blink) {
                int cx = x + d->getStringWidth(t + s, (uint16_t)(cur - s));
                d->drawVerticalLine(cx, ly + 1, lh - 1);
            }
        }
    }
};

static EditorApp editor;

void openCompose(int firstKey)
{
    openMessagesCompose(firstKey); // written under the conversation, like a phone
}

void openReply(uint32_t dest, uint8_t channel, const char *name)
{
    editor.setup(EditorApp::ED_MESSAGE, -1);
    editor.replying = true;
    editor.replyDest = dest;
    editor.replyCh = channel;
    snprintf(editor.replyName, sizeof(editor.replyName), "%s", name ? name : "?");
    appOpen(&editor);
}

void openNameEditor()
{
    editor.setup(EditorApp::ED_NAME, -1);
    appOpen(&editor);
}

void openTextEditor(const char *title, const char *initial, int maxLen, int caseMode, void (*done)(const char *text))
{
    editor.setupCustom(title, initial, maxLen, caseMode, done);
    appOpen(&editor);
}

void openTaglineEditor()
{
    editor.setup(EditorApp::ED_TAGLINE, -1);
    appOpen(&editor);
}

// ---- tagline storage ------------------------------------------------------------------------
static char tagline[64];
static bool taglineLoaded = false;

const char *taglineGet()
{
    if (!taglineLoaded) {
        taglineLoaded = true;
        tagline[0] = 0; // optional, empty unless the owner adds one
        Preferences p;
        if (p.begin("utah", true)) {
            if (p.isKey("tagline"))
                strncpy(tagline, p.getString("tagline", "").c_str(), sizeof(tagline) - 1);
            p.end();
        }
    }
    return tagline;
}

void taglineSet(const char *text)
{
    strncpy(tagline, text ? text : "", sizeof(tagline) - 1);
    tagline[sizeof(tagline) - 1] = 0;
    taglineLoaded = true;
    Preferences p;
    if (p.begin("utah", false)) {
        p.putString("tagline", tagline);
        p.end();
    }
}

} // namespace badge

namespace badge
{
// ---- name shown on the name tag (empty = the Meshtastic long name) ------------------------
static char tagName[32];
static bool tagNameLoaded = false;

const char *nametagName()
{
    if (!tagNameLoaded) {
        tagNameLoaded = true;
        tagName[0] = 0;
        Preferences p;
        if (p.begin("utah", true)) {
            if (p.isKey("tagname"))
                strncpy(tagName, p.getString("tagname", "").c_str(), sizeof(tagName) - 1);
            p.end();
        }
    }
    // not typed in yet: follow the Meshtastic name (it may change in the phone app)
    return tagName[0] ? tagName : owner.long_name;
}

// has the wearer given the badge its own name? (vs. the untouched Meshtastic default)
bool nametagNameSet()
{
    nametagName(); // make sure tagName is loaded
    return tagName[0] != 0;
}

void nametagSetName(const char *text)
{
    strncpy(tagName, text ? text : "", sizeof(tagName) - 1);
    tagName[sizeof(tagName) - 1] = 0;
    tagNameLoaded = true;
    Preferences p;
    if (p.begin("utah", false)) {
        p.putString("tagname", tagName);
        p.end();
    }
}
} // namespace badge

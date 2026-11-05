// A small search/filter box shared by the list screens (LoRa nodes, WiFi / Bluetooth, flagged
// devices, sightings). Open it with a key, type a few letters (multi-tap, like the editor), and
// the list shows only rows that contain the text. Case-insensitive substring match, live as you
// type. '#' keeps the text, '*' deletes (empty '*' clears and closes).
#pragma once
#include "BadgeApp.h"
#include "BadgeTextEntry.h"

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include <strings.h>

namespace badge
{

class ListFilter
{
  public:
    bool editing() const { return edit; }
    bool active() const { return edit || query[0]; } // a filter is in effect or being typed
    const char *current() const { return edit ? te.text() : query; }

    void open()
    {
        te.reset(query, 31, TextEntry::CASE_LOWER);
        edit = true;
    }
    void clear()
    {
        query[0] = 0;
        edit = false;
    }
    void tick(uint32_t now)
    {
        if (edit)
            te.tick(now);
    }
    // feed a key while editing; returns true if it was consumed (the app should not act on it)
    bool key(int k, KeyAction a, uint32_t now)
    {
        if (!edit)
            return false;
        TextEntry::Result r = te.key(k, a, now);
        if (r == TextEntry::TE_DONE)
            snprintf(query, sizeof(query), "%s", te.text()), edit = false; // keep what was typed
        else if (r == TextEntry::TE_CANCEL)
            query[0] = 0, edit = false; // '*' on empty: clear and close
        return true;
    }
    // does this text pass the filter? (empty filter passes everything)
    bool match(const char *s) const
    {
        const char *q = current();
        return !q[0] || (s && strcasestr(s, q) != nullptr);
    }

    // the bottom row: the query with a cursor while editing, plus the multi-tap letter strip
    void draw(OLEDDisplay *d, int16_t x, int16_t y)
    {
        int lh = fontHeight(FONT_SMALL), W = d->width(), H = d->height();
        int by = y + H - lh - 1;
        d->setColor(BLACK);
        d->fillRect(x, by - 1, W, lh + 2);
        d->setColor(WHITE);
        d->drawHorizontalLine(x, by - 1, W);
        d->setFont(FONT_SMALL);
        d->setTextAlignment(TEXT_ALIGN_LEFT);
        char b[40];
        snprintf(b, sizeof(b), "find: %s", edit ? te.text() : query);
        d->drawString(x + 2, by, b);
        if (edit) {
            int cx = x + 2 + d->getStringWidth(b);
            d->drawVerticalLine(cx + 1, by + 1, lh - 2);
            const char *letters = te.pendingLetters();
            if (letters) {
                int n = (int)strlen(letters), cell = 12, sw = n * cell, sx = x + (W - sw) / 2, sy = by - lh - 1;
                d->setColor(BLACK);
                d->fillRect(sx, sy, sw, lh + 1);
                d->setColor(WHITE);
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
        }
    }

  private:
    TextEntry te;
    bool edit = false;
    char query[32] = "";
};

} // namespace badge

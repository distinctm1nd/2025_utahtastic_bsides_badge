// Messages, like a phone: a list of chats (each channel, each person you've messaged), and each
// chat on its own: oldest at the top, newest at the bottom, with the message you're writing in a
// box under them. Writing only ever goes to the chat that's open. It takes the place of
// Meshtastic's own Messages page (which mixes everything, newest first).
//   chats:    up/down pick, # or centre open, "+ New chat" to message a person or join/create
//             a channel, * or left/right leave
//   a chat:   up/down scroll, a digit or # starts writing, * back to the chats, left/right leave;
//             in a group, hold * (the menu key, as everywhere on the badge) for its menu: invite
//             a badge (a private group's name and key go to their badge end to end encrypted;
//             they press # to join), a QR code for the Meshtastic phone app, change the key and
//             re-invite (to remove someone), leave the group
//   writing:  multi-tap as in the badge editor; # sends (and you stay in the chat), * deletes,
//             * with nothing typed stops writing
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeKeypad.h"
#include "BadgeLock.h"
#include "BadgeMeshSettings.h"
#include "BadgeStats.h"
#include "BadgeBase64.h"
#include "BadgeTextEntry.h"
#include "qrcodegen.h"
#include "BadgeMenu.h"
#include "BadgeMesh.h"
#include "BadgeTime.h"

#include "MessageStore.h"
#include "mesh/generated/meshtastic/apponly.pb.h"
#include "mesh/mesh-pb-constants.h"
#include "NodeDB.h"
#include "gps/RTC.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "mesh/Channels.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>
#include <string>
#include <vector>

extern graphics::Screen *screen;
namespace graphics
{
extern bool hasUnreadMessage;
} // namespace graphics

// Which of Meshtastic's pages is showing, read from the Screen's private members without
// changing Meshtastic: explicit template instantiations may name private members (the same
// standard C++ rule as the 915 MHz scanner's radio access).
#include <OLEDDisplayUi.h>
namespace
{
struct ScreenUi {
    friend auto stealUi(ScreenUi);
};
struct ScreenFrames {
    friend auto stealFrames(ScreenFrames);
};
struct ScreenNormal {
    friend auto stealNormal(ScreenNormal);
};
template <auto M> struct StealUi {
    friend auto stealUi(ScreenUi) { return M; }
};
template <auto M> struct StealFrames {
    friend auto stealFrames(ScreenFrames) { return M; }
};
template <auto M> struct StealNormal {
    friend auto stealNormal(ScreenNormal) { return M; }
};
template struct StealUi<&graphics::Screen::ui>;
template struct StealFrames<&graphics::Screen::framesetInfo>;
template struct StealNormal<&graphics::Screen::showingNormalScreen>;

struct PageInfo {
    bool normal = false; // Meshtastic's pages are up (not a badge screen or other alert)
    bool settled = false; // not mid page-turn
    uint8_t current = 255, messages = 255, count = 0;
};
PageInfo pageInfo()
{
    PageInfo p;
    if (!screen)
        return p;
    OLEDDisplayUi *ui = screen->*stealUi(ScreenUi{});
    if (!ui)
        return p;
    OLEDDisplayUiState *st = ui->getUiState();
    const auto &fs = screen->*stealFrames(ScreenFrames{});
    p.normal = screen->*stealNormal(ScreenNormal{});
    p.settled = st->frameState == FIXED;
    p.current = st->currentFrame;
    p.messages = fs.positions.textMessage;
    p.count = fs.frameCount;
    return p;
}
} // namespace

namespace badge
{

struct Chat {
    bool dm;
    uint8_t ch;    // channel (for a DM: the channel it came on)
    uint32_t peer; // the other person (DM)
    bool same(const Chat &o) const { return dm == o.dm && (dm ? peer == o.peer : ch == o.ch); }
};

struct Line {
    std::string text;
    int msg; // index into the message list
    bool header;
};

static bool mine(const StoredMessage &m)
{
    return nodeDB && m.sender == nodeDB->getNodeNum();
}

static bool inChat(const StoredMessage &m, const Chat &c)
{
    bool dm = m.dest != NODENUM_BROADCAST;
    if (c.dm)
        return dm && (m.sender == c.peer || m.dest == c.peer);
    return !dm && m.channelIndex == c.ch;
}

static void chatName(const Chat &c, char *b, size_t n)
{
    if (c.dm)
        meshNodeName(c.peer, b, n);
    else
        snprintf(b, n, "#%s", channels.getName(c.ch));
}

// How private a chat is, from its keys:
//   a channel: its own random key = private group; the well-known default key = public (anyone
//   can read it); no key = not encrypted at all
//   a person: their public key known = private (only they can read it); not yet = Meshtastic
//   won't send the DM at all ("DM failed") until their node info (with the key) arrives
enum Privacy { P_PRIVATE, P_PUBLIC, P_NONE, P_NOKEY };
static Privacy chatPrivacy(const Chat &c)
{
    if (c.dm) {
        const meshtastic_NodeInfoLite *n = nodeDB ? nodeDB->getMeshNode(c.peer) : nullptr;
        return n && n->has_user && n->user.public_key.size == 32 ? P_PRIVATE : P_NOKEY;
    }
    const auto &psk = channels.getByIndex(c.ch).settings.psk;
    if (psk.size == 0 || (psk.size == 1 && psk.bytes[0] == 0))
        return P_NONE;
    if (psk.size == 1 || (psk.size == 16 && !memcmp(psk.bytes, defaultpsk, 15)))
        return P_PUBLIC; // the default key (or one of its numbered variants)
    return P_PRIVATE;
}
static const char *privacyLabel(const Chat &c) // for the title bar
{
    switch (chatPrivacy(c)) {
    case P_PRIVATE: return c.dm ? "private" : "private group";
    case P_PUBLIC: return "public group";
    case P_NONE: return "NO encryption";
    default: return "no key: can't DM";
    }
}

// small icons: a group (two people) or one person, and a lock (open when not private)
static void drawPeopleIcon(OLEDDisplay *d, int x, int y, bool group)
{
    if (group) {
        d->fillCircle(x + 2, y + 2, 2);
        d->fillCircle(x + 7, y + 2, 2);
        d->fillRect(x, y + 6, 5, 4);
        d->fillRect(x + 5, y + 6, 5, 4);
    } else {
        d->fillCircle(x + 4, y + 2, 2);
        d->fillRect(x + 2, y + 6, 5, 4);
    }
}
static void drawLockIcon(OLEDDisplay *d, int x, int y, bool locked)
{
    d->fillRect(x, y + 5, 7, 5);
    if (locked) {
        d->drawRect(x + 1, y + 1, 5, 5);
    } else { // shackle swung up and open
        d->drawRect(x + 3, y, 5, 4);
        d->drawVerticalLine(x + 3, y + 3, 2);
    }
}

static uint32_t ageSecs(const StoredMessage &m, bool &known)
{
    known = true;
    if (m.isBootRelative)
        return millis() / 1000 >= m.timestamp ? millis() / 1000 - m.timestamp : 0;
    if (timeKnown() && getValidTime(RTCQualityFromNet) >= m.timestamp)
        return getValidTime(RTCQualityFromNet) - m.timestamp;
    known = false;
    return 0;
}

static void ago(char *b, size_t n, const StoredMessage &m)
{
    bool known;
    uint32_t secs = ageSecs(m, known);
    if (!known)
        snprintf(b, n, "?");
    else if (secs < 60)
        snprintf(b, n, "now");
    else if (secs < 3600)
        snprintf(b, n, "%um", (unsigned)(secs / 60));
    else if (secs < 86400)
        snprintf(b, n, "%uh", (unsigned)(secs / 3600));
    else
        snprintf(b, n, "%ud", (unsigned)(secs / 86400));
}

// who this badge invited to each group (NVS "iv_" + group name), for "change key and re-invite"
static const int MAX_INVITED = 16;
static int loadInvited(const char *group, uint32_t *out)
{
    char key[16];
    snprintf(key, sizeof(key), "iv_%.11s", group);
    Preferences p;
    int n = 0;
    if (p.begin("utah", true)) {
        n = (int)(p.getBytes(key, out, sizeof(uint32_t) * MAX_INVITED) / sizeof(uint32_t));
        p.end();
    }
    return n;
}
static void saveInvited(const char *group, const uint32_t *nodes, int n)
{
    char key[16];
    snprintf(key, sizeof(key), "iv_%.11s", group);
    Preferences p;
    if (p.begin("utah", false)) {
        if (n > 0)
            p.putBytes(key, nodes, sizeof(uint32_t) * (n < MAX_INVITED ? n : MAX_INVITED));
        else
            p.remove(key);
        p.end();
    }
}
static void rememberInvited(const char *group, uint32_t node)
{
    uint32_t list[MAX_INVITED];
    int n = loadInvited(group, list);
    for (int i = 0; i < n; i++)
        if (list[i] == node)
            return;
    if (n == MAX_INVITED) { // full: drop the oldest
        memmove(list, list + 1, sizeof(uint32_t) * (MAX_INVITED - 1));
        n--;
    }
    list[n++] = node;
    saveInvited(group, list, n);
}

// what you've read of each chat (in RAM: after a restart nothing counts as new)
struct Seen {
    Chat chat;
    int count;
};
static Seen seen[32];
static int nSeen = 0;
static int *seenFor(const Chat &c, int countNow)
{
    for (int i = 0; i < nSeen; i++)
        if (seen[i].chat.same(c))
            return &seen[i].count;
    if (nSeen < 32) {
        seen[nSeen] = {c, countNow}; // first time it's listed: what's there now counts as read
        return &seen[nSeen++].count;
    }
    return nullptr;
}

class MessagesApp : public App
{
  public:
    bool numberShortcuts() const override { return !composing; } // not while writing
    const char *name() const override { return "Messages"; }
    uint32_t frameMs() const override { return composing ? 120 : 5000; } // cursor blink / "5m ago"
    bool typingLights() const override { return composing; }
    void begin() override
    {
        mode = M_LIST;
        composing = false;
        listSel = 0;
        noteUntil = 0;
        graphics::hasUnreadMessage = false;
        if (hasStart) { // opened straight into a chat (e.g. one just joined)
            hasStart = false;
            openChat(startChat);
            say(startNote);
        }
    }
    bool writing() const { return composing; }
    // open into this chat, with a short note at the bottom
    void startIn(const Chat &c, const char *noteText)
    {
        startChat = c;
        startNote = noteText;
        hasStart = true;
    }
    void tick(uint32_t now) override
    {
        if (!composing)
            return;
        bool before = te.pendingLetters() != nullptr;
        te.tick(now);
        if (before && !te.pendingLetters())
            appRedraw();
        if (now - blinkAt > 500) {
            blinkAt = now;
            blink = !blink;
            appRedraw();
        }
    }
    void key(int k, KeyAction a) override
    {
        if (mode == M_CHAT && composing) {
            blink = true;
            TextEntry::Result r = te.key(k, a, millis());
            if (r == TextEntry::TE_DONE)
                send();
            else if (r == TextEntry::TE_CANCEL)
                composing = false;
            return;
        }
        bool turn = a == KA_PRESS || a == KA_REPEAT;
        if (k == AK_STAR && a == KA_RELEASE && ignoreStarRelease) { // the end of the hold that opened a menu
            ignoreStarRelease = false;
            return;
        }
        if (a == KA_PRESS && (k == AK_LEFT || k == AK_RIGHT)) {
            leave(k == AK_LEFT ? -1 : +1);
            return;
        }
        if (mode == M_CHAT) {
            if (a == KA_HOLD && k == AK_STAR && !chat.dm) { // hold *: this group's menu
                ignoreStarRelease = true;
                mode = M_SHARE;
                shareSel = 0;
                return;
            }
            if (a == KA_PRESS && appKeyDigit(k) >= 0) { // a digit: start writing, with that letter
                startCompose();
                te.key(k, a, millis());
            } else if (turn && k == AK_UP) {
                fromBottom++; // clamped when drawn
            } else if (turn && k == AK_DOWN && fromBottom > 0) {
                fromBottom--;
            } else if (a == KA_RELEASE && k == AK_HASH) {
                startCompose();
            } else if (a == KA_RELEASE && k == AK_STAR) {
                mode = M_LIST; // back to the chats
            }
            return;
        }
        // the lists: chats, new chat, people
        if (mode == M_QR) { // any key: back to the chat
            if (a == KA_RELEASE)
                mode = M_CHAT;
            return;
        }
        if (mode == M_LEAVE) { // # leaves, * keeps it
            if (a == KA_RELEASE && k == AK_HASH) {
                static char b[48];
                char g[16];
                snprintf(g, sizeof(g), "%s", channels.getName(chat.ch));
                const char *why = meshLeaveChannel(chat.ch);
                if (why) {
                    mode = M_CHAT;
                    say(why);
                } else {
                    saveInvited(g, nullptr, 0);
                    snprintf(b, sizeof(b), "Left #%s", g);
                    chat = {false, 0, 0};
                    mode = M_LIST;
                    listSel = 0;
                    say(b);
                }
            } else if (a == KA_RELEASE && k == AK_STAR) {
                mode = M_CHAT;
            }
            return;
        }
        if (mode == M_REKEY) { // up/down move, # ticks/unticks (or runs the last rows), * cancels
            int rows = nMembers + 2;
            if ((a == KA_PRESS || a == KA_REPEAT) && (k == AK_UP || k == AK_DOWN))
                memberSel = (memberSel + (k == AK_UP ? rows - 1 : 1)) % rows;
            else if (a == KA_RELEASE && k == AK_STAR)
                mode = M_CHAT;
            else if (a == KA_RELEASE && k == AK_HASH) {
                if (memberSel < nMembers)
                    ticked[memberSel] = !ticked[memberSel];
                else if (memberSel == nMembers)
                    rekeyAndSend();
                else
                    mode = M_CHAT;
            }
            return;
        }
        int n = mode == M_LIST ? nChats + 1 : mode == M_NEW ? 3 : mode == M_SHARE ? 5 : nPeople;
        int &sel = mode == M_LIST ? listSel : mode == M_NEW ? newSel : mode == M_SHARE ? shareSel : peopleSel;
        if (turn && n > 0 && (k == AK_UP || k == AK_DOWN)) {
            sel = (sel + (k == AK_UP ? n - 1 : 1)) % n;
            return;
        }
        if (a != KA_RELEASE)
            return;
        if (k == AK_STAR) {
            if (mode == M_LIST)
                leave(-1);
            else
                mode = mode == M_PEOPLE ? M_NEW : (mode == M_INVITE || mode == M_SHARE) ? M_CHAT : M_LIST;
            return;
        }
        if (k != AK_HASH)
            return;
        if (mode == M_LIST) {
            if (listSel == 0) {
                mode = M_NEW;
                newSel = 0;
            } else if (listSel - 1 < nChats) {
                openChat(chats[listSel - 1]);
            }
        } else if (mode == M_NEW) {
            if (newSel == 0) {
                loadPeople();
                mode = M_PEOPLE;
            } else if (newSel == 1) {
                appClose(); // the channel setup has its own screens
                meshJoinChannel();
            } else {
                mode = M_LIST;
            }
        } else if (mode == M_SHARE) {
            Privacy pv = chatPrivacy(chat);
            if (shareSel == 0) { // invite a badge
                if (pv == P_PRIVATE) {
                    loadPeople();
                    mode = M_INVITE;
                } else {
                    mode = M_CHAT;
                    say(pv == P_PUBLIC ? "Public group: anyone can join" : "No key: anyone can read it");
                }
            } else if (shareSel == 1) { // QR code for a phone
                mode = makeQr() ? M_QR : M_CHAT;
                if (mode == M_CHAT)
                    say("Couldn't make the QR code");
            } else if (shareSel == 2) { // change key and re-invite
                if (channels.getByIndex((ChannelIndex)chat.ch).role == meshtastic_Channel_Role_PRIMARY) {
                    mode = M_CHAT;
                    say("The main channel: change it in the app");
                } else {
                    loadMembers();
                    mode = M_REKEY;
                }
            } else if (shareSel == 3) { // leave
                mode = M_LEAVE;
            } else {
                mode = M_CHAT;
            }
        } else if (mode == M_PEOPLE && peopleSel < nPeople) {
            openChat(Chat{true, 0, people[peopleSel]});
        } else if (mode == M_INVITE && peopleSel < nPeople) {
            uint32_t who = people[peopleSel];
            static char b[64];
            char nm[32];
            meshNodeName(who, nm, sizeof(nm));
            if (chatPrivacy(Chat{true, 0, who}) != P_PRIVATE) {
                meshRequestNodeInfo(who, chat.ch);
                snprintf(b, sizeof(b), "No key for %s yet: asked, try in a minute", nm);
            } else if (meshSendInvite(who, chat.ch)) {
                rememberInvited(channels.getName(chat.ch), who);
                snprintf(b, sizeof(b), "Invite sent to %s", nm);
            } else {
                snprintf(b, sizeof(b), "Couldn't send the invite");
            }
            mode = M_CHAT;
            say(b);
        }
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        if (mode == M_CHAT)
            drawChat(d, x, y);
        else if (mode == M_LIST)
            drawList(d, x, y);
        else if (mode == M_NEW)
            drawMenu(d, x, y, "New chat", NEW_ITEMS, 3, newSel);
        else if (mode == M_SHARE) {
            static char t[40];
            char cn[28];
            chatName(chat, cn, sizeof(cn));
            snprintf(t, sizeof(t), "Group %s", cn);
            drawMenu(d, x, y, t, SHARE_ITEMS, 5, shareSel);
        } else if (mode == M_LEAVE)
            drawLeave(d, x, y);
        else if (mode == M_REKEY)
            drawRekey(d, x, y);
        else if (mode == M_QR)
            drawQr(d, x, y);
        else
            drawPeople(d, x, y);
        if ((int32_t)(millis() - noteUntil) < 0 && note) { // a short note along the bottom
            int lh = fontHeight(FONT_SMALL), by = y + d->height() - lh - 2;
            d->setColor(BLACK);
            d->fillRect(x, by - 2, d->width(), lh + 4);
            d->setColor(WHITE);
            d->drawHorizontalLine(x, by - 2, d->width());
            drawCentered(d, x, by, note, FONT_SMALL);
        }
    }

  private:
    enum Mode { M_LIST, M_CHAT, M_NEW, M_PEOPLE, M_INVITE, M_SHARE, M_QR, M_LEAVE, M_REKEY };
    void drawLeave(OLEDDisplay *d, int16_t x, int16_t y)
    {
        char cn[28], b[48];
        chatName(chat, cn, sizeof(cn));
        drawTitle(d, x, y, "Leave group?");
        int lh = fontHeight(FONT_SMALL);
        snprintf(b, sizeof(b), "Leave %s?", cn);
        drawCentered(d, x, y + 2 * lh, b, FONT_SMALL);
        drawCentered(d, x, y + 3 * lh + 4, "Its messages stop coming.", FONT_SMALL);
        drawCentered(d, x, y + 4 * lh + 4, "To come back you need", FONT_SMALL);
        drawCentered(d, x, y + 5 * lh + 4, "an invite or the key.", FONT_SMALL);
        drawCentered(d, x, y + d->height() - lh - 2, "#: leave   *: keep it", FONT_SMALL);
    }
    void drawRekey(OLEDDisplay *d, int16_t x, int16_t y)
    {
        drawTitle(d, x, y, "Who keeps it?", "#tick");
        rowIcon = -1;
        drawRows(d, x, y, nMembers + 2, memberSel, [](MessagesApp *me, int i, char *l, size_t ln, char *r, size_t rn) {
            r[0] = 0;
            if (i == me->nMembers) {
                int k = 0;
                for (int j = 0; j < me->nMembers; j++)
                    k += me->ticked[j];
                snprintf(l, ln, "> New key, send to %d", k);
                return;
            }
            if (i == me->nMembers + 1) {
                snprintf(l, ln, "Cancel");
                return;
            }
            char nm[32];
            meshNodeName(me->members[i], nm, sizeof(nm));
            snprintf(l, ln, "[%c] %s", me->ticked[i] ? 'x' : ' ', nm);
            me->rowIcon = chatPrivacy(Chat{true, 0, me->members[i]}) == P_PRIVATE ? 2 : 0;
            if (me->rowIcon == 0)
                snprintf(r, rn, "no key");
        });
    }
    static constexpr const char *SHARE_ITEMS[5] = {"Invite a badge", "QR code for a phone", "Change key and re-invite",
                                                   "Leave this group", "Back"};
    // change key and re-invite: the group's people, ticked to get the new key
    uint32_t members[MAX_INVITED * 2];
    bool ticked[MAX_INVITED * 2];
    int nMembers = 0, memberSel = 0;
    void loadMembers()
    {
        char g[16];
        snprintf(g, sizeof(g), "%s", channels.getName(chat.ch));
        nMembers = loadInvited(g, members);
        const auto &msgs = messageStore.getMessages(); // and whoever has posted in it
        for (int i = (int)msgs.size() - 1; i >= 0 && nMembers < MAX_INVITED * 2; i--) {
            const StoredMessage &m = msgs[i];
            if (!inChat(m, chat) || mine(m))
                continue;
            bool have = false;
            for (int j = 0; j < nMembers && !have; j++)
                have = members[j] == m.sender;
            if (!have)
                members[nMembers++] = m.sender;
        }
        for (int j = 0; j < nMembers; j++)
            ticked[j] = true;
        memberSel = 0;
    }
    void rekeyAndSend()
    {
        static char b[64];
        const char *why = meshRekeyChannel(chat.ch);
        if (why) {
            mode = M_CHAT;
            say(why);
            return;
        }
        char g[16];
        snprintf(g, sizeof(g), "%s", channels.getName(chat.ch));
        uint32_t kept[MAX_INVITED];
        int sent = 0, nokey = 0, nk = 0;
        for (int j = 0; j < nMembers; j++) {
            if (!ticked[j])
                continue;
            if (chatPrivacy(Chat{true, 0, members[j]}) != P_PRIVATE) {
                nokey++;
                meshRequestNodeInfo(members[j], chat.ch);
                continue;
            }
            if (meshSendInvite(members[j], chat.ch))
                sent++;
            if (nk < MAX_INVITED)
                kept[nk++] = members[j];
        }
        saveInvited(g, kept, nk); // the removed ones are forgotten
        if (nokey)
            snprintf(b, sizeof(b), "New key: sent to %d, %d without a key", sent, nokey);
        else
            snprintf(b, sizeof(b), "New key: sent to %d. Phones: show the QR", sent);
        mode = M_CHAT;
        say(b);
    }
    int shareSel = 0;
    // the channel's Meshtastic link as a QR code (what the phone app scans to add a channel)
    uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(12)];
    bool makeQr()
    {
        static meshtastic_ChannelSet cs;
        memset(&cs, 0, sizeof(cs));
        cs.settings_count = 1;
        cs.settings[0] = channels.getByIndex(chat.ch).settings;
        cs.has_lora_config = true; // the phone's radio needs the same region and preset
        cs.lora_config = config.lora;
        uint8_t pb[160];
        size_t n = pb_encode_to_bytes(pb, sizeof(pb), &meshtastic_ChannelSet_msg, &cs);
        if (!n || n > 150)
            return false;
        static char b64[208], url[256];
        base64Encode(pb, (int)n, b64);
        char *o = b64; // base64url, no padding: how the apps write it
        for (char *c = b64; *c; c++)
            if (*c != '=')
                *o++ = *c == '+' ? '-' : *c == '/' ? '_' : *c;
        *o = 0;
        snprintf(url, sizeof(url), "https://meshtastic.org/e/?add=true#%s", b64);
        static uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(12)];
        return qrcodegen_encodeText(url, tmp, qr, qrcodegen_Ecc_LOW, 1, 12, qrcodegen_Mask_AUTO, true);
    }
    void drawQr(OLEDDisplay *d, int16_t x, int16_t y)
    {
        int size = qrcodegen_getSize(qr), H = d->height(), W = d->width();
        int scale = (H - 4) / (size + 4); // with a light border of 2 modules
        if (scale < 1)
            scale = 1;
        int side = (size + 4) * scale, qx = x + 2, qy = y + (H - side) / 2;
        d->setColor(WHITE); // QR codes are dark on light
        d->fillRect(qx, qy, side, side);
        d->setColor(BLACK);
        for (int r = 0; r < size; r++)
            for (int c = 0; c < size; c++)
                if (qrcodegen_getModule(qr, c, r))
                    d->fillRect(qx + (c + 2) * scale, qy + (r + 2) * scale, scale, scale);
        d->setColor(WHITE);
        int tx = qx + side + 3, lh = fontHeight(FONT_SMALL);
        char cn[28];
        chatName(chat, cn, sizeof(cn));
        static const char *const lines[] = {"Scan in", "Meshtastic", "app: adds", "", "", "Only to", "people you", "trust"};
        d->setFont(FONT_SMALL);
        d->setTextAlignment(TEXT_ALIGN_LEFT);
        for (int i = 0; i < 8; i++)
            if (i == 3)
                d->drawString(tx, y + 2 + i * lh, cn);
            else if (lines[i][0])
                d->drawString(tx, y + 2 + i * lh, lines[i]);
        (void)W;
    }
    bool ignoreStarRelease = false, hasStart = false;
    Chat startChat = {false, 0, 0};
    const char *startNote = nullptr, *note = nullptr;
    uint32_t noteUntil = 0;
    void say(const char *t) // Meshtastic's pop-ups are paused while a badge screen is up
    {
        note = t;
        noteUntil = t ? millis() + 4000 : 0;
        appRedraw();
    }
    Mode mode = M_LIST;
    static constexpr const char *NEW_ITEMS[3] = {"Message a person", "Join or create a channel", "Back"};

    // the chat list
    Chat chats[32];
    int lastMsg[32], countIn[32];
    int nChats = 0, listSel = 0, newSel = 0;
    // people to start a chat with
    uint32_t people[48];
    int nPeople = 0, peopleSel = 0;
    // the open chat
    Chat chat = {false, 0, 0};
    std::vector<Line> lines;
    int fromBottom = 0;
    // writing
    TextEntry te;
    bool composing = false, blink = true;
    uint32_t blinkAt = 0;

    void openChat(const Chat &c)
    {
        if (c.dm && chatPrivacy(c) == P_NOKEY)
            meshRequestNodeInfo(c.peer, c.ch); // their key comes with it; DMs need it
        chat = c;
        mode = M_CHAT;
        fromBottom = 0;
        composing = false;
    }

    // every channel, then the people you've had DMs with; the most recently active first
    void loadChats()
    {
        nChats = 0;
        const auto &msgs = messageStore.getMessages();
        for (int i = 0; i < 8 && nChats < 32; i++)
            if (channels.getByIndex((ChannelIndex)i).role != meshtastic_Channel_Role_DISABLED)
                chats[nChats++] = {false, (uint8_t)i, 0};
        for (int i = (int)msgs.size() - 1; i >= 0 && nChats < 32; i--) {
            const StoredMessage &m = msgs[i];
            if (m.dest == NODENUM_BROADCAST)
                continue;
            Chat c = {true, m.channelIndex, mine(m) ? m.dest : m.sender};
            bool have = false;
            for (int j = 0; j < nChats && !have; j++)
                have = chats[j].same(c);
            if (!have)
                chats[nChats++] = c;
        }
        if (chat.dm) { // a new DM chat with no messages yet stays listed while it's open
            bool have = false;
            for (int j = 0; j < nChats && !have; j++)
                have = chats[j].same(chat);
            if (!have && nChats < 32)
                chats[nChats++] = chat;
        }
        for (int j = 0; j < nChats; j++) {
            lastMsg[j] = -1;
            countIn[j] = 0;
        }
        for (int i = 0; i < (int)msgs.size(); i++)
            for (int j = 0; j < nChats; j++)
                if (inChat(msgs[i], chats[j])) {
                    lastMsg[j] = i;
                    countIn[j]++;
                }
        for (int a = 1; a < nChats; a++) // newest activity first (insertion sort, small)
            for (int b = a; b > 0 && lastMsg[b] > lastMsg[b - 1]; b--) {
                Chat t = chats[b];
                chats[b] = chats[b - 1];
                chats[b - 1] = t;
                int l = lastMsg[b], c = countIn[b];
                lastMsg[b] = lastMsg[b - 1], countIn[b] = countIn[b - 1];
                lastMsg[b - 1] = l, countIn[b - 1] = c;
            }
    }

    void loadPeople()
    {
        nPeople = 0;
        peopleSel = 0;
        if (!nodeDB)
            return;
        for (size_t i = 0; i < nodeDB->getNumMeshNodes() && nPeople < 48; i++) {
            const meshtastic_NodeInfoLite *nd = nodeDB->getMeshNodeByIndex(i);
            if (nd && nd->num != nodeDB->getNodeNum() && nd->has_user)
                people[nPeople++] = nd->num;
        }
        for (int a = 1; a < nPeople; a++) // most recently heard first
            for (int b = a; b > 0; b--) {
                const meshtastic_NodeInfoLite *x = nodeDB->getMeshNode(people[b]), *y = nodeDB->getMeshNode(people[b - 1]);
                if (!x || !y || x->last_heard <= y->last_heard)
                    break;
                uint32_t t = people[b];
                people[b] = people[b - 1];
                people[b - 1] = t;
            }
    }

    // a highlighted-row list
    // icon for a row: -1 none, else bit 0 = group, bit 1 = locked
    int rowIcon = -1;
    void drawRows(OLEDDisplay *d, int16_t x, int16_t y, int n, int sel, void (*row)(MessagesApp *, int, char *, size_t, char *, size_t))
    {
        int lh = fontHeight(FONT_SMALL), top = y + lh + 2, W = d->width();
        int rows = (d->height() - lh - 2) / lh;
        int first = sel - rows + 1 > 0 ? sel - rows + 1 : 0;
        char left[64], right[24];
        d->setFont(FONT_SMALL);
        for (int i = 0; i < rows && first + i < n; i++) {
            rowIcon = -1;
            row(this, first + i, left, sizeof(left), right, sizeof(right));
            int tx = rowIcon >= 0 ? x + 23 : x + 2;
            int ly = top + i * lh, rw = right[0] ? d->getStringWidth(right) : 0;
            int len = strlen(left);
            while (len > 1 && d->getStringWidth(left, len) > W - rw - (tx - x) - 6)
                left[--len] = 0;
            if (first + i == sel) {
                d->fillRect(x, ly, W, lh);
                d->setColor(BLACK);
            }
            if (rowIcon >= 0) {
                int iy = ly + (lh - 10) / 2;
                drawPeopleIcon(d, x + 2, iy, rowIcon & 1);
                drawLockIcon(d, x + 13, iy, rowIcon & 2);
            }
            d->setTextAlignment(TEXT_ALIGN_LEFT);
            d->drawString(tx, ly, left);
            if (right[0]) {
                d->setTextAlignment(TEXT_ALIGN_RIGHT);
                d->drawString(x + W - 2, ly, right);
                d->setTextAlignment(TEXT_ALIGN_LEFT);
            }
            d->setColor(WHITE);
        }
    }

    void drawList(OLEDDisplay *d, int16_t x, int16_t y)
    {
        loadChats();
        if (listSel > nChats)
            listSel = nChats;
        drawTitle(d, x, y, "Chats", "#open *back");
        drawRows(d, x, y, nChats + 1, listSel, [](MessagesApp *me, int i, char *l, size_t ln, char *r, size_t rn) {
            r[0] = 0;
            if (i == 0) {
                snprintf(l, ln, "+ New chat");
                return;
            }
            int j = i - 1;
            chatName(me->chats[j], l, ln);
            me->rowIcon = (me->chats[j].dm ? 0 : 1) | (chatPrivacy(me->chats[j]) == P_PRIVATE ? 2 : 0);
            int *s = seenFor(me->chats[j], me->countIn[j]);
            int unread = s ? me->countIn[j] - *s : 0;
            if (unread > 0)
                snprintf(r, rn, "%d new", unread);
            else if (me->lastMsg[j] >= 0)
                ago(r, rn, messageStore.getMessages()[me->lastMsg[j]]);
        });
    }

    void drawMenu(OLEDDisplay *d, int16_t x, int16_t y, const char *title, const char *const *items, int n, int sel)
    {
        drawTitle(d, x, y, title, "#ok *back");
        static const char *const *its;
        its = items;
        drawRows(d, x, y, n, sel, [](MessagesApp *, int i, char *l, size_t ln, char *r, size_t) {
            r[0] = 0;
            snprintf(l, ln, "%s", its[i]);
        });
    }

    void drawPeople(OLEDDisplay *d, int16_t x, int16_t y)
    {
        static char inv[40];
        if (mode == M_INVITE) {
            char cn[28];
            chatName(chat, cn, sizeof(cn));
            snprintf(inv, sizeof(inv), "Invite to %s", cn);
        }
        drawTitle(d, x, y, mode == M_INVITE ? inv : "Message a person", "#ok *back");
        if (!nPeople) {
            drawCentered(d, x, y + 3 * fontHeight(FONT_SMALL), "No nodes heard yet", FONT_SMALL);
            return;
        }
        drawRows(d, x, y, nPeople, peopleSel, [](MessagesApp *me, int i, char *l, size_t ln, char *r, size_t rn) {
            meshNodeName(me->people[i], l, ln);
            r[0] = 0;
            me->rowIcon = chatPrivacy(Chat{true, 0, me->people[i]}) == P_PRIVATE ? 2 : 0;
            const meshtastic_NodeInfoLite *nd = nodeDB->getMeshNode(me->people[i]);
            uint32_t now = getValidTime(RTCQualityFromNet);
            if (nd && nd->last_heard && now > nd->last_heard) {
                uint32_t s = now - nd->last_heard;
                snprintf(r, rn, s < 3600 ? "%um" : s < 86400 ? "%uh" : "%ud",
                         (unsigned)(s < 3600 ? s / 60 : s < 86400 ? s / 3600 : s / 86400));
            }
        });
    }

    void drawChat(OLEDDisplay *d, int16_t x, int16_t y)
    {
        const auto &msgs = messageStore.getMessages();
        char title[48];
        chatName(chat, title, sizeof(title));
        const char *label = privacyLabel(chat);
        d->setFont(FONT_SMALL);
        int tl = strlen(title), room = d->width() - d->getStringWidth(label) - 10;
        while (tl > 1 && d->getStringWidth(title, tl) > room) // keep the label whole
            title[--tl] = 0;
        drawTitle(d, x, y, title, label);
        int lh = fontHeight(FONT_SMALL), top = y + lh + 2, W = d->width();
        int rows = (d->height() - lh - 2) / lh;
        if (composing) {
            const int boxRows = 3; // who it goes to (or the letters of the key), two lines of text
            rows -= boxRows;
            drawCompose(d, x, top + rows * lh, W, boxRows, lh);
        }
        build(d, W - 4);
        int count = 0;
        for (const auto &m : msgs)
            count += inChat(m, chat);
        if (int *s = seenFor(chat, count)) // reading it: all of it is read
            *s = count;
        if (lines.empty()) {
            if (!composing) {
                drawCentered(d, x, top + lh, "No messages here yet", FONT_SMALL);
                drawCentered(d, x, top + 3 * lh, "Tap a digit to write one", FONT_SMALL);
            }
            return;
        }
        int total = (int)lines.size();
        int maxUp = total > rows ? total - rows : 0;
        if (fromBottom > maxUp)
            fromBottom = maxUp;
        int first = total - rows - fromBottom;
        if (first < 0)
            first = 0;
        d->setFont(FONT_SMALL);
        d->setTextAlignment(TEXT_ALIGN_LEFT);
        for (int i = 0; i < rows && first + i < total; i++) {
            const Line &l = lines[first + i];
            int ly = top + i * lh;
            if (l.header) {
                d->drawHorizontalLine(x, ly, W);
                d->drawString(x + 2, ly, l.text.c_str());
            } else {
                d->drawString(x + 4, ly, l.text.c_str());
            }
        }
        if (fromBottom) { // scrolled back: newer ones are below
            int by = top + rows * lh;
            d->fillTriangle(x + W - 8, by - 6, x + W - 2, by - 6, x + W - 5, by - 2);
        }
    }

    void build(OLEDDisplay *d, int width)
    {
        lines.clear();
        const auto &msgs = messageStore.getMessages();
        d->setFont(FONT_SMALL);
        char h[96], who[40], when[8];
        for (int i = 0; i < (int)msgs.size(); i++) {
            const StoredMessage &m = msgs[i];
            if (!inChat(m, chat))
                continue;
            ago(when, sizeof(when), m);
            if (mine(m))
                snprintf(who, sizeof(who), "You");
            else
                meshNodeName(m.sender, who, sizeof(who));
            snprintf(h, sizeof(h), "%s  %s", who, when);
            std::string hs = h;
            while (hs.size() > 1 && d->getStringWidth(hs.c_str()) > width)
                hs.pop_back();
            lines.push_back({hs, i, true});
            std::string t = MessageStore::getText(m);
            for (auto &c : t)
                if (c == '\n' || c == '\r')
                    c = ' ';
            while (!t.empty()) {
                size_t cut = t.size();
                while (cut > 1 && d->getStringWidth(t.c_str(), (uint16_t)cut) > width - 2) {
                    size_t sp = t.rfind(' ', cut - 1);
                    cut = (sp != std::string::npos && sp > 0) ? sp : cut - 1;
                }
                lines.push_back({t.substr(0, cut), i, false});
                t.erase(0, cut);
                while (!t.empty() && t[0] == ' ')
                    t.erase(0, 1);
            }
        }
    }

    void startCompose()
    {
        te.reset("", 200, TextEntry::CASE_SENTENCE);
        composing = true;
        fromBottom = 0;
    }

    void send()
    {
        composing = false;
        if (!te.length())
            return;
        static char msg[TextEntry::MAX + 1];
        strncpy(msg, te.text(), sizeof(msg));
        msg[sizeof(msg) - 1] = 0;
        meshSendPrepareTo(chat.dm ? chat.peer : NODENUM_BROADCAST, chat.ch); // only ever to this chat
        keypadInjectText(msg, true); // Meshtastic sends it and adds it to the messages
        statsMessageSent();
        fromBottom = 0;
        appRedraw();
    }

    // the box at the bottom: who it's to (or the key's letters while tapping), then the text
    // with a blinking cursor, scrolled to keep the cursor in view
    void drawCompose(OLEDDisplay *d, int x, int y, int W, int boxRows, int lh)
    {
        d->drawHorizontalLine(x, y, W);
        const char *letters = te.pendingLetters();
        d->setFont(FONT_SMALL);
        if (letters) {
            int n = (int)strlen(letters), cell = 12, sx = x + (W - n * cell) / 2;
            for (int i = 0; i < n; i++) {
                char s[2] = {te.applyCase(letters[i]), 0};
                if (s[0] == ' ')
                    s[0] = '_';
                if (i == te.pendingIndex()) {
                    d->fillRect(sx + i * cell, y + 1, cell, lh);
                    d->setColor(BLACK);
                }
                d->setTextAlignment(TEXT_ALIGN_CENTER);
                d->drawString(sx + i * cell + cell / 2, y + 1, s);
                d->setColor(WHITE);
            }
            d->setTextAlignment(TEXT_ALIGN_LEFT);
        } else {
            char who[40], b[64], r[16];
            chatName(chat, who, sizeof(who));
            Privacy pv = chatPrivacy(chat);
            if (chat.dm)
                snprintf(b, sizeof(b), pv == P_PRIVATE ? "To %s only" : "To %s: no key yet, can't send", who);
            else
                snprintf(b, sizeof(b),
                         pv == P_PRIVATE ? "To all in %s" : pv == P_NONE ? "To ALL in %s (no encryption)" : "To ALL in %s (public)",
                         who);
            snprintf(r, sizeof(r), "%s %d", te.modeName(), 200 - te.length());
            int n = strlen(b), rw = d->getStringWidth(r);
            while (n > 1 && d->getStringWidth(b, n) > W - rw - 6)
                b[--n] = 0;
            d->drawString(x + 2, y + 1, b);
            d->drawString(x + W - rw - 2, y + 1, r);
        }
        const char *t = te.text();
        int len = te.length(), cur = te.cursor(), w = W - 6, maxLines = boxRows - 1, ty = y + 1 + lh;
        int starts[40], nl = 0, i = 0, lineStart = 0, lastSpace = -1;
        starts[nl++] = 0;
        while (i < len && nl < 40) {
            if (t[i] == ' ')
                lastSpace = i;
            if (d->getStringWidth(t + lineStart, (uint16_t)(i - lineStart + 1)) > w && i > lineStart) {
                lineStart = (lastSpace > lineStart) ? lastSpace + 1 : i;
                starts[nl++] = lineStart;
                lastSpace = -1;
                i = lineStart;
                continue;
            }
            i++;
        }
        int curLine = 0;
        for (int l = 0; l < nl; l++)
            if (cur >= starts[l])
                curLine = l;
        int firstLine = curLine >= maxLines ? curLine - maxLines + 1 : 0;
        for (int l = firstLine; l < nl && l < firstLine + maxLines; l++) {
            int s = starts[l], e = (l + 1 < nl) ? starts[l + 1] : len;
            char line[TextEntry::MAX + 1];
            memcpy(line, t + s, (size_t)(e - s));
            line[e - s] = 0;
            int ly = ty + (l - firstLine) * lh;
            d->drawString(x + 3, ly, line);
            if (l == curLine && blink)
                d->drawVerticalLine(x + 3 + d->getStringWidth(t + s, (uint16_t)(cur - s)), ly + 1, lh - 1);
        }
    }

    void leave(int dir);
};

constexpr const char *MessagesApp::NEW_ITEMS[3];
constexpr const char *MessagesApp::SHARE_ITEMS[5];
static MessagesApp &messagesApp()
{
    static MessagesApp *p = psramNew<MessagesApp>();
    return *p;
}

void MessagesApp::leave(int dir)
{
    // Put Meshtastic's pages back already on the page before (left) or after (right) its
    // Messages page, before anything is drawn: rebuilding them here makes the badge screen's
    // closing a no-op for Meshtastic, which would otherwise show its home page first.
    PageInfo p = pageInfo();
    if (screen && p.messages != 255) {
        screen->setFrames(graphics::Screen::FOCUS_PRESERVE);
        p = pageInfo();
        if (p.count) {
            uint8_t target = (uint8_t)((p.messages + (dir > 0 ? 1 : p.count - 1)) % p.count);
            OLEDDisplayUi *ui = screen->*stealUi(ScreenUi{});
            ui->switchToFrame(target);
        }
    }
    appClose();
}

void openMessages()
{
    appOpen(&messagesApp());
}

void openMessagesChannel(uint8_t ch)
{
    messagesApp().startIn({false, ch, 0}, nullptr);
    appOpen(&messagesApp());
}

void openMessagesCompose(int firstKey)
{
    (void)firstKey; // pick the chat first, so nothing goes to the wrong one
    appOpen(&messagesApp());
}

// ---- an invite arrived: show it, # joins -------------------------------------------------------
class InviteApp : public App
{
  public:
    bool numberShortcuts() const override { return true; }
    const char *name() const override { return "Invite"; }
    bool keepAwake() const override { return true; }
    bool take()
    {
        if (!meshTakeInvite(from, chName, sizeof(chName), psk, len))
            return false;
        bool same = false;
        newKey = meshChannelNamed(chName, psk, len, &same) >= 0 && !same; // a group you're in, re-keyed
        return true;
    }
    void end() override { memset(psk, 0, sizeof(psk)); }
    void key(int k, KeyAction a) override
    {
        if (a != KA_RELEASE)
            return;
        if (k == AK_STAR) {
            appClose();
        } else if (k == AK_HASH) {
            const char *why = nullptr;
            int idx = meshJoinWithKey(chName, psk, len, &why);
            memset(psk, 0, sizeof(psk));
            appClose();
            if (idx >= 0) {
                static char b[48];
                snprintf(b, sizeof(b), why ? "%s" : "Joined #%s", why ? why : chName);
                messagesApp().startIn(Chat{false, (uint8_t)idx, 0}, b);
                appOpen(&messagesApp());
            } else if (why && screen) {
                screen->showSimpleBanner(why, 3500);
            }
        }
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        drawTitle(d, x, y, newKey ? "New group key" : "Channel invite");
        int lh = fontHeight(FONT_SMALL);
        char who[40], b[48];
        meshNodeName(from, who, sizeof(who));
        drawCentered(d, x, y + lh + 4, who, FONT_SMALL);
        drawCentered(d, x, y + 2 * lh + 4, newKey ? "sent a new key for" : "invites you to the group", FONT_SMALL);
        snprintf(b, sizeof(b), "#%s", chName);
        drawCentered(d, x, y + 3 * lh + 6, b, FONT_MEDIUM);
        if (newKey) { // anyone could send one: only switch for someone you trust
            drawCentered(d, x, y + d->height() - 3 * lh - 2, "Only if you trust them:", FONT_SMALL);
            drawCentered(d, x, y + d->height() - 2 * lh - 2, "it replaces your key", FONT_SMALL);
            drawCentered(d, x, y + d->height() - lh - 2, "#switch  *keep mine", FONT_SMALL);
        } else {
            drawCentered(d, x, y + d->height() - 2 * lh - 2, "(private: its key came", FONT_SMALL);
            drawCentered(d, x, y + d->height() - lh - 2, "encrypted)  #join *no", FONT_SMALL);
        }
    }

  private:
    uint32_t from = 0;
    bool newKey = false;
    char chName[12] = {};
    uint8_t psk[32] = {};
    int len = 0;
};
static InviteApp inviteApp;

static void invitePoll()
{
    if (!meshInvitePending() || lockLocked() || !screen || screen->isOverlayBannerShowing())
        return;
    App *a = appActive();
    if (a && (strcmp(a->name(), "Name tag") && strcmp(a->name(), "Messages")))
        return; // in a game, a scan, ...: show it when that's done
    if (a == &messagesApp() && messagesApp().writing())
        return; // not in the middle of writing
    if (inviteApp.take())
        appOpen(&inviteApp);
}

// joystick left/right on Meshtastic's pages: if that page turn would land on its Messages page,
// open ours instead (so its page never shows). True if it did.
bool messagesTakePageTurn(int dir)
{
    PageInfo p = pageInfo();
    if (!p.normal || !p.settled || p.messages == 255 || !p.count || appActive() || lockLocked() ||
        screen->isOverlayBannerShowing())
        return false;
    uint8_t next = (uint8_t)((p.current + (dir > 0 ? 1 : p.count - 1)) % p.count);
    if (next != p.messages)
        return false;
    openMessages();
    return true;
}

// Meshtastic's Messages page came up some other way (e.g. it jumped there itself): put ours up
void messagesPoll(uint32_t now)
{
    (void)now;
    invitePoll();
    PageInfo p = pageInfo();
    if (!p.normal || !p.settled || p.messages == 255 || p.current != p.messages)
        return;
    if (appActive() || lockLocked() || welcomeActive() || !screen->isScreenOn() || screen->isOverlayBannerShowing())
        return;
    openMessages();
}

} // namespace badge

// Mesh settings menu (see BadgeMeshSettings.h). Each change is saved the way Meshtastic's own
// admin code saves it (AdminModule.cpp), so the phone app sees the same settings.
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeBase64.h"
#include "BadgeHome.h"
#include "BadgeMenu.h"
#include "BadgeMesh.h"
#include "BadgeMeshSettings.h"

#include "MeshService.h"
#include "NodeDB.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "graphics/draw/MenuHandler.h"
#include "main.h"
#include "mesh/Channels.h"
#include "mesh/MeshTypes.h"
#include "mesh/generated/meshtastic/cannedmessages.pb.h"
#include "modules/CannedMessageModule.h"
#include <Arduino.h>
#include <Preferences.h>
#include <functional>
#include <stdio.h>
#include <string.h>

extern meshtastic_CannedMessageModuleConfig cannedMessageModuleConfig; // CannedMessageModule.cpp

namespace badge
{

// where each menu reopens: the item last chosen, so changing a setting doesn't jump to the top
static int mainSel = 1, chListSel = 1, chItemSel = 1, cannedListSel = 1;

enum MsStep {
    MS_NONE,
    MS_MAIN,
    MS_CHANNELS,
    MS_CHANNEL,
    MS_JOIN_KEY,
    MS_SENDTO,
    MS_TXPOWER,
    MS_HOPS,
    MS_CANNED,
    MS_CANNED_ITEM,
    MS_LORA,
    MS_BT
};
static volatile MsStep queued = MS_NONE;
static int chSel = 0;     // channel being looked at
static int cannedSel = 0; // quick-send message being looked at
static char pendingName[12];
static const int MAX_CH = 8;

static void showKeyChoice();
static void queue(MsStep s)
{
    queued = s;
}

static void banner(const char *title, const char **items, int count, int selected, void (*cb)(int))
{
    graphics::BannerOverlayOptions o;
    o.message = title;
    o.optionsArrayPtr = items;
    o.optionsCount = (uint8_t)count;
    o.InitialSelected = (int8_t)selected;
    o.bannerCallback = cb;
    screen->showOverlayBanner(o);
}
static void note(const char *text, uint32_t ms = 2000)
{
    if (screen)
        screen->showSimpleBanner(text, ms);
}

// ---- a page of text (channel keys), closed with * or centre ---------------------------------
class InfoApp : public App
{
  public:
    void show(const char *t, const char *body, MsStep back)
    {
        strncpy(title, t, sizeof(title) - 1);
        strncpy(text, body, sizeof(text) - 1);
        title[sizeof(title) - 1] = text[sizeof(text) - 1] = 0;
        after = back;
        appOpen(this);
    }
    const char *name() const override { return "Info"; }
    bool keepAwake() const override { return true; }
    uint32_t frameMs() const override { return 1000; }
    void key(int k, KeyAction a) override
    {
        if ((k == AK_STAR && a == KA_RELEASE) || (k == AK_HASH && a == KA_PRESS)) {
            appClose();
            queue(after);
        }
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        drawTitle(d, x, y, title, "*back");
        int lh = fontHeight(FONT_SMALL);
        d->setFont(FONT_SMALL);
        int ly = y + lh + 4;
        const char *s = text;
        // one line per '\n'; long lines (keys) are broken to fit
        while (*s && ly + lh <= y + d->height()) {
            const char *e = strchr(s, '\n');
            int n = e ? (int)(e - s) : (int)strlen(s);
            int fit = n;
            while (fit > 1 && d->getStringWidth(s, (uint16_t)fit) > d->width() - 4)
                fit--;
            char line[64];
            int m = fit < 63 ? fit : 63;
            memcpy(line, s, (size_t)m);
            line[m] = 0;
            d->drawString(x + 2, ly, line);
            ly += lh;
            s += fit;
            if (e && s == e)
                s++; // the whole line fitted: skip its newline
        }
    }

  private:
    char title[24] = "";
    char text[200] = "";
    MsStep after = MS_NONE;
};
static InfoApp info;

// ---- node name ----------------------------------------------------------------------------
static void saveOwner()
{
    service->reloadOwner(true); // tells the mesh
    service->reloadConfig(SEGMENT_DEVICESTATE | SEGMENT_NODEDATABASE);
}
static void longNameDone(const char *t)
{
    if (t && *t) {
        strncpy(owner.long_name, t, sizeof(owner.long_name) - 1);
        owner.long_name[sizeof(owner.long_name) - 1] = 0;
        saveOwner();
        note("Node name saved");
    }
    queue(MS_MAIN);
}
static void shortNameDone(const char *t)
{
    if (t && *t) {
        strncpy(owner.short_name, t, sizeof(owner.short_name) - 1);
        owner.short_name[sizeof(owner.short_name) - 1] = 0;
        saveOwner();
        note("Short name saved");
    }
    queue(MS_MAIN);
}

// ---- channels -----------------------------------------------------------------------------
static bool chActive(int i)
{
    return channels.getByIndex((ChannelIndex)i).role != meshtastic_Channel_Role_DISABLED;
}
static int freeSlot()
{
    for (int i = 1; i < MAX_CH; i++)
        if (!chActive(i))
            return i;
    return -1;
}
static void saveChannel(meshtastic_Channel &ch)
{
    channels.setChannel(ch);
    channels.onConfigChanged(); // tells the radio
    service->reloadConfig(SEGMENT_CHANNELS);
}
static void keyText(const meshtastic_ChannelSettings &s, char *out, size_t n)
{
    if (s.psk.size == 0)
        snprintf(out, n, "none (not encrypted)");
    else if (s.psk.size == 1)
        snprintf(out, n, s.psk.bytes[0] == 1 ? "AQ== (the default key)" : "simple%u", s.psk.bytes[0] - 1);
    else
        base64Encode(s.psk.bytes, s.psk.size, out);
}
static void showKey(int i)
{
    meshtastic_Channel &ch = channels.getByIndex((ChannelIndex)i);
    char k[48], body[200];
    keyText(ch.settings, k, sizeof(k));
    snprintf(body, sizeof(body), "Channel: %s\nKey:\n%s\n\nType the name and key\non the other badge,\nor in the app.",
             channels.getName((size_t)i), k);
    info.show("Channel key", body, MS_CHANNEL);
}
static void setKeyRandom(meshtastic_ChannelSettings &s)
{
    s.psk.size = 16; // AES-128: 24 characters to type on another badge
    for (int i = 0; i < 16; i++)
        s.psk.bytes[i] = (uint8_t)random(256);
}
// make the pending channel (or re-key the selected one) with a key choice
static void applyKey(int choice, const uint8_t *typed, int typedLen)
{
    bool creating = pendingName[0] != 0;
    int idx = creating ? freeSlot() : chSel;
    if (idx < 0) {
        note("All 8 channels are in use");
        queue(MS_CHANNELS);
        return;
    }
    meshtastic_Channel ch = channels.getByIndex((ChannelIndex)idx);
    if (creating) {
        memset(&ch.settings, 0, sizeof(ch.settings));
        strncpy(ch.settings.name, pendingName, sizeof(ch.settings.name) - 1);
        ch.has_settings = true;
        ch.role = meshtastic_Channel_Role_SECONDARY;
    }
    ch.index = (int8_t)idx;
    switch (choice) {
    case 0:
        setKeyRandom(ch.settings);
        break;
    case 1:
        ch.settings.psk.size = (pb_size_t)typedLen;
        memcpy(ch.settings.psk.bytes, typed, (size_t)typedLen);
        break;
    case 2:
        ch.settings.psk.size = 1, ch.settings.psk.bytes[0] = 1;
        break; // the default key
    default:
        ch.settings.psk.size = 0;
        break; // not encrypted
    }
    saveChannel(ch);
    pendingName[0] = 0;
    chSel = idx;
    showKey(idx);
}
static void typedKeyDone(const char *t)
{
    if (!t) {
        queue(MS_JOIN_KEY);
        return;
    }
    uint8_t k[32];
    int n = base64Decode(t, k, sizeof(k));
    if (n != 16 && n != 32 && n != 1) {
        note("That isn't a channel key\n(24 or 44 characters,\nfrom the other badge or app)", 3500);
        queue(MS_JOIN_KEY);
        return;
    }
    applyKey(1, k, n);
}
static void newNameDone(const char *t)
{
    if (!t || !*t) {
        pendingName[0] = 0;
        queue(MS_CHANNELS);
        return;
    }
    strncpy(pendingName, t, sizeof(pendingName) - 1);
    pendingName[sizeof(pendingName) - 1] = 0;
    showKeyChoice(); // straight into the key step, so nothing can interrupt between name and key
}
int meshChannelNamed(const char *name, const uint8_t *psk, int len, bool *sameKey)
{
    for (int i = 0; i < MAX_CH; i++) {
        const meshtastic_Channel &c = channels.getByIndex((ChannelIndex)i);
        if (chActive(i) && !strcmp(channels.getName((size_t)i), name)) {
            if (sameKey)
                *sameKey = c.settings.psk.size == len && !memcmp(c.settings.psk.bytes, psk, (size_t)len);
            return i;
        }
    }
    return -1;
}

int meshJoinWithKey(const char *name, const uint8_t *psk, int len, const char **why)
{
    *why = nullptr;
    bool same = false;
    int have = meshChannelNamed(name, psk, len, &same);
    if (have >= 0 && same) {
        *why = "You're already in it";
        return have;
    }
    if (have >= 0) { // a group of that name, with a new key: switch it (not a second one)
        meshtastic_Channel ch = channels.getByIndex((ChannelIndex)have);
        if (ch.role == meshtastic_Channel_Role_PRIMARY) {
            *why = "That's the main channel: not changed";
            return -1;
        }
        ch.settings.psk.size = (pb_size_t)len;
        memcpy(ch.settings.psk.bytes, psk, (size_t)len);
        ch.index = (int8_t)have;
        saveChannel(ch);
        *why = "Switched to the new key";
        return have;
    }
    int idx = freeSlot();
    if (idx < 0) {
        *why = "All 8 channels are in use:\nremove one in Mesh settings";
        return -1;
    }
    meshtastic_Channel ch = channels.getByIndex((ChannelIndex)idx);
    memset(&ch.settings, 0, sizeof(ch.settings));
    strncpy(ch.settings.name, name, sizeof(ch.settings.name) - 1);
    ch.settings.psk.size = (pb_size_t)len;
    memcpy(ch.settings.psk.bytes, psk, (size_t)len);
    ch.has_settings = true;
    ch.role = meshtastic_Channel_Role_SECONDARY;
    ch.index = (int8_t)idx;
    saveChannel(ch);
    return idx;
}

const char *meshLeaveChannel(int idx)
{
    if (idx < 0 || idx >= MAX_CH || !chActive(idx))
        return "That group is gone";
    meshtastic_Channel ch = channels.getByIndex((ChannelIndex)idx);
    if (ch.role == meshtastic_Channel_Role_PRIMARY)
        return "The main channel can't be left";
    ch.role = meshtastic_Channel_Role_DISABLED;
    memset(&ch.settings, 0, sizeof(ch.settings));
    ch.index = (int8_t)idx;
    saveChannel(ch);
    return nullptr;
}

const char *meshRekeyChannel(int idx)
{
    if (idx < 0 || idx >= MAX_CH || !chActive(idx))
        return "That group is gone";
    meshtastic_Channel ch = channels.getByIndex((ChannelIndex)idx);
    if (ch.role == meshtastic_Channel_Role_PRIMARY)
        return "The main channel: change it in the app";
    setKeyRandom(ch.settings);
    ch.index = (int8_t)idx;
    saveChannel(ch);
    return nullptr;
}

void meshJoinChannel()
{
    if (freeSlot() < 0) {
        note("No room: 8 channels already.\nRemove one in Mesh settings");
        return;
    }
    pendingName[0] = 0;
    openTextEditor("Channel name", "", 11, 1, newNameDone); // then: new key, type a key, ...
}
static void renameDone(const char *t)
{
    if (t && *t) {
        meshtastic_Channel ch = channels.getByIndex((ChannelIndex)chSel);
        strncpy(ch.settings.name, t, sizeof(ch.settings.name) - 1);
        ch.settings.name[sizeof(ch.settings.name) - 1] = 0;
        ch.index = (int8_t)chSel;
        saveChannel(ch);
        note("Channel renamed");
    }
    queue(MS_CHANNEL);
}

static void showChannels()
{
    static char lbl[MAX_CH + 2][28];
    static const char *items[MAX_CH + 2];
    static int map[MAX_CH + 2];
    int n = 0;
    items[n] = "Back";
    map[n++] = -1;
    for (int i = 0; i < MAX_CH; i++) {
        if (!chActive(i))
            continue;
        bool primary = channels.getByIndex((ChannelIndex)i).role == meshtastic_Channel_Role_PRIMARY;
        snprintf(lbl[n], sizeof(lbl[n]), "%s%s", channels.getName((size_t)i), primary ? " (main)" : "");
        items[n] = lbl[n];
        map[n++] = i;
    }
    if (freeSlot() >= 0) {
        items[n] = "+ Join / new channel";
        map[n++] = -2;
    }
    static int count;
    count = n;
    if (chListSel >= n)
        chListSel = n - 1;
    banner("Channels", items, n, chListSel > 0 ? chListSel : 1, [](int sel) -> void {
        if (sel > 0 && sel < count)
            chListSel = sel;
        if (sel <= 0 || sel >= count) {
            queue(MS_MAIN);
        } else if (map[sel] == -2) {
            pendingName[0] = 0;
            openTextEditor("Channel name", "", 11, 1, newNameDone);
        } else {
            chSel = map[sel];
            chItemSel = 1;
            queue(MS_CHANNEL);
        }
    });
}
static void showChannel()
{
    bool primary = channels.getByIndex((ChannelIndex)chSel).role == meshtastic_Channel_Role_PRIMARY;
    static char title[40];
    snprintf(title, sizeof(title), "%s%s", channels.getName((size_t)chSel), primary ? "\n(main channel: view only)" : "");
    if (primary) {
        static const char *items[] = {"Back", "Show key"};
        banner(title, items, 2, 1, [](int sel) -> void {
            if (sel == 1)
                showKey(chSel);
            else
                queue(MS_CHANNELS);
        });
        return;
    }
    static const char *items[] = {"Back", "Show key", "Rename", "New key", "Remove"};
    banner(title, items, 5, chItemSel, [](int sel) -> void {
        if (sel > 0)
            chItemSel = sel;
        switch (sel) {
        case 1:
            showKey(chSel);
            break;
        case 2:
            openTextEditor("Rename channel", channels.getName((size_t)chSel), 11, 1, renameDone);
            break;
        case 3:
            pendingName[0] = 0; // re-key this one
            queue(MS_JOIN_KEY);
            break;
        case 4: {
            meshtastic_Channel ch = channels.getByIndex((ChannelIndex)chSel);
            ch.role = meshtastic_Channel_Role_DISABLED;
            memset(&ch.settings, 0, sizeof(ch.settings));
            ch.index = (int8_t)chSel;
            saveChannel(ch);
            note("Channel removed");
            queue(MS_CHANNELS);
            break;
        }
        default:
            queue(MS_CHANNELS);
            break;
        }
    });
}
static void showKeyChoice()
{
    static char title[40];
    snprintf(title, sizeof(title), "Key for %s", pendingName[0] ? pendingName : channels.getName((size_t)chSel));
    static const char *items[] = {"Back", "Make a new random key", "Type the key", "Default key (public)",
                                  "No key (not encrypted)"};
    banner(title, items, 5, 1, [](int sel) -> void {
        switch (sel) {
        case 1:
            applyKey(0, nullptr, 0);
            break;
        case 2:
            openTextEditor("Channel key", "", 44, 0, typedKeyDone);
            break;
        case 3:
            applyKey(2, nullptr, 0);
            break;
        case 4:
            applyKey(3, nullptr, 0);
            break;
        default:
            if (pendingName[0]) {
                pendingName[0] = 0;
                queue(MS_CHANNELS);
            } else {
                queue(MS_CHANNEL);
            }
            break;
        }
    });
}

// ---- where messages go ------------------------------------------------------------------------
static int sendChannel()
{
    Preferences p;
    int c = 0;
    if (p.begin("utah", true)) {
        c = p.getInt("sendch", 0);
        p.end();
    }
    if (c < 0 || c >= MAX_CH || !chActive(c))
        c = channels.getPrimaryIndex();
    return c;
}
const char *meshSendChannelName()
{
    return channels.getName((size_t)sendChannel());
}
// Send straight through the mesh service (no on-screen keyboard) to the chosen "Send messages to"
// channel. Driving Meshtastic's freetext UI to send would pop up its on-screen keyboard on 2.8.
bool meshSendTextDefault(const char *text)
{
    return meshSendTextDM(NODENUM_BROADCAST, text, (uint8_t)sendChannel());
}
static void showSendTo()
{
    static char lbl[MAX_CH][24];
    static const char *items[MAX_CH + 1];
    static int map[MAX_CH + 1];
    int n = 0, cur = sendChannel(), selected = 1;
    items[n] = "Back";
    map[n++] = -1;
    for (int i = 0; i < MAX_CH; i++)
        if (chActive(i)) {
            snprintf(lbl[i], sizeof(lbl[i]), "%s", channels.getName((size_t)i));
            if (i == cur)
                selected = n;
            items[n] = lbl[i];
            map[n++] = i;
        }
    banner("Send messages to", items, n, selected, [](int sel) -> void {
        if (sel > 0) {
            Preferences p;
            if (p.begin("utah", false)) {
                p.putInt("sendch", map[sel]);
                p.end();
            }
        }
        queue(MS_MAIN);
    });
}

// ---- radio ----------------------------------------------------------------------------------
static const int8_t TX_LEVELS[] = {0, 20, 17, 14, 10, 7};
static const char *const TX_NAMES[] = {"Max allowed", "20 dBm", "17 dBm", "14 dBm", "10 dBm", "7 dBm"};
static const int NTX = sizeof(TX_LEVELS) / sizeof(TX_LEVELS[0]);
static const char *txName()
{
    for (int i = 0; i < NTX; i++)
        if (config.lora.tx_power == TX_LEVELS[i])
            return TX_NAMES[i];
    static char b[12];
    snprintf(b, sizeof(b), "%d dBm", config.lora.tx_power);
    return b;
}
static void showTxPower()
{
    static const char *items[NTX + 1];
    items[0] = "Back";
    int selected = 1;
    for (int i = 0; i < NTX; i++) {
        items[i + 1] = TX_NAMES[i];
        if (config.lora.tx_power == TX_LEVELS[i])
            selected = i + 1;
    }
    banner("Transmit power\n(lower saves battery)", items, NTX + 1, selected, [](int sel) -> void {
        if (sel > 0 && config.lora.tx_power != TX_LEVELS[sel - 1]) {
            config.lora.tx_power = TX_LEVELS[sel - 1];
            service->reloadConfig(SEGMENT_CONFIG);
            rebootAtMsec = millis() + DEFAULT_REBOOT_SECONDS * 1000; // the radio needs a restart, as in the app
            note("Saved. Restarting...", 3000);
            return;
        }
        queue(MS_MAIN);
    });
}
static void showHops()
{
    static const char *items[] = {"Back", "1", "2", "3 (usual)", "4", "5", "6", "7"};
    int cur = (int)config.lora.hop_limit;
    banner("Hop limit\n(how far messages travel)", items, 8, (cur >= 1 && cur <= 7) ? cur : 3, [](int sel) -> void {
        if (sel > 0) {
            config.lora.hop_limit = (uint32_t)sel;
            service->reloadConfig(SEGMENT_CONFIG);
            note("Hop limit saved");
        }
        queue(MS_MAIN);
    });
}

// ---- quick-send messages (Meshtastic's canned messages, separated by '|') --------------------
static const int MAX_CANNED = 16;
static char cannedBuf[sizeof(cannedMessageModuleConfig.messages)];
static char *canned[MAX_CANNED];
static int nCanned = 0;
static void loadCanned()
{
    strncpy(cannedBuf, cannedMessageModuleConfig.messages, sizeof(cannedBuf) - 1);
    cannedBuf[sizeof(cannedBuf) - 1] = 0;
    nCanned = 0;
    char *s = cannedBuf;
    while (*s && nCanned < MAX_CANNED) {
        canned[nCanned++] = s;
        char *bar = strchr(s, '|');
        if (!bar)
            break;
        *bar = 0;
        s = bar + 1;
    }
}
static int cannedUsed(int skip)
{
    int n = 0;
    for (int i = 0; i < nCanned; i++)
        if (i != skip)
            n += (int)strlen(canned[i]) + 1;
    return n;
}
// rebuild the '|' list with message `at` replaced by `text` (nullptr removes it, at == nCanned adds)
static void saveCanned(int at, const char *text)
{
    char out[sizeof(cannedMessageModuleConfig.messages)];
    out[0] = 0;
    size_t o = 0;
    for (int i = 0; i <= nCanned; i++) {
        const char *m = i == at ? text : (i < nCanned ? canned[i] : nullptr);
        if (!m || !*m)
            continue;
        for (const char *c = m; *c && o + 2 < sizeof(out); c++)
            if (*c != '|')
                out[o++] = *c;
        out[o++] = '|';
        out[o] = 0;
    }
    if (o > 0)
        out[--o] = 0; // no trailing '|'
    if (!out[0]) {
        note("Keep at least one message");
        return;
    }
    if (cannedMessageModule)
        cannedMessageModule->handleSetCannedMessageModuleMessages(out); // saves it, like the app
}
static void cannedDone(const char *t)
{
    if (t && *t) {
        loadCanned();
        saveCanned(cannedSel, t);
        note("Message saved");
    }
    queue(MS_CANNED);
}
static void showCanned()
{
    loadCanned();
    static char lbl[MAX_CANNED][24];
    static const char *items[MAX_CANNED + 2];
    int n = 0;
    items[n++] = "Back";
    for (int i = 0; i < nCanned; i++) {
        snprintf(lbl[i], sizeof(lbl[i]), "%d %.19s", i + 1, canned[i]);
        items[n++] = lbl[i];
    }
    bool room = nCanned < MAX_CANNED && cannedUsed(-1) < (int)sizeof(cannedBuf) - 8;
    if (room)
        items[n++] = "+ Add message";
    static int count;
    count = n;
    if (cannedListSel >= n)
        cannedListSel = n - 1;
    banner("Quick messages\n(Settings > Shortcuts\nputs them on a number)", items, n, cannedListSel > 0 ? cannedListSel : 1,
           [](int sel) -> void {
               if (sel > 0 && sel < count)
                   cannedListSel = sel;
               if (sel <= 0 || sel >= count) {
                   queue(MS_MAIN);
               } else if (sel - 1 == nCanned) {
                   cannedSel = nCanned;
                   int room = (int)sizeof(cannedBuf) - 2 - cannedUsed(-1);
                   openTextEditor("New message", "", room, 1, cannedDone);
               } else {
                   cannedSel = sel - 1;
                   queue(MS_CANNED_ITEM);
               }
           });
}
static void showCannedItem()
{
    loadCanned();
    if (cannedSel >= nCanned) {
        queue(MS_CANNED);
        return;
    }
    static char title[48];
    snprintf(title, sizeof(title), "%d: %.40s", cannedSel + 1, canned[cannedSel]);
    static const char *items[] = {"Back", "Edit", "Move up", "Delete"};
    banner(title, items, 4, 1, [](int sel) -> void {
        loadCanned();
        if (sel == 1) {
            int room = (int)sizeof(cannedBuf) - 2 - cannedUsed(cannedSel);
            openTextEditor("Edit message", canned[cannedSel], room, 1, cannedDone);
            return;
        }
        if (sel == 2 && cannedSel > 0) {
            char *t = canned[cannedSel];
            canned[cannedSel] = canned[cannedSel - 1];
            canned[cannedSel - 1] = t;
            saveCanned(-1, nullptr);
            cannedSel--;
            cannedListSel = cannedSel + 1; // follow the message that moved
        } else if (sel == 3) {
            if (nCanned <= 1)
                note("Keep at least one message");
            else
                saveCanned(cannedSel, nullptr);
        }
        queue(MS_CANNED);
    });
}

// ---- the menu ---------------------------------------------------------------------------------
static void showMain()
{
    enum { I_BACK, I_LONG, I_SHORT, I_CHANNELS, I_SENDTO, I_TX, I_HOPS, I_CANNED, I_LORA, I_BT, I_COUNT };
    static char lbl[I_COUNT][48];
    static const char *items[I_COUNT];
    snprintf(lbl[I_BACK], 48, "Back");
    snprintf(lbl[I_LONG], 48, "Node name: %s", owner.long_name);
    snprintf(lbl[I_SHORT], 48, "Short name: %s", owner.short_name);
    snprintf(lbl[I_CHANNELS], 48, "Channels");
    snprintf(lbl[I_SENDTO], 48, "Send messages to: %s", meshSendChannelName());
    snprintf(lbl[I_TX], 48, "Transmit power: %s", txName());
    snprintf(lbl[I_HOPS], 48, "Hop limit: %u", (unsigned)config.lora.hop_limit);
    snprintf(lbl[I_CANNED], 48, "Quick-send messages");
    snprintf(lbl[I_LORA], 48, "Region, preset, role");
    snprintf(lbl[I_BT], 48, "Bluetooth on/off");
    for (int i = 0; i < I_COUNT; i++)
        items[i] = lbl[i];
    banner("Mesh settings", items, I_COUNT, mainSel, [](int sel) -> void {
        if (sel > 0)
            mainSel = sel;
        switch (sel) {
        case I_LONG:
            openTextEditor("Node name", owner.long_name, sizeof(owner.long_name) - 1, 1, longNameDone);
            break;
        case I_SHORT:
            openTextEditor("Short name (4)", owner.short_name, sizeof(owner.short_name) - 1, 2, shortNameDone);
            break;
        case I_CHANNELS:
            chListSel = 1;
            queue(MS_CHANNELS);
            break;
        case I_SENDTO:
            queue(MS_SENDTO);
            break;
        case I_TX:
            queue(MS_TXPOWER);
            break;
        case I_HOPS:
            queue(MS_HOPS);
            break;
        case I_CANNED:
            cannedListSel = 1;
            queue(MS_CANNED);
            break;
        case I_LORA:
            queue(MS_LORA);
            break;
        case I_BT:
            queue(MS_BT);
            break;
        default:
            menuOpen();
            break; // back to the badge menu
        }
    });
}

void meshSettingsOpen()
{
    mainSel = 1; // opened from the badge menu: start at the top
    queue(MS_MAIN);
}

bool meshSettingsBusy()
{
    return queued != MS_NONE;
}

void meshSettingsPoll()
{
    MsStep s = queued;
    if (s == MS_NONE || !screen || (appActive() && !homeActive()) || screen->isOverlayBannerShowing())
        return;
    queued = MS_NONE;
    switch (s) {
    case MS_MAIN:
        showMain();
        break;
    case MS_CHANNELS:
        showChannels();
        break;
    case MS_CHANNEL:
        showChannel();
        break;
    case MS_JOIN_KEY:
        showKeyChoice();
        break;
    case MS_SENDTO:
        showSendTo();
        break;
    case MS_TXPOWER:
        showTxPower();
        break;
    case MS_HOPS:
        showHops();
        break;
    case MS_CANNED:
        showCanned();
        break;
    case MS_CANNED_ITEM:
        showCannedItem();
        break;
    case MS_LORA:
        graphics::menuHandler::loraMenu();
        break;  // Meshtastic's own pickers
    case MS_BT: // Meshtastic's own toggle, opened through its menu queue (as its System menu does)
        graphics::menuHandler::menuQueue = graphics::menuHandler::BluetoothToggleMenu;
        screen->runNow();
        break;
    default:
        break;
    }
}

// ---- "Your Brand" colour --------------------------------------------------------------------
struct Brand {
    const char *name;
    uint8_t r, g, b;
};
static const Brand BRANDS[] = {
    {"Orange", 255, 96, 0}, {"Red", 255, 0, 0},      {"Gold", 255, 170, 0},  {"Green", 0, 255, 40},    {"Turquoise", 0, 200, 180},
    {"Blue", 0, 60, 255},   {"Purple", 150, 0, 255}, {"Pink", 255, 40, 160}, {"White", 255, 255, 255},
};
static const int NBRANDS = sizeof(BRANDS) / sizeof(BRANDS[0]);
int brandColourCount()
{
    return NBRANDS;
}
const char *brandColourName(int i)
{
    return (i >= 0 && i < NBRANDS) ? BRANDS[i].name : "Custom (app)";
}
int brandColour()
{
    const auto &al = moduleConfig.ambient_lighting;
    if (!al.red && !al.green && !al.blue)
        return 0; // not set: the scenes use orange
    for (int i = 0; i < NBRANDS; i++)
        if (al.red == BRANDS[i].r && al.green == BRANDS[i].g && al.blue == BRANDS[i].b)
            return i;
    return -1;
}
void brandColourSet(int i)
{
    if (i < 0 || i >= NBRANDS)
        return;
    auto &al = moduleConfig.ambient_lighting;
    al.red = BRANDS[i].r;
    al.green = BRANDS[i].g;
    al.blue = BRANDS[i].b;
    nodeDB->saveToDisk(SEGMENT_MODULECONFIG); // the same setting the app's Ambient Lighting uses
}

} // namespace badge

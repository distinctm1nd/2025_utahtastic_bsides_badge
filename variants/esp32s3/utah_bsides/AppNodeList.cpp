// LoRa node list: every Meshtastic node the badge knows, newest first (or by signal), scrollable
// for the hundreds you can see at a busy event. Kept apart from the WiFi / Bluetooth scans. It
// also lists nodes heard on OTHER channels that share our frequency -- their packet header is in
// the clear, so we see the sender, signal and hops even without the channel key (shown as "other
// channel", no name). Nodes on a different frequency (different primary channel / modem preset)
// can't be heard without retuning. Pick one to run the LoRa fox hunt on it.
//
//   joystick (or 2 / 8) scroll, 0 search, 1 sort (recent / signal), # hunt the node, * done
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeListFilter.h"
#include "BadgeMesh.h"

#include "DisplayFormatters.h"
#include "NodeDB.h"
#include "gps/RTC.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "mesh/Throttle.h"
#include <Arduino.h>
#include <algorithm>
#include <stdio.h>

namespace badge
{

static const int MAXN = 300;

struct NodeRow {
    uint32_t num;
    uint32_t ageSecs; // how long since heard (unified across RF / NodeDB)
    float snr;
    int hops; // -1 unknown
    bool mqtt;
    bool favorite;
    bool other;      // heard on a channel we can't decode
    uint16_t chHash; // channel-hash byte from the clear header (other-channel nodes)
    uint8_t preset;  // modem preset it was heard on (0xFF unknown)
    char name[24];
    char hay[40]; // lower-case haystack for the search
};

static void agoShort(char *out, size_t n, uint32_t secs)
{
    if (secs >= 999999)
        snprintf(out, n, "-");
    else if (secs < 60)
        snprintf(out, n, "%us", (unsigned)secs);
    else if (secs < 3600)
        snprintf(out, n, "%um", (unsigned)(secs / 60));
    else if (secs < 86400)
        snprintf(out, n, "%uh", (unsigned)(secs / 3600));
    else
        snprintf(out, n, "%ud", (unsigned)(secs / 86400));
}

class NodeListApp : public App
{
  public:
    bool numberShortcuts() const override { return !filter.editing(); }
    bool typingLights() const override { return filter.editing(); }
    const char *name() const override { return "LoRa nodes"; }
    uint32_t frameMs() const override { return filter.editing() ? 120 : 1000; }
    void begin() override
    {
        sel = top = 0;
        bySignal = false;
        sweeping = false;
        handoff = false;
        sweepAt = millis();
        presetIdx = presetIndexOf(meshCurrentPreset());
        filter.clear();
        load();
    }
    void end() override
    {
        if (!handoff) // leaving for good: put the radio back on the badge's own preset
            meshRestorePreset();
    }
    void tick(uint32_t now) override
    {
        filter.tick(now);
        if (sweeping && !filter.editing() && now - sweepAt > 5000) { // dwell ~5 s per preset
            sweepAt = now;
            setPreset((presetIdx + 1) % meshPresetCount());
        }
    }
    void key(int k, KeyAction a) override
    {
        if (filter.key(k, a, millis())) { // the search box ate it
            rebuildVisible();
            appRedraw();
            return;
        }
        if (k == AK_STAR && a == KA_RELEASE) {
            appClose();
            return;
        }
        if (a != KA_PRESS && a != KA_REPEAT)
            return;
        if (k == AK_DOWN || k == AK_8) {
            if (sel < vcount - 1)
                sel++;
        } else if (k == AK_UP || k == AK_2) {
            if (sel > 0)
                sel--;
        } else if (k == AK_LEFT || k == AK_4) {
            sweeping = false;
            setPreset((presetIdx + meshPresetCount() - 1) % meshPresetCount());
        } else if (k == AK_RIGHT || k == AK_6) {
            sweeping = false;
            setPreset((presetIdx + 1) % meshPresetCount());
        } else if (a == KA_PRESS && k == AK_7) {
            sweeping = !sweeping; // auto-cycle the presets
            sweepAt = millis();
        } else if (a == KA_PRESS && k == AK_0) {
            filter.open();
        } else if (a == KA_PRESS && k == AK_1) {
            bySignal = !bySignal;
            sort();
            rebuildVisible();
            sel = top = 0;
        } else if (a == KA_PRESS && (k == AK_HASH || k == AK_5)) {
            if (vcount && sel < vcount) {
                const NodeRow &r = rows[vis[sel]];
                if (r.preset != 0xFF && r.preset != meshCurrentPreset())
                    meshListenPreset(r.preset); // hunt it on the preset it was heard on
                handoff = true;                 // the fox hunt owns the radio now; it restores our preset on exit
                uint32_t node = r.num;
                appClose();
                openFoxHunt(node);
                return;
            }
        }
        appRedraw();
    }
    void draw(OLEDDisplay *d, int16_t x, int16_t y) override
    {
        if (!filter.editing() && !Throttle::isWithinTimespanMs(lastLoad, 3000)) // refresh every few seconds
            load();
        int lh = fontHeight(FONT_SMALL);
        char title[44];
        const char *preset =
            DisplayFormatters::getModemPresetDisplayName(config.lora.modem_preset, false, config.lora.use_preset);
        snprintf(title, sizeof(title), "%s%s (%d)", sweeping ? "~" : "", preset, vcount);
        drawTitle(d, x, y, title, bySignal ? "by signal" : "by time");
        bool filterRow = filter.active();
        if (!vcount) {
            drawCentered(d, x, y + 2 * lh + 6, count ? "No match" : "No nodes heard yet", FONT_MEDIUM);
            if (filterRow)
                filter.draw(d, x, y);
            else
                drawCentered(d, x, y + 4 * lh + 6, "0 search  1 sort  * done", FONT_SMALL);
            return;
        }
        int listTop = y + lh + 2, rowH = 2 * lh;
        int reserved = lh + (filterRow ? lh + 1 : 0); // bottom hint or the search bar
        int room = d->height() - lh - 2 - reserved + lh;
        int perPage = room / rowH;
        if (perPage < 1)
            perPage = 1;
        if (sel < top)
            top = sel;
        if (sel >= top + perPage)
            top = sel - perPage + 1;
        d->setFont(FONT_SMALL);
        int ry = listTop;
        for (int i = top; i < vcount && i < top + perPage; i++, ry += rowH) {
            const NodeRow &r = rows[vis[i]];
            bool isSel = i == sel;
            if (isSel) {
                d->setColor(WHITE);
                d->fillRect(x, ry, d->width(), rowH);
                d->setColor(BLACK);
            }
            d->setTextAlignment(TEXT_ALIGN_LEFT);
            d->drawString(x + 2, ry, r.favorite ? (String("* ") + r.name).c_str() : r.name);
            char line2[44], ago[12], hop[16];
            agoShort(ago, sizeof(ago), r.ageSecs);
            if (r.other)
                snprintf(hop, sizeof(hop), "other ch #%02X", r.chHash & 0xFF);
            else if (r.mqtt)
                snprintf(hop, sizeof(hop), "MQTT");
            else if (r.hops == 0)
                snprintf(hop, sizeof(hop), "direct %ddB", (int)r.snr);
            else if (r.hops > 0)
                snprintf(hop, sizeof(hop), "%d hop%s", r.hops, r.hops == 1 ? "" : "s");
            else
                snprintf(hop, sizeof(hop), "%ddB", (int)r.snr);
            const char *pc = shortPreset(r.preset);
            if (pc[0])
                snprintf(line2, sizeof(line2), "%s %s  heard %s", pc, hop, ago);
            else
                snprintf(line2, sizeof(line2), "%s  heard %s", hop, ago);
            d->drawString(x + 10, ry + lh, line2);
            if (isSel)
                d->setColor(WHITE);
        }
        if (filterRow)
            filter.draw(d, x, y);
        else
            drawCentered(d, x, y + d->height() - lh, "<>preset 7sweep 0find #hunt", FONT_SMALL);
    }

  private:
    NodeRow rows[MAXN];
    HeardNode hn[MAXN]; // scratch for the heard snapshot (lives in PSRAM with this instance)
    int vis[MAXN];
    int count = 0, vcount = 0, sel = 0, top = 0;
    bool bySignal = false, sweeping = false, handoff = false;
    int presetIdx = 0;
    uint32_t lastLoad = 0, sweepAt = 0;
    ListFilter filter;

    static int presetIndexOf(int preset)
    {
        for (int i = 0; i < meshPresetCount(); i++)
            if (meshPresetAt(i) == preset)
                return i;
        return 0;
    }
    void setPreset(int idx)
    {
        presetIdx = idx;
        meshListenPreset(meshPresetAt(idx)); // retune live; keep the nodes already gathered
    }
    static const char *shortPreset(uint8_t p)
    {
        switch (p) {
        case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW:
            return "LS";
        case meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE:
            return "LM";
        case meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST:
            return "LF";
        case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW:
            return "MS";
        case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST:
            return "MF";
        case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW:
            return "SS";
        case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST:
            return "SF";
        case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO:
            return "ST";
        default:
            return "";
        }
    }

    static uint32_t nowSecs() { return getValidTime(RTCQualityFromNet, true); }

    int findRow(uint32_t num)
    {
        for (int i = 0; i < count; i++)
            if (rows[i].num == num)
                return i;
        return -1;
    }
    void fillName(NodeRow &r)
    {
        meshNodeName(r.num, r.name, sizeof(r.name));
        char lower[40];
        snprintf(lower, sizeof(lower), "%s %08x", r.name, (unsigned)r.num);
        for (char *c = lower; *c; c++)
            *c = (char)tolower(*c);
        snprintf(r.hay, sizeof(r.hay), "%s", lower);
    }

    void load()
    {
        lastLoad = millis();
        count = 0;
        uint32_t me = meshMyNode(), now = millis();

        // 1) everything heard over the air this session (includes other-channel senders)
        int hnN = meshHeardList(hn, MAXN);
        for (int i = 0; i < hnN && count < MAXN; i++) {
            if (hn[i].num == me)
                continue;
            NodeRow &r = rows[count++];
            r.num = hn[i].num;
            r.ageSecs = (now - hn[i].lastMs) / 1000;
            r.snr = hn[i].snr;
            r.hops = hn[i].hops;
            r.other = !hn[i].decoded;
            r.chHash = hn[i].chHash;
            r.preset = hn[i].preset;
            meshtastic_NodeInfoLite *info = nodeDB ? nodeDB->getMeshNode(hn[i].num) : nullptr;
            r.mqtt = nodeInfoLiteViaMqtt(info);
            r.favorite = nodeInfoLiteIsFavorite(info);
            if (nodeInfoLiteHasUser(info))
                r.other = false; // we know who it is after all
            fillName(r);
        }

        // 2) nodes the badge knows but hasn't heard this session (saved, or via MQTT)
        if (nodeDB) {
            size_t n = nodeDB->getNumMeshNodes();
            for (size_t i = 0; i < n && count < MAXN; i++) {
                meshtastic_NodeInfoLite *info = nodeDB->getMeshNodeByIndex(i);
                if (!info || info->num == me || findRow(info->num) >= 0)
                    continue;
                NodeRow &r = rows[count++];
                r.num = info->num;
                r.ageSecs =
                    (nowSecs() && info->last_heard && nowSecs() > info->last_heard) ? nowSecs() - info->last_heard : 999999;
                r.snr = info->snr;
                r.hops = info->has_hops_away ? (int)info->hops_away : -1;
                r.mqtt = nodeInfoLiteViaMqtt(info);
                r.favorite = nodeInfoLiteIsFavorite(info);
                r.other = false;
                r.chHash = 0;
                r.preset = 0xFF;
                fillName(r);
            }
        }
        sort();
        rebuildVisible();
    }

    void sort()
    {
        if (bySignal) // closest first: direct before hops, stronger SNR; other-channel and MQTT last
            std::sort(rows, rows + count, [](const NodeRow &a, const NodeRow &b) {
                int ra = a.mqtt ? 99 : a.other ? 90 : a.hops < 0 ? 50 : a.hops;
                int rb = b.mqtt ? 99 : b.other ? 90 : b.hops < 0 ? 50 : b.hops;
                if (ra != rb)
                    return ra < rb;
                return a.snr > b.snr;
            });
        else // most recently heard first
            std::sort(rows, rows + count, [](const NodeRow &a, const NodeRow &b) { return a.ageSecs < b.ageSecs; });
    }

    void rebuildVisible()
    {
        vcount = 0;
        for (int i = 0; i < count; i++)
            if (filter.match(rows[i].hay))
                vis[vcount++] = i;
        if (sel >= vcount)
            sel = vcount ? vcount - 1 : 0;
        if (top > sel)
            top = sel;
    }
};

static NodeListApp &nodeListApp()
{
    static NodeListApp *p = psramNew<NodeListApp>();
    return *p;
}

void openNodeList()
{
    appOpen(&nodeListApp());
}

} // namespace badge

#include "configuration.h"

#include "BadgeMeshCoreSettings.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeHome.h"
#include "BadgeMeshCore.h"
#include "BadgeMeshSettings.h"

#include "graphics/Screen.h"
#include "main.h"
#include <Arduino.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace badge
{

enum McStep {
    MC_NONE,
    MC_MAIN,
    MC_CONTACTS,
    MC_CONTACT,
    MC_HEARD,
    MC_RADIO,
    MC_PRESET,
    MC_BW,
    MC_SF,
    MC_CR,
    MC_POWER,
};
static volatile McStep queued = MC_NONE;
static int mainSel = 1, contactsSel = 1, radioSel = 1;
static int contactIdx = 0; // the contact being looked at

static void queue(McStep s)
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
static void note(const char *text, uint32_t ms = 2500)
{
    if (screen)
        screen->showSimpleBanner(text, ms);
}

// ---- the radio --------------------------------------------------------------------------------
// MeshCore's regional presets for the badge's 902-928 MHz radio, from the MeshCore app's list
// (api.meshcore.nz/api/v1/config). "Badges" is the badge's own: what every badge's radio can do.
struct Preset {
    const char *name;
    float freq, bw;
    uint8_t sf, cr;
};
static const Preset PRESETS[] = {
    {"Badges (any badge)", 910.525f, 125.0f, 7, 5},
    {"USA / Canada", 910.525f, 62.5f, 7, 5},
    {"USA - S. California", 927.875f, 62.5f, 7, 5},
    {"Australia", 915.8f, 250.0f, 10, 5},
    {"Australia (Narrow)", 916.575f, 62.5f, 7, 7},
    {"Australia (Mid)", 915.075f, 125.0f, 9, 5},
    {"Australia: SA, WA", 923.125f, 62.5f, 8, 8},
    {"Australia: QLD", 923.125f, 62.5f, 8, 5},
    {"Brazil", 923.125f, 62.5f, 8, 8},
    {"New Zealand (Narrow)", 917.375f, 62.5f, 7, 5},
    {"Thailand", 921.0f, 62.5f, 7, 5},
    {"Vietnam (Narrow)", 920.25f, 62.5f, 8, 5},
};
static const int NPRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);

static void applyRadio(const McRadioCfg &c)
{
    if (meshcoreSetRadio(c)) {
        static char b[64];
        snprintf(b, sizeof(b), "MeshCore radio:\n%.3f MHz, %g kHz\nSF%d, 4/%d, %d dBm", (double)c.freq, (double)c.bw, c.sf, c.cr,
                 c.power);
        note(b, 3000);
    } else {
        note("This badge's radio\ncan't do that setting\n(the stock radio needs\n125 kHz or wider)", 4000);
    }
    queue(MC_RADIO);
}

static void showPreset()
{
    static const char *items[NPRESETS + 1];
    items[0] = "Back";
    McRadioCfg now = meshcoreRadio();
    int sel = 0;
    for (int i = 0; i < NPRESETS; i++) {
        items[i + 1] = PRESETS[i].name;
        if (PRESETS[i].freq == now.freq && PRESETS[i].bw == now.bw && PRESETS[i].sf == now.sf && PRESETS[i].cr == now.cr)
            sel = i + 1;
    }
    banner("MeshCore region\n(62.5 kHz ones need\nthe SX1262 mod)", items, NPRESETS + 1, sel, [](int s) -> void {
        if (s < 1 || s > NPRESETS) {
            queue(MC_RADIO);
            return;
        }
        McRadioCfg c = meshcoreRadio();
        const Preset &p = PRESETS[s - 1];
        c.freq = p.freq;
        c.bw = p.bw;
        c.sf = p.sf;
        c.cr = p.cr;
        applyRadio(c);
    });
}

static void freqDone(const char *t)
{
    if (!t) {
        queue(MC_RADIO);
        return;
    }
    float f = (float)atof(t);
    if (f < 902.0f || f > 928.0f) {
        note("Between 902 and 928 MHz\n(type it like 910.525)", 3500);
        queue(MC_RADIO);
        return;
    }
    McRadioCfg c = meshcoreRadio();
    c.freq = f;
    applyRadio(c);
}

static const float BWS[] = {62.5f, 125.0f, 250.0f, 500.0f};
static void showBw()
{
    static const char *items[] = {"Back", "62.5 kHz", "125 kHz", "250 kHz", "500 kHz"};
    float now = meshcoreRadio().bw;
    int sel = 0;
    for (int i = 0; i < 4; i++)
        if (BWS[i] == now)
            sel = i + 1;
    banner("Bandwidth", items, 5, sel, [](int s) -> void {
        if (s < 1 || s > 4) {
            queue(MC_RADIO);
            return;
        }
        McRadioCfg c = meshcoreRadio();
        c.bw = BWS[s - 1];
        applyRadio(c);
    });
}
static void showSf()
{
    static const char *items[] = {"Back", "SF5", "SF6", "SF7", "SF8", "SF9", "SF10", "SF11", "SF12"};
    int sf = meshcoreRadio().sf;
    banner("Spreading factor", items, 9, sf >= 5 && sf <= 12 ? sf - 4 : 0, [](int s) -> void {
        if (s < 1 || s > 8) {
            queue(MC_RADIO);
            return;
        }
        McRadioCfg c = meshcoreRadio();
        c.sf = (uint8_t)(s + 4);
        applyRadio(c);
    });
}
static void showCr()
{
    static const char *items[] = {"Back", "4/5", "4/6", "4/7", "4/8"};
    int cr = meshcoreRadio().cr;
    banner("Coding rate", items, 5, cr >= 5 && cr <= 8 ? cr - 4 : 0, [](int s) -> void {
        if (s < 1 || s > 4) {
            queue(MC_RADIO);
            return;
        }
        McRadioCfg c = meshcoreRadio();
        c.cr = (uint8_t)(s + 4);
        applyRadio(c);
    });
}
static const int8_t POWERS[] = {22, 20, 17, 14, 10, 5, 2};
static void showPower()
{
    static const char *items[] = {"Back", "22 dBm (most)", "20 dBm", "17 dBm", "14 dBm", "10 dBm", "5 dBm", "2 dBm"};
    int now = meshcoreRadio().power, sel = 0;
    for (int i = 0; i < 7; i++)
        if (POWERS[i] == now)
            sel = i + 1;
    banner("Transmit power", items, 8, sel, [](int s) -> void {
        if (s < 1 || s > 7) {
            queue(MC_RADIO);
            return;
        }
        McRadioCfg c = meshcoreRadio();
        c.power = POWERS[s - 1];
        applyRadio(c);
    });
}

static void showRadio()
{
    static char lbl[6][40];
    McRadioCfg c = meshcoreRadio();
    snprintf(lbl[0], 40, "Region preset...");
    snprintf(lbl[1], 40, "Frequency: %.3f MHz", (double)c.freq);
    snprintf(lbl[2], 40, "Bandwidth: %g kHz", (double)c.bw);
    snprintf(lbl[3], 40, "Spreading factor: %d", c.sf);
    snprintf(lbl[4], 40, "Coding rate: 4/%d", c.cr);
    snprintf(lbl[5], 40, "Transmit power: %d dBm", c.power);
    static const char *items[7];
    items[0] = "Back";
    for (int i = 0; i < 6; i++)
        items[i + 1] = lbl[i];
    banner("MeshCore radio\n(everyone on a mesh\nuses the same setting)", items, 7, radioSel, [](int s) -> void {
        if (s < 1 || s > 6) {
            queue(MC_MAIN);
            return;
        }
        radioSel = s;
        static char f[16];
        switch (s) {
        case 1:
            queue(MC_PRESET);
            break;
        case 2:
            snprintf(f, sizeof(f), "%.3f", (double)meshcoreRadio().freq);
            openTextEditor("Frequency (MHz)", f, 8, 0, freqDone);
            break;
        case 3:
            queue(MC_BW);
            break;
        case 4:
            queue(MC_SF);
            break;
        case 5:
            queue(MC_CR);
            break;
        default:
            queue(MC_POWER);
            break;
        }
    });
}

// ---- contacts ---------------------------------------------------------------------------------
static const int MAX_SHOWN = 40;
static const char *typeName(uint8_t type)
{
    return type == 2 ? "repeater" : type == 3 ? "room" : type == 4 ? "sensor" : "person";
}

static void showContacts()
{
    static char lbl[MAX_SHOWN + 1][44];
    static const char *items[MAX_SHOWN + 2];
    static int nShown;
    int n = meshcoreContactCount(), heard = meshcoreHeardCount();
    if (n > MAX_SHOWN)
        n = MAX_SHOWN;
    nShown = n;
    items[0] = "Back";
    McContact c;
    for (int i = 0; i < n; i++) {
        if (!meshcoreContact(i, c))
            snprintf(lbl[i], 44, "?");
        else if (c.type == 1 || c.type == 0)
            snprintf(lbl[i], 44, "%s%s", c.favourite ? "* " : "", c.name);
        else
            snprintf(lbl[i], 44, "%s%s (%s)", c.favourite ? "* " : "", c.name, typeName(c.type));
        items[i + 1] = lbl[i];
    }
    int count = n + 1;
    if (heard) {
        snprintf(lbl[MAX_SHOWN], 44, "+ Add someone heard (%d)", heard);
        items[count++] = lbl[MAX_SHOWN];
    }
    static char title[48];
    snprintf(title, sizeof(title),
             n ? "MeshCore contacts (%d)" : "No contacts yet:\nthey appear when another\nMeshCore node announces", n);
    if (contactsSel >= count)
        contactsSel = count - 1;
    banner(title, items, count, contactsSel, [](int s) -> void {
        if (s < 1) {
            queue(MC_MAIN);
            return;
        }
        contactsSel = s;
        if (s > nShown) {
            queue(MC_HEARD);
            return;
        }
        contactIdx = s - 1;
        queue(MC_CONTACT);
    });
}

static void showContact()
{
    McContact c;
    if (!meshcoreContact(contactIdx, c)) {
        queue(MC_CONTACTS);
        return;
    }
    static char title[96], fav[24];
    if (c.hops < 0)
        snprintf(title, sizeof(title), "%s\n%s\nno route yet: messages\nare relayed by everyone", c.name, typeName(c.type));
    else if (c.hops == 0)
        snprintf(title, sizeof(title), "%s\n%s\nin direct range", c.name, typeName(c.type));
    else
        snprintf(title, sizeof(title), "%s\n%s\nroute: %d relay%s", c.name, typeName(c.type), c.hops, c.hops == 1 ? "" : "s");
    snprintf(fav, sizeof(fav), "Favourite: %s", c.favourite ? "yes" : "no");
    static const char *items[] = {"Back", fav, "Forget the route", "Delete this contact"};
    banner(title, items, 4, 0, [](int s) -> void {
        McContact c;
        switch (s) {
        case 1:
            if (meshcoreContact(contactIdx, c))
                meshcoreContactFav(contactIdx, !c.favourite);
            queue(MC_CONTACT);
            break;
        case 2:
            meshcoreContactResetPath(contactIdx);
            note("Route forgotten: the next\nmessage finds a new one");
            queue(MC_CONTACT);
            break;
        case 3:
            note(meshcoreContactDelete(contactIdx) ? "Contact deleted" : "Couldn't delete it");
            contactsSel = 1;
            queue(MC_CONTACTS);
            break;
        default:
            queue(MC_CONTACTS);
            break;
        }
    });
}

static void showHeard()
{
    static const char *items[13];
    static char lbl[12][36];
    int n = meshcoreHeardCount();
    if (n > 12)
        n = 12;
    if (!n) {
        queue(MC_CONTACTS);
        return;
    }
    items[0] = "Back";
    for (int i = 0; i < n; i++) {
        snprintf(lbl[i], 36, "%s", meshcoreHeardName(i));
        items[i + 1] = lbl[i];
    }
    banner("Heard announcing:\npick one to add", items, n + 1, 0, [](int s) -> void {
        if (s >= 1)
            note(meshcoreHeardAdd(s - 1) ? "Added to your contacts" : "Couldn't add it\n(contacts full?)");
        queue(MC_CONTACTS);
    });
}

// ---- hashtag channels ---------------------------------------------------------------------------
static void hashtagDone(const char *t)
{
    if (!t || !t[0]) {
        queue(MC_MAIN);
        return;
    }
    // MeshCore hashtag channels are lower case, and their key is made from "#name"
    char name[12];
    size_t n = 0;
    for (const char *c = t[0] == '#' ? t + 1 : t; *c && n < sizeof(name) - 1; c++)
        if (!isspace((unsigned char)*c))
            name[n++] = (char)tolower((unsigned char)*c);
    name[n] = 0;
    if (!n) {
        queue(MC_MAIN);
        return;
    }
    uint8_t key[16];
    meshcoreHashtagKey(name, key);
    const char *why = nullptr;
    int idx = meshJoinWithKey(name, key, sizeof(key), &why);
    static char b[72];
    if (idx >= 0)
        snprintf(b, sizeof(b), "Joined #%s\nIt's in Messages now", name);
    else
        snprintf(b, sizeof(b), "%s", why ? why : "Couldn't join it\n(8 channels at most)");
    note(b, 3500);
    queue(MC_MAIN);
}

// ---- the menu -----------------------------------------------------------------------------------
static void showMain()
{
    static char lbl[4][44];
    McRadioCfg c = meshcoreRadio();
    snprintf(lbl[0], 44, "Contacts (%d)", meshcoreContactCount());
    snprintf(lbl[1], 44, "Radio: %.3f / %g kHz / SF%d", (double)c.freq, (double)c.bw, c.sf);
    snprintf(lbl[2], 44, "Add new contacts by itself: %s", meshcoreAutoAdd() ? "yes" : "no");
    snprintf(lbl[3], 44, "Share my location: %s", meshcoreShareLocation() ? "yes" : "no");
    static const char *items[] = {
        "Back", "Announce to badges nearby", "Announce to the whole mesh", lbl[0], "Join a #hashtag channel", lbl[1], lbl[2],
        lbl[3]};
    banner("MeshCore settings", items, 8, mainSel, [](int s) -> void {
        if (s < 1 || s > 7)
            return;
        mainSel = s;
        switch (s) {
        case 1:
            meshcoreAdvertise(false);
            note("Announced to badges\nin range");
            queue(MC_MAIN);
            break;
        case 2:
            meshcoreAdvertise(true);
            note("Announced: repeaters\npass it across the mesh");
            queue(MC_MAIN);
            break;
        case 3:
            contactsSel = 1;
            queue(MC_CONTACTS);
            break;
        case 4:
            openTextEditor("Hashtag channel", "", 11, 0, hashtagDone);
            break;
        case 5:
            radioSel = 1;
            queue(MC_RADIO);
            break;
        case 6:
            meshcoreSetAutoAdd(!meshcoreAutoAdd());
            queue(MC_MAIN);
            break;
        default:
            meshcoreSetShareLocation(!meshcoreShareLocation());
            if (meshcoreShareLocation())
                note("Announcements carry the\nposition set in Mesh\nsettings (none: nothing)", 3500);
            queue(MC_MAIN);
            break;
        }
    });
}

void mcSettingsOpen()
{
    mainSel = 1;
    queue(MC_MAIN);
}
bool mcSettingsBusy()
{
    return queued != MC_NONE;
}
void mcSettingsPoll()
{
    McStep s = queued;
    if (s == MC_NONE || !screen || (appActive() && !homeActive()) || screen->isOverlayBannerShowing())
        return;
    queued = MC_NONE;
    switch (s) {
    case MC_MAIN:
        showMain();
        break;
    case MC_CONTACTS:
        showContacts();
        break;
    case MC_CONTACT:
        showContact();
        break;
    case MC_HEARD:
        showHeard();
        break;
    case MC_RADIO:
        showRadio();
        break;
    case MC_PRESET:
        showPreset();
        break;
    case MC_BW:
        showBw();
        break;
    case MC_SF:
        showSf();
        break;
    case MC_CR:
        showCr();
        break;
    case MC_POWER:
        showPower();
        break;
    default:
        break;
    }
}

} // namespace badge

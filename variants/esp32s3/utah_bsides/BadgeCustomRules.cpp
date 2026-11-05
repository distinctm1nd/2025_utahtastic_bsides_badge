// Your own rules, added on the Badge WiFi page and used straight away (no rebuild): kept in
// /badge/rules.txt, one per line: kind|pattern|title|mark. Kinds: btname (Bluetooth name),
// wifiname (network name), maker (the maker's name from the address / Bluetooth company),
// address (the first part of the address, "AA:BB:CC"). Patterns match anywhere, ignoring case.
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeRules.h"
#include "BadgeVendors.h"

#include "FSCommon.h"
#include "SPILock.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

namespace badge
{

static const char *FILE_PATH = "/badge/rules.txt";
static CustomRule *rules = nullptr; // made in PSRAM on first use
static int count = -1;

struct MarkInfo {
    const char *code, *tag;
    bool alert;
};
static const MarkInfo MARKS[] = {
    {"SURV", "Surveillance", true}, {"POL", "Tactical", true},  {"CAM", "Camera", true},
    {"TAG", "Tracker", true},       {"SKIM", "Skimmer", true},  {"ATK", "Attack", true},
    {"DRON", "Drone", true},        {"HOME", "Smart Home", false}, {"NOTE", "Note", false},
};

static const MarkInfo *markInfo(const char *code)
{
    for (const auto &m : MARKS)
        if (!strcmp(m.code, code))
            return &m;
    return nullptr;
}

static int kindOf(const char *k)
{
    static const char *const K[] = {"btname", "wifiname", "maker", "address"};
    for (int i = 0; i < 4; i++)
        if (!strcmp(K[i], k))
            return i;
    return -1;
}

static bool containsNoCase(const char *hay, const char *needle)
{
    if (!hay || !*needle)
        return false;
    size_t n = strlen(needle);
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i] && tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == n)
            return true;
    }
    return false;
}

static void load()
{
    if (count >= 0)
        return;
    count = 0;
    if (!rules)
        rules = psramArray<CustomRule>(MAX_CUSTOM_RULES);
    if (!rules)
        return;
    concurrency::LockGuard g(spiLock);
    File f = FSCom.open(FILE_PATH, FILE_O_READ);
    if (!f)
        return;
    char line[128];
    while (f.available() && count < MAX_CUSTOM_RULES) {
        int n = f.readBytesUntil('\n', line, sizeof(line) - 1);
        line[n] = 0;
        char *parts[4] = {line, nullptr, nullptr, nullptr};
        for (int i = 1; i < 4; i++) {
            char *bar = parts[i - 1] ? strchr(parts[i - 1], '|') : nullptr;
            if (!bar)
                break;
            *bar = 0;
            parts[i] = bar + 1;
        }
        if (!parts[3] || kindOf(parts[0]) < 0 || !markInfo(parts[3]) || !parts[1][0])
            continue;
        CustomRule &r = rules[count++];
        r.kind = (uint8_t)kindOf(parts[0]);
        snprintf(r.pattern, sizeof(r.pattern), "%s", parts[1]);
        snprintf(r.title, sizeof(r.title), "%s", parts[2][0] ? parts[2] : parts[1]);
        snprintf(r.code, sizeof(r.code), "%s", parts[3]);
    }
    f.close();
}

static void save()
{
    concurrency::LockGuard g(spiLock);
    if (!FSCom.exists("/badge"))
        FSCom.mkdir("/badge");
    File f = FSCom.open(FILE_PATH, FILE_O_WRITE);
    if (!f)
        return;
    static const char *const K[] = {"btname", "wifiname", "maker", "address"};
    for (int i = 0; i < count; i++) {
        char line[128];
        snprintf(line, sizeof(line), "%s|%s|%s|%s\n", K[rules[i].kind], rules[i].pattern, rules[i].title, rules[i].code);
        f.print(line);
    }
    f.close();
}

int customRuleCount()
{
    load();
    return count;
}

const CustomRule &customRuleAt(int i)
{
    load();
    return rules[i < 0 ? 0 : i >= count ? count - 1 : i];
}

const char *customRuleAdd(const char *kind, const char *pattern, const char *title, const char *code)
{
    load();
    int k = kindOf(kind ? kind : "");
    if (k < 0)
        return "Pick what to match";
    if (!pattern || !*pattern || strlen(pattern) > sizeof(rules[0].pattern) - 1)
        return "The pattern is empty or too long (40 characters at most)";
    if (strchr(pattern, '|') || (title && strchr(title, '|')))
        return "No | in the pattern or title";
    if (!code || !markInfo(code))
        return "Pick a mark";
    if (!rules)
        return "Out of memory";
    if (count >= MAX_CUSTOM_RULES)
        return "32 rules already: delete one first";
    CustomRule &r = rules[count++];
    r.kind = (uint8_t)k;
    snprintf(r.pattern, sizeof(r.pattern), "%s", pattern);
    snprintf(r.title, sizeof(r.title), "%s", title && *title ? title : pattern);
    snprintf(r.code, sizeof(r.code), "%s", code);
    save();
    return nullptr;
}

bool customRuleDelete(int i)
{
    load();
    if (i < 0 || i >= count)
        return false;
    memmove(&rules[i], &rules[i + 1], sizeof(CustomRule) * (count - i - 1));
    count--;
    save();
    return true;
}

bool customRuleMatch(const ScanEntry &e, bool wifi, RuleHit &out)
{
    load();
    for (int i = 0; i < count; i++) {
        const CustomRule &r = rules[i];
        bool hit = false;
        switch (r.kind) {
        case 0: hit = !wifi && containsNoCase(e.name, r.pattern); break;
        case 1: hit = wifi && containsNoCase(e.name, r.pattern); break;
        case 2:
            hit = (e.publicAddr && containsNoCase(vendorFromAddress(e.mac), r.pattern)) ||
                  (!wifi && e.company != NO_COMPANY && containsNoCase(vendorFromCompanyId(e.company), r.pattern));
            break;
        case 3: {
            char a[18];
            snprintf(a, sizeof(a), "%02X:%02X:%02X:%02X:%02X:%02X", e.mac[0], e.mac[1], e.mac[2], e.mac[3], e.mac[4], e.mac[5]);
            hit = !strncasecmp(a, r.pattern, strlen(r.pattern)); // the address starts with it
            break;
        }
        }
        if (hit) {
            const MarkInfo *m = markInfo(r.code);
            out = {r.title, m->tag, m->code, m->alert};
            return true;
        }
    }
    return false;
}

} // namespace badge

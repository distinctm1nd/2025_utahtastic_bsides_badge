#include "BadgeRules.h"

#include "generated/BadgeRulesData.h"
#include <ctype.h>

namespace badge
{

// whole-name match, ignoring case: '.' any letter, ".*" anything, "\d" a digit, '\x' a literal x
static bool patternMatch(const char *p, const char *s)
{
    while (*p) {
        if (p[0] == '.' && p[1] == '*') {
            p += 2;
            for (;; s++) {
                if (patternMatch(p, s))
                    return true;
                if (!*s)
                    return false;
            }
        }
        if (!*s)
            return false;
        if (p[0] == '\\' && p[1] == 'd') {
            if (!isdigit((unsigned char)*s))
                return false;
            p += 2;
        } else if (p[0] == '\\' && p[1]) {
            if (tolower((unsigned char)*s) != tolower((unsigned char)p[1]))
                return false;
            p += 2;
        } else if (p[0] == '.') {
            p++;
        } else {
            if (tolower((unsigned char)*s) != tolower((unsigned char)*p))
                return false;
            p++;
        }
        s++;
    }
    return !*s;
}

// lowest rule index among table entries whose key (the bits above the low 8) is key
static int lookup(const uint32_t *t, int n, uint32_t key, uint8_t types)
{
    int lo = 0, hi = n;
    while (lo < hi) { // first entry with that key; rules within a key are sorted, alerts first
        int mid = (lo + hi) / 2;
        if ((t[mid] >> 8) < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    for (; lo < n && (t[lo] >> 8) == key; lo++)
        if (RULES[t[lo] & 0xFF].types & types)
            return t[lo] & 0xFF;
    return -1;
}

bool ruleMatch(const ScanEntry &e, bool wifi, RuleHit &out)
{
    // things the scanner itself spots come first
    if (e.flags & SF_DRONE) {
        out = {"Drone (Remote ID)", "Drone", "DRON", true};
        return true;
    }
    // trackers: the ones away from their owner alert; the ones whose owner is nearby (your own
    // AirTag, a friend's) are only labelled
    if (!wifi && (e.flags & SF_FMDN)) {
        if (e.flags & SF_AWAY)
            out = {"Android-network tracker, away from its owner", "Tracker", "TAG", true};
        else
            out = {"Android-network tracker (owner nearby)", "Tracker", "TAG", false};
        return true;
    }
    if (!wifi && (e.flags & SF_FINDMY)) {
        if (e.flags & SF_AWAY)
            out = {"Find My tracker, away from its owner", "Tracker", "TAG", true};
        else
            out = {"Find My tracker (owner nearby)", "Tracker", "TAG", false};
        return true;
    }
    if (wifi && (e.flags & SF_MANYNAMES)) {
        out = {"One radio, many network names (Pineapple?)", "Attack", "ATK", true};
        return true;
    }
    if (wifi && (e.flags & SF_TWIN)) {
        out = {"Same name open and protected (evil twin?)", "Attack", "TWIN", true};
        return true;
    }
    if (customRuleMatch(e, wifi, out)) // yours win over the built-in rule files
        return true;
    uint8_t types = wifi ? 2 : 1;
    int best = -1;
    auto take = [&](int r) {
        if (r >= 0 && (best < 0 || r < best))
            best = r;
    };
    if (e.publicAddr)
        take(lookup(RULE_OUI, RULE_OUI_COUNT, ((uint32_t)e.mac[0] << 16) | (e.mac[1] << 8) | e.mac[2], types));
    if (!wifi && e.company != NO_COMPANY) {
        take(lookup(RULE_MFG, RULE_MFG_COUNT, e.company, types));
        if (e.mfgByte >= 0)
            take(lookup(RULE_MFG_DATA, RULE_MFG_DATA_COUNT, ((uint32_t)e.company << 8) | e.mfgByte, types));
    }
    for (int i = 0; !wifi && i < e.nServices; i++)
        take(lookup(RULE_SERVICE, RULE_SERVICE_COUNT, e.services[i], types));
    if (e.name[0])
        for (int i = 0; i < RULE_PATTERN_COUNT; i++) {
            const RulePattern &p = RULE_PATTERNS[i];
            if (p.field == (wifi ? 1 : 0) && (RULES[p.rule].types & types) && (best < 0 || p.rule < best) &&
                patternMatch(p.pattern, e.name))
                take(p.rule);
        }
    if (best < 0)
        return false;
    out = {RULES[best].title, RULES[best].tag, RULES[best].code, RULES[best].alert};
    return true;
}

} // namespace badge

// Fox hunt rules (tools/data/rules, compiled by tools/make_vendors.py): what a WiFi or
// Bluetooth device is likely to be, e.g. "Flock plate reader" or "Tile tracker".
#pragma once
#include "BadgeRadioScan.h"

namespace badge
{

struct RuleHit {
    const char *title; // "Flock plate reader"
    const char *tag;   // "Surveillance"
    const char *code;  // "SURV", for the box on screen
    bool alert;        // surveillance, police, camera or tracker: highlighted
};

// the best rule this device meets (alert rules first); false if none
bool ruleMatch(const ScanEntry &e, bool wifi, RuleHit &out);

// your own rules (BadgeCustomRules.cpp), added on the Badge WiFi page; checked before the built-in
// rule files. kind: 0 Bluetooth name, 1 WiFi network name, 2 maker name, 3 address start
static const int MAX_CUSTOM_RULES = 32;
struct CustomRule {
    uint8_t kind;
    char pattern[41];
    char title[41];
    char code[5]; // SURV POL CAM TAG SKIM ATK DRON (alert) or HOME NOTE (shown only)
};
int customRuleCount();
const CustomRule &customRuleAt(int i);
// kind: "btname", "wifiname", "maker" or "address"; returns why not, or nullptr
const char *customRuleAdd(const char *kind, const char *pattern, const char *title, const char *code);
bool customRuleDelete(int i);
bool customRuleMatch(const ScanEntry &e, bool wifi, RuleHit &out);

} // namespace badge

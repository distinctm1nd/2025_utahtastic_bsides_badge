#!/usr/bin/env python3
"""Maker names for the WiFi / Bluetooth fox hunt: the IEEE address-prefix (OUI) list and the
Bluetooth SIG company identifiers, packed into a table the firmware looks names up in.

Before each PlatformIO build (extra_scripts in platformio.ini) this reads tools/data/oui.csv
and tools/data/bt_companies.csv and writes generated/vendors.bin plus generated/vendors.S,
which embeds it (both are build output, not committed).

Refresh the lists (IEEE blocks scripted downloads, so download oui.txt in a browser from
https://standards-oui.ieee.org/oui/oui.txt; the SIG list can be a file or the URL):

  python3 make_vendors.py --update path/to/oui.txt \\
      https://bitbucket.org/bluetooth-SIG/public/raw/main/assigned_numbers/company_identifiers/company_identifiers.yaml

It also compiles the fox hunt rules (tools/data/rules/*.json, plus tools/data/rules.local/*.json
if present; format in tools/data/rules/README.md) into generated/BadgeRulesData.h, resolving
company-name patterns against the two lists.

Table layout (little-endian): "BVN3", u32 OUI count, u32 company count, then OUI records
(3-byte prefix big-endian, 3-byte name offset), company records (u16 ID, 3-byte name offset),
then the names, each ending in a zero byte. Both record lists are sorted for binary search.
"""
import csv
import hashlib
import os
import re
import struct
import sys
import urllib.request

VERSION = "3"
MAX_NAME = 24

IN_PIO = False
try:  # running inside PlatformIO
    Import("env")  # noqa: F821
    IN_PIO = True
    HERE = os.path.join(env.subst("$PROJECT_DIR"), "variants", "esp32s3", "utah_bsides")  # noqa: F821
except NameError:
    HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

DATA = os.path.join(HERE, "tools", "data")
OUI_CSV = os.path.join(DATA, "oui.csv")
SIG_CSV = os.path.join(DATA, "bt_companies.csv")
RULE_DIRS = [os.path.join(DATA, "rules"), os.path.join(DATA, "rules.local")]
ALERT_TAGS = ("Surveillance", "Tactical", "Camera", "Tracker", "Attack", "Skimmer", "Drone")
TAG_CODES = {"Surveillance": "SURV", "Tactical": "POL", "Camera": "CAM", "Tracker": "TAG", "Networking": "NET",
             "Defense": "DEF", "Smart Home": "HOME", "Audio": "AUD", "Wearable": "WEAR",
             "Attack": "ATK", "Hacking tool": "HACK", "Skimmer": "SKIM", "Drone": "DRON"}
PATTERN_OK = re.compile(r"(?:\\[d.\-_ ]|\.\*|\.|[^\\.*+?()\[\]{}|^$])*")
GEN = os.path.join(HERE, "generated")
BIN = os.path.join(GEN, "vendors.bin")
ASM = os.path.join(GEN, "vendors.S")
RULES_H = os.path.join(GEN, "BadgeRulesData.h")

SUFFIX = re.compile(
    r"[\s,.]*\b(co|company|corp|corporation|inc|incorporated|llc|ltd|limited|gmbh|ag|s\.?a|b\.?v|"
    r"pty|plc|srl|s\.?r\.?l|oy|ab|as|kg|sas|spa|s\.?p\.?a|bhd|sdn|kk|k\.?k|pte|lda)\b\.?[\s,.]*$",
    re.I,
)


def log(*a):
    print("[badge vendors]", *a)


def short(name):
    n = re.sub(r"\([^)]*\)", " ", name)
    n = re.sub(r"\s+", " ", n).strip(" ,.")
    for _ in range(4):  # "Co., Ltd." takes two passes
        m = SUFFIX.sub("", n).strip(" ,.")
        if not m or m == n:
            break
        n = m
    if len(n) > MAX_NAME:
        cut = n[:MAX_NAME].rsplit(" ", 1)[0]
        n = cut if len(cut) >= 8 else n[:MAX_NAME]
    return n.strip(" ,.-&") or name[:MAX_NAME]


def read_source(src):
    if re.match(r"https?://", src):
        req = urllib.request.Request(src, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req) as r:
            return r.read().decode("utf-8", "replace")
    with open(src, encoding="utf-8", errors="replace") as fh:
        return fh.read()


def update(oui_src, sig_src):
    rows = []
    for line in read_source(oui_src).splitlines():
        m = re.match(r"^([0-9A-F]{6})\s+\(base 16\)\s+(.*)$", line.strip())
        if m and m.group(2).strip() and m.group(2).strip() not in ("IEEE Registration Authority", "Private"):
            rows.append((m.group(1), m.group(2).strip()))
    if len(rows) < 10000:
        sys.exit("that doesn't look like IEEE's oui.txt (%d entries)" % len(rows))
    rows.sort()
    sig = []
    text = read_source(sig_src)
    for m in re.finditer(r"value:\s*0x([0-9A-Fa-f]{1,4})\s*\n\s*name:\s*'((?:[^']|'')*)'", text):
        sig.append((int(m.group(1), 16), m.group(2).replace("''", "'")))
    if len(sig) < 1000:
        sys.exit("that doesn't look like the SIG company_identifiers.yaml (%d entries)" % len(sig))
    sig.sort()
    os.makedirs(DATA, exist_ok=True)
    with open(OUI_CSV, "w", newline="", encoding="utf-8") as fh:
        csv.writer(fh).writerows(rows)
    with open(SIG_CSV, "w", newline="", encoding="utf-8") as fh:
        csv.writer(fh).writerows(("0x%04X" % i, n) for i, n in sig)
    log("%d address prefixes, %d Bluetooth company IDs" % (len(rows), len(sig)))


def build_table():
    names, offsets = bytearray(), {}

    def name_at(full):
        s = short(full)
        if s not in offsets:
            offsets[s] = len(names)
            names.extend(s.encode("utf-8", "replace") + b"\0")
        return offsets[s]

    oui, sig = [], []
    with open(OUI_CSV, encoding="utf-8") as fh:
        for prefix, full in csv.reader(fh):
            oui.append((int(prefix, 16), name_at(full)))
    with open(SIG_CSV, encoding="utf-8") as fh:
        for ident, full in csv.reader(fh):
            sig.append((int(ident, 16), name_at(full)))
    oui = sorted(dict(oui).items())
    sig = sorted(dict(sig).items())
    out = bytearray(b"BVN3" + struct.pack("<II", len(oui), len(sig)))
    for p, off in oui:
        out += p.to_bytes(3, "big") + off.to_bytes(3, "little")
    for i, off in sig:
        out += struct.pack("<H", i) + off.to_bytes(3, "little")
    return bytes(out + names), len(oui), len(sig)


def rule_files():
    files = []
    for d in RULE_DIRS:
        if os.path.isdir(d):
            files += sorted(os.path.join(d, f) for f in os.listdir(d) if f.endswith(".json"))
    return files


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def compile_rules():
    """Rules -> generated/BadgeRulesData.h. Returns a summary for the log."""
    import json

    def read_csv(path):
        with open(path, encoding="utf-8") as fh:
            return list(csv.reader(fh))

    ouis = read_csv(OUI_CSV) if os.path.exists(OUI_CSV) else []
    sigs = read_csv(SIG_CSV) if os.path.exists(SIG_CSV) else []
    types = {"ble": 1, "wifi": 2, "auto": 3}
    rules = []
    for path in rule_files():
        try:
            with open(path, encoding="utf-8") as fh:
                r = json.load(fh)
        except (OSError, ValueError) as e:
            log("skipping %s: %s" % (os.path.basename(path), e))
            continue
        tags = r.get("tag") or ["Other"]
        tag = next((t for t in tags if t in ALERT_TAGS), tags[0])
        rules.append({"file": os.path.basename(path), "title": (r.get("title") or r.get("name") or "?")[:40],
                      "tag": tag, "alert": tag in ALERT_TAGS, "type": types.get(r.get("type", "auto"), 3),
                      "criteria": r.get("criteria", [])})
    rules.sort(key=lambda r: (not r["alert"], r["file"]))  # alerts win when two rules match
    if len(rules) > 255:
        sys.exit("too many rules (255 at most)")

    oui_pairs, mfg_pairs, data_pairs, svc_pairs, pats = set(), set(), set(), set(), []
    for i, r in enumerate(rules):
        n_oui = n_mfg = n_pat = 0
        for c in r["criteria"]:
            for key, vals in c.items():
                for v in vals or []:
                    v = str(v).strip()
                    if key == "oui":
                        oui_pairs.add((int(v.replace(":", "").replace("-", ""), 16), i)); n_oui += 1
                    elif key == "oui_org":
                        p = re.compile(v, re.I)
                        for prefix, full in ouis:
                            if p.search(full):
                                oui_pairs.add((int(prefix, 16), i)); n_oui += 1
                    elif key == "mfg":
                        mfg_pairs.add((int(v, 16), i)); n_mfg += 1
                    elif key == "mfg_org":
                        p = re.compile(v, re.I)
                        for ident, full in sigs:
                            if p.search(full):
                                mfg_pairs.add((int(ident, 16), i)); n_mfg += 1
                    elif key == "mfg_data":
                        cid, byte = v.split(":")
                        data_pairs.add((int(cid, 16), int(byte, 16), i))
                    elif key == "service":
                        svc_pairs.add((int(v, 16), i))
                    elif key in ("name", "ssid"):
                        if not PATTERN_OK.fullmatch(v):
                            log("%s: pattern %r uses more than . .* \\d - skipped" % (r["file"], v))
                            continue
                        pats.append((i, 0 if key == "name" else 1, v.lower())); n_pat += 1
                    else:
                        log("%s: %s isn't used by the badge" % (r["file"], key))
        note = "  <- broad, expect false alarms" if n_oui > 400 else ""
        log("rule %-24s %4d prefixes %3d IDs %3d names%s" % (r["file"], n_oui, n_mfg, n_pat, note))

    L = ["// generated by tools/make_vendors.py from tools/data/rules - don't edit", "#pragma once",
         "#include <stdint.h>", "namespace badge", "{",
         "struct RuleInfo {", "    const char *title, *tag, *code;", "    uint8_t types; // 1 Bluetooth, 2 WiFi",
         "    bool alert;", "};", "struct RulePattern {", "    uint8_t rule, field; // field 0 Bluetooth name, 1 WiFi name",
         "    const char *pattern;", "};", "static const RuleInfo RULES[] = {"]
    for r in rules:
        code = TAG_CODES.get(r["tag"], r["tag"][:4].upper())
        L.append("    {%s, %s, %s, %d, %s}," % (c_str(r["title"]), c_str(r["tag"]), c_str(code), r["type"],
                                              "true" if r["alert"] else "false"))
    if not rules:
        L.append('    {"", "", "", 0, false},')
    L.append("};")
    L.append("static const int RULE_COUNT = %d;" % len(rules))

    def table(name, rows):
        L.append("static const uint32_t %s[] = {" % name)
        for j in range(0, len(rows), 8):
            L.append("    " + ", ".join("0x%08xu" % v for v in rows[j:j + 8]) + ",")
        if not rows:
            L.append("    0xffffffffu,")
        L.append("};")
        L.append("static const int %s_COUNT = %d;" % (name, len(rows)))

    table("RULE_OUI", sorted((o << 8) | i for o, i in oui_pairs))  # prefix, rule
    table("RULE_MFG", sorted((m << 8) | i for m, i in mfg_pairs))  # company ID, rule
    table("RULE_MFG_DATA", sorted((m << 16) | (b << 8) | i for m, b, i in data_pairs))
    table("RULE_SERVICE", sorted((u << 8) | i for u, i in svc_pairs))
    L.append("static const RulePattern RULE_PATTERNS[] = {")
    for i, f, p in pats:
        L.append("    {%d, %d, %s}," % (i, f, c_str(p)))
    if not pats:
        L.append('    {255, 255, ""},')
    L.append("};")
    L.append("static const int RULE_PATTERN_COUNT = %d;" % len(pats))
    L.append("} // namespace badge")
    with open(RULES_H, "w") as fh:
        fh.write("\n".join(L) + "\n")
    return "%d rules" % len(rules)


def fingerprint():
    h = hashlib.sha1((VERSION + BIN).encode())
    for p in [OUI_CSV, SIG_CSV] + rule_files() + [__file__ if "__file__" in globals() else ""]:
        if p and os.path.exists(p):
            with open(p, "rb") as fh:
                h.update(fh.read())
    return h.hexdigest()


def generate():
    fp = fingerprint()
    try:
        with open(ASM) as fh:
            if fp in fh.readline():
                return
    except OSError:
        pass
    os.makedirs(GEN, exist_ok=True)
    if os.path.exists(OUI_CSV) and os.path.exists(SIG_CSV):
        data, n_oui, n_sig = build_table()
        log("%d address prefixes, %d Bluetooth company IDs, %d KB" % (n_oui, n_sig, len(data) // 1024))
    else:
        data = b"BVN3" + struct.pack("<II", 0, 0)
        log("no lists in tools/data; building without maker names")
    with open(BIN, "wb") as fh:
        fh.write(data)
    log(compile_rules())
    path = BIN.replace("\\", "/")
    with open(ASM, "w") as fh:
        fh.write(
            "// fingerprint %s (generated by tools/make_vendors.py)\n"
            '    .section .rodata.badge_vendors, "a"\n'
            "    .balign 4\n"
            "    .global badge_vendors_start\n"
            "    .global badge_vendors_end\n"
            "badge_vendors_start:\n"
            '    .incbin "%s"\n'
            "badge_vendors_end:\n" % (fp, path)
        )


if IN_PIO:
    generate()
elif len(sys.argv) == 4 and sys.argv[1] == "--update":
    update(sys.argv[2], sys.argv[3])
    generate()
elif len(sys.argv) > 1:
    sys.exit(__doc__)
else:
    generate()

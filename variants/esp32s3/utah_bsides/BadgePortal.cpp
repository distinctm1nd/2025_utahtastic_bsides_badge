#include "configuration.h"

#include "BadgePortal.h"

#include "BadgeApp.h"
#include "BadgeApps.h"
#include "BadgeCapture.h"
#include "BadgeDeviceMenu.h"
#include "BadgeFollow.h"
#include "BadgeHome.h"
#include "BadgeLeds.h"
#include "BadgeLock.h"
#include "BadgeMenu.h"
#include "BadgeMesh.h"
#include "BadgeOta.h"
#include "BadgePictures.h"
#include "BadgePositions.h"
#include "BadgeRadioScan.h"
#include "BadgeRules.h"
#include "BadgeScenes.h"
#include "BadgeTime.h"
#include "BadgeVendors.h"
#include "generated/BadgePortalPage.h"

#include "FSCommon.h"
#include "NodeDB.h"
#include "SPILock.h"
#include "mesh/MeshTypes.h"
#include "mesh/Throttle.h"
#include "mesh/generated/meshtastic/cannedmessages.pb.h"
#include "modules/CannedMessageModule.h"
#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <stdio.h>

extern meshtastic_CannedMessageModuleConfig cannedMessageModuleConfig;

namespace badge
{

static const char *LOG = "/badge/sightings.csv";
static const char *OLD = "/badge/sightings.old.csv";
static const char *CAP = "/badge/capture.pcap";
static const char *WIFICAP = "/badge/wifi.pcap";
static const char *BLECAP = "/badge/ble.pcap";
#if BADGE_OFFENSIVE // research build only: the default build has no trace of these logs
static const char *WARDRIVE = "/badge/wardrive.csv";
static const char *PROBES = "/badge/probes.csv";
#endif
static const char *EVENTS = "/badge/events.csv"; // threat log; 404s until something is logged
static const size_t MAX_PIC = 512 * 1024;

static WebServer *server = nullptr;
static WiFiServer *streamSrv = nullptr; // live pcap stream to Wireshark (TCP 19000)
static WiFiClient streamClient;
static bool streamHdrSent = false;
static char event[48] = "";
static uint32_t eventAt = 0;
static uint8_t *picBuf = nullptr;
static size_t picLen = 0;
static const char *picError = nullptr;
static bool scanning = false;
static char curSsid[24] = "", curPass[16] = "", curAddr[32] = ""; // kept so the app can show them again
static uint32_t portalLastActive = 0;                 // millis() of the last connected phone; drives the idle time-out
static const uint32_t PORTAL_IDLE_MS = 5 * 60 * 1000; // no phone connected this long -> stop sharing

static void note(const char *what)
{
    snprintf(event, sizeof(event), "%s", what);
    eventAt = millis();
}

static String js(const char *s) // a JSON string
{
    String o = "\"";
    for (; s && *s; s++) {
        char c = *s;
        if (c == '"' || c == '\\') {
            o += '\\';
            o += c;
        } else if ((uint8_t)c < 0x20) {
            char b[8];
            snprintf(b, sizeof(b), "\\u%04x", c);
            o += b;
        } else {
            o += c;
        }
    }
    return o + "\"";
}

static void sendJson(const String &body)
{
    server->sendHeader("Cache-Control", "no-store");
    server->send(200, "application/json", body);
}

static void fail(const char *why, int code = 400)
{
    server->send(code, "text/plain", why);
}

// Only answer requests addressed to the badge's own address, and (when the browser says where
// the request came from) only from the badge's own page. Stops another web page open on the
// connected phone from driving this API (cross-site requests, DNS rebinding).
static bool originOk()
{
    String ip = WiFi.softAPIP().toString();
    String host = server->hostHeader();
    if (host != ip && host != ip + ":80")
        return false;
    if (!server->hasHeader("Origin"))
        return true; // same-origin GETs and plain downloads carry no Origin
    String origin = server->header("Origin");
    return origin == "http://" + ip || origin == "http://" + ip + ":80";
}

// The PIN lock and the portal: a device that was using the page while the badge was unlocked keeps
// its session when the lock comes up (it locks by itself after 5 minutes), but a device that joins
// while the badge is locked is refused until the PIN is typed. One device at a time can be joined,
// so "the session" is simply: something was served while unlocked and nobody has left since.
static bool sessionOk = false;

// nullptr when this request may be answered, otherwise why not (sets *code)
static const char *refusal(int *code)
{
    if (!originOk()) {
        *code = 403;
        return "Forbidden";
    }
    if (!lockLocked())
        sessionOk = true;
    else if (!sessionOk) {
        *code = 423;
        return "The badge is locked: unlock it, then reload this page";
    }
    return nullptr;
}

static void guarded(const char *uri, HTTPMethod method, WebServer::THandlerFunction fn)
{
    server->on(uri, method, [fn] {
        int code = 0;
        if (const char *why = refusal(&code))
            fail(why, code);
        else
            fn();
    });
}
static void guardedUpload(const char *uri, WebServer::THandlerFunction done, WebServer::THandlerFunction upload)
{
    server->on(
        uri, HTTP_POST,
        [done] {
            int code = 0;
            if (const char *why = refusal(&code))
                fail(why, code);
            else
                done();
        },
        [upload] {
            int code = 0;
            if (!refusal(&code))
                upload();
        });
}

static size_t fileSize(const char *path)
{
    concurrency::LockGuard g(spiLock);
    File f = FSCom.open(path, FILE_O_READ);
    size_t n = f ? f.size() : 0;
    if (f)
        f.close();
    return n;
}

// ---- pages ---------------------------------------------------------------------------------
static void page()
{
    server->sendHeader("Content-Encoding", "gzip");
    server->sendHeader("Cache-Control", "no-store");
    server->send_P(200, "text/html", (const char *)PORTAL_PAGE_GZ, PORTAL_PAGE_GZ_LEN);
}

static void info()
{
    char name[40];
    meshNodeName(meshMyNode(), name, sizeof(name));
    sendJson(String("{\"node\":") + js(name) + ",\"version\":" + js(otaRunningVersion()) +
             ",\"ota\":" + (otaAvailable() ? "true" : "false") + "}");
}

static void logList()
{
    String o = "{\"files\":[";
    bool first = true;
    for (const char *p : {LOG, OLD, CAP, WIFICAP, BLECAP,
#if BADGE_OFFENSIVE
                          WARDRIVE, PROBES,
#endif
                          EVENTS}) {
        size_t n = fileSize(p);
        if (!n)
            continue;
        o += String(first ? "" : ",") + "{\"name\":" + js(strrchr(p, '/') + 1) + ",\"size\":" + String((unsigned)n) + "}";
        first = false;
    }
    sendJson(o + "]}");
}

// Sent in pieces, taking the storage lock only for each read: holding it for the whole download
// (a capture can be megabytes over slow WiFi) would stall everything else that needs the lock.
static void sendLog(const char *path, const char *name)
{
    File f;
    size_t left = 0;
    {
        concurrency::LockGuard g(spiLock);
        f = FSCom.open(path, FILE_O_READ);
        left = f ? f.size() : 0;
    }
    if (!f) {
        fail("Not found", 404);
        return;
    }
    server->sendHeader("Content-Disposition", String("attachment; filename=") + name);
    server->setContentLength(left);
    server->send(200, strstr(name, ".pcap") ? "application/vnd.tcpdump.pcap" : "text/csv", "");
    static uint8_t buf[1024];
    while (left && server->client().connected()) {
        int n;
        {
            concurrency::LockGuard g(spiLock);
            n = f.read(buf, left < sizeof(buf) ? left : sizeof(buf));
        }
        if (n <= 0)
            break;
        server->sendContent((const char *)buf, (size_t)n);
        left -= n;
    }
    concurrency::LockGuard g(spiLock);
    f.close();
}

// ---- pictures ------------------------------------------------------------------------------
static void picList()
{
    String o = "{\"free\":" + String((unsigned)picFreeSpace()) + ",\"pics\":[";
    for (int i = 0; i < BADGE_IMAGE_COUNT; i++)
        o += String(i ? "," : "") + "{\"name\":" + js(BADGE_IMAGES[i].name) +
             ",\"builtin\":true,\"fav\":" + (nametagIsFavorite(BADGE_IMAGES[i].name) ? "true" : "false") + "}";
    for (int i = 0; i < picCount(); i++)
        o += String(BADGE_IMAGE_COUNT || i ? "," : "") + "{\"i\":" + String(i) + ",\"name\":" + js(picName(i)) +
             ",\"fav\":" + (nametagIsFavorite(picName(i)) ? "true" : "false") + "}";
    sendJson(o + "]}");
}

static void picUpload()
{
    HTTPUpload &u = server->upload();
    if (u.status == UPLOAD_FILE_START) {
        free(picBuf);
        picBuf = (uint8_t *)malloc(MAX_PIC); // PSRAM
        picLen = 0;
        picError = picBuf ? nullptr : "Not enough memory";
    } else if (u.status == UPLOAD_FILE_WRITE && picBuf && !picError) {
        if (picLen + u.currentSize > MAX_PIC)
            picError = "Picture too big (512 KB at most)";
        else {
            memcpy(picBuf + picLen, u.buf, u.currentSize);
            picLen += u.currentSize;
        }
    } else if (u.status == UPLOAD_FILE_END && picBuf && !picError) {
        picError = picSave(picBuf, picLen);
    } else if (u.status == UPLOAD_FILE_ABORTED) {
        picError = "Upload stopped";
        free(picBuf); // the finish handler doesn't run after an abort
        picBuf = nullptr;
    }
}

static void picDone()
{
    free(picBuf);
    picBuf = nullptr;
    if (picError) {
        fail(picError);
        return;
    }
    note("Picture saved");
    sendJson("{}");
}

static void picOp(bool del)
{
    int i = server->arg("i").toInt(), d = server->arg("d").toInt();
    if (del ? picDelete(i) : picMove(i, d > 0 ? 1 : -1))
        sendJson("{}");
    else
        fail("No such picture");
}

static void picFav()
{
    String name = server->arg("name");
    bool known = false;
    for (int i = 1; i < nametagChoiceCount() && !known; i++)
        known = name == nametagChoice(i);
    if (!known)
        fail("No such picture");
    else if (!nametagSetFavorite(name.c_str(), server->arg("on") == "1"))
        fail("Too many favorites");
    else
        sendJson("{}");
}

// ---- settings ------------------------------------------------------------------------------
static String names(int n, const char *(*name)(int))
{
    String o = "[";
    for (int i = 0; i < n; i++)
        o += String(i ? "," : "") + js(name(i));
    return o + "]";
}

static void settingsGet()
{
    String o = "{\"name\":" + js(nametagName()) + ",\"tagline\":" + js(taglineGet());
    o += ",\"font\":" + String(nametagFont()) + ",\"fonts\":" + names(nametagFontCount(), nametagFontName);
    o += ",\"size\":" + String(nametagSize()) + ",\"sizes\":" + names(4, nametagSizeName);
    o += ",\"scene\":" + String(ledsScene()) + ",\"scenes\":" + names(sceneCount(), [](int i) { return sceneAt(i).name; });
    o += ",\"speed\":" + String(ledsSpeed()) + ",\"speeds\":" + names(ledsSpeedCount(), ledsSpeedName);
    o += ",\"bright\":" + String(ledsBright()) + ",\"brights\":" + names(ledsBrightCount(), ledsBrightName);
    o += ",\"colour\":" + String(menuColour()) + ",\"colours\":" + names(menuColourCount(), menuColourName);
    o += String(",\"night\":") + (ledsNightMode() ? "true" : "false");
    o += String(",\"welcome\":") + (menuWelcome() ? "true" : "false");
    o += ",\"canned\":" + js(cannedMessageModuleConfig.messages);
    o += String(",\"tagawake\":") + (menuTagAwake() ? "true" : "false");
    o += String(",\"scroll\":") + (nametagScroll() ? "true" : "false");
    o += String(",\"remote\":") + (meshRemoteCommands() ? "true" : "false");
    o += String(",\"home\":") + (homeEnabled() ? "true" : "false");
    o += ",\"ledoff\":" + String(ledsOffMode()) + ",\"ledoffs\":[\"stay on\",\"dim\",\"off\"]";
    o += ",\"lockpic\":" + js(lockPicture()) +
         ",\"lockpics\":[{\"v\":\"@clock\",\"n\":\"Clock and date\"},{\"v\":\"@cycle\",\"n\":\"Cycle: name + pictures\"},"
         "{\"v\":\"@cyclepics\",\"n\":\"Cycle: pictures only\"},{\"v\":\"@cyclefavs\",\"n\":\"Cycle: favourites\"},"
         "{\"v\":\"@tag\",\"n\":\"Your name\"}";
    for (int i = 0; i < BADGE_IMAGE_COUNT; i++)
        o += ",{\"v\":" + js(BADGE_IMAGES[i].name) + ",\"n\":" + js(BADGE_IMAGES[i].name) + "}";
    for (int i = 0; i < picCount(); i++)
        o += ",{\"v\":" + js(picName(i)) + ",\"n\":" + js(picName(i)) + "}";
    o += "]";
    o += ",\"tz\":" + js(config.device.tzdef) + ",\"tzs\":[";
    for (int i = 0; i < timeZoneCount(); i++)
        o += String(i ? "," : "") + "{\"v\":" + js(timeZoneValue(i)) + ",\"n\":" + js(timeZoneLabel(i)) + "}";
    o += "]";
    char qrl[200], qrt[200];
    qrGet(true, qrl, sizeof(qrl));
    qrGet(false, qrt, sizeof(qrt));
    o += ",\"qrlink\":" + js(qrl) + ",\"qrtext\":" + js(qrt);
    o += ",\"startup\":" + js(picStartup()) +
         ",\"startups\":[{\"v\":\"\",\"n\":\"Animated raven intro\"},{\"v\":\"@tag\",\"n\":\"Name tag\"}";
    for (int i = 0; i < BADGE_IMAGE_COUNT; i++)
        o += ",{\"v\":" + js(BADGE_IMAGES[i].name) + ",\"n\":" + js(BADGE_IMAGES[i].name) + "}";
    for (int i = 0; i < picCount(); i++)
        o += ",{\"v\":" + js(picName(i)) + ",\"n\":" + js(picName(i)) + "}";
    sendJson(o + "]}");
}

static int argInt(const char *name, int lo, int hi, int keep)
{
    if (!server->hasArg(name))
        return keep;
    int v = server->arg(name).toInt();
    return v < lo ? lo : v > hi ? hi : v;
}

static void settingsPost()
{
    if (server->hasArg("name"))
        nametagSetName(server->arg("name").substring(0, 40).c_str());
    if (server->hasArg("tagline"))
        taglineSet(server->arg("tagline").substring(0, 60).c_str());
    nametagSetFont(argInt("font", 0, nametagFontCount() - 1, nametagFont()));
    nametagSetSize(argInt("size", 0, 3, nametagSize()));
    int scene = argInt("scene", 0, sceneCount() - 1, ledsScene());
    if (scene != ledsScene())
        ledsSetScene(scene, true);
    ledsSetSpeed(argInt("speed", 0, ledsSpeedCount() - 1, ledsSpeed()));
    ledsSetBright(argInt("bright", 0, ledsBrightCount() - 1, ledsBright()));
    int colour = argInt("colour", 0, menuColourCount() - 1, menuColour());
    if (colour != menuColour())
        menuSetColour(colour);
    bool night = argInt("night", 0, 1, ledsNightMode());
    if (night != ledsNightMode()) {
        ledsSetNightMode(night);
        menuSetColour(menuColour()); // red screen on / off
    }
    menuSetWelcome(argInt("welcome", 0, 1, menuWelcome()));
    menuSetTagAwake(argInt("tagawake", 0, 1, menuTagAwake()));
    nametagSetScroll(argInt("scroll", 0, 1, nametagScroll()));
    meshSetRemoteCommands(argInt("remote", 0, 1, meshRemoteCommands()));
    int home = argInt("home", 0, 1, homeEnabled());
    if (home != homeEnabled())
        homeSetEnabled(home);
    ledsSetOffMode(argInt("ledoff", 0, 2, ledsOffMode()));
    if (server->hasArg("lockpic"))
        lockSetPicture(server->arg("lockpic").substring(0, 24).c_str());
    if (server->hasArg("tz")) {
        String tz = server->arg("tz");
        if (tz.length() && tz != config.device.tzdef)
            timeSetZone(tz.c_str());
    }
    if (server->hasArg("startup"))
        picSetStartup(server->arg("startup").substring(0, 24).c_str());
    if (server->hasArg("qrlink"))
        qrSet(true, server->arg("qrlink").substring(0, 199).c_str());
    if (server->hasArg("qrtext"))
        qrSet(false, server->arg("qrtext").substring(0, 199).c_str());
    if (server->hasArg("canned") && cannedMessageModule) {
        String c = server->arg("canned");
        if (c.length() < sizeof(cannedMessageModuleConfig.messages) && c.length())
            cannedMessageModule->handleSetCannedMessageModuleMessages(c.c_str());
    }
    note("Settings saved");
    sendJson("{}");
}

// Meshtastic network config (WiFi + MQTT) — the text-entry settings that are painful on the 12-key
// pad, typed here from a browser so the phone app is optional. Saved to flash; WiFi/MQTT take
// effect after the badge reboots.
static void copyArg(const char *name, char *dst, size_t n)
{
    if (server->hasArg(name))
        strlcpy(dst, server->arg(name).c_str(), n);
}
// A password is write-only: blank keeps the saved one, <name>Clear=1 removes it.
static void copySecret(const char *name, const char *clearName, char *dst, size_t n)
{
    if (server->arg(clearName) == "1")
        dst[0] = 0;
    else if (server->arg(name).length())
        strlcpy(dst, server->arg(name).c_str(), n);
}
static void meshConfigGet()
{
    auto &n = config.network;
    auto &m = moduleConfig.mqtt;
    String o = "{";
    o += String("\"wifiEnabled\":") + (n.wifi_enabled ? "true" : "false");
    o += ",\"wifiSsid\":" + js(n.wifi_ssid);
    o += String(",\"wifiPskSet\":") + (n.wifi_psk[0] ? "true" : "false"); // never sent back, only whether one is saved
    o += ",\"ntp\":" + js(n.ntp_server);
    o += String(",\"mqttEnabled\":") + (m.enabled ? "true" : "false");
    o += ",\"mqttAddr\":" + js(m.address);
    o += ",\"mqttUser\":" + js(m.username);
    o += String(",\"mqttPassSet\":") + (m.password[0] ? "true" : "false");
    o += ",\"mqttRoot\":" + js(m.root);
    o += String(",\"mqttTls\":") + (m.tls_enabled ? "true" : "false");
    o += String(",\"mqttJson\":") + (m.json_enabled ? "true" : "false");
    o += String(",\"mqttEncrypt\":") + (m.encryption_enabled ? "true" : "false");
    sendJson(o + "}");
}
static void meshConfigPost()
{
    auto &n = config.network;
    auto &m = moduleConfig.mqtt;
    n.wifi_enabled = argInt("wifiEnabled", 0, 1, n.wifi_enabled);
    copyArg("wifiSsid", n.wifi_ssid, sizeof(n.wifi_ssid));
    copySecret("wifiPsk", "wifiPskClear", n.wifi_psk, sizeof(n.wifi_psk));
    copyArg("ntp", n.ntp_server, sizeof(n.ntp_server));
    m.enabled = argInt("mqttEnabled", 0, 1, m.enabled);
    copyArg("mqttAddr", m.address, sizeof(m.address));
    copyArg("mqttUser", m.username, sizeof(m.username));
    copySecret("mqttPass", "mqttPassClear", m.password, sizeof(m.password));
    copyArg("mqttRoot", m.root, sizeof(m.root));
    m.tls_enabled = argInt("mqttTls", 0, 1, m.tls_enabled);
    m.json_enabled = argInt("mqttJson", 0, 1, m.json_enabled);
    m.encryption_enabled = argInt("mqttEncrypt", 0, 1, m.encryption_enabled);
    nodeDB->saveToDisk(SEGMENT_CONFIG | SEGMENT_MODULECONFIG);
    note("Mesh network saved");
    sendJson("{}");
}

static void myNetsGetH()
{
    char buf[200];
    myNetsGet(buf, sizeof(buf));
    sendJson(String("{\"nets\":") + js(buf) + "}");
}
static void myNetsPostH()
{
    myNetsSet(server->arg("nets").c_str()); // one SSID per line; the targeted evil-twin watch list
    note("My networks saved");
    sendJson("{}");
}

// ---- message -------------------------------------------------------------------------------
static void nodes()
{
    String o = "[";
    int n = 0;
    for (size_t i = 0; nodeDB && i < nodeDB->getNumMeshNodes() && n < 80; i++) {
        meshtastic_NodeInfoLite *nd = nodeDB->getMeshNodeByIndex(i);
        if (!nd || nd->num == meshMyNode() || !nodeInfoLiteHasUser(nd))
            continue;
        o += String(n++ ? "," : "") + "{\"num\":" + String((unsigned long)nd->num) + ",\"name\":" + js(nd->long_name) + "}";
    }
    sendJson(o + "]");
}

static void message()
{
    String text = server->arg("text"), to = server->arg("to");
    text.trim();
    if (!text.length() || text.length() > 200) {
        fail("Empty or too long (200 bytes at most)");
        return;
    }
    uint32_t dest = to == "all" ? NODENUM_BROADCAST : (uint32_t)strtoul(to.c_str(), nullptr, 10);
    if (!dest || !meshSendTextDM(dest, text.c_str(), 0)) {
        fail("The mesh isn't ready", 503);
        return;
    }
    note("Message sent");
    sendJson("{}");
}

// ---- your own scan rules ---------------------------------------------------------------------
static void tilesGet()
{
    String o = "{\"tiles\":[";
    for (int i = 0; i < tileCount(); i++)
        o += (i ? "," : "") + String("{\"id\":") + js(tileIdAt(i)) + ",\"label\":" + js(tileLabelAt(i)) +
             ",\"shown\":" + (tileShownAt(i) ? "true" : "false") + "}";
    sendJson(o + "]}");
}
static void tilesPost()
{
    tileSetOrder(server->arg("ids").c_str(), server->arg("hidden").c_str());
    sendJson("{\"ok\":true}");
}
static void shortcutsGet()
{
    String o = "{\"slots\":[";
    for (int n = 0; n < 10; n++)
        o += (n ? "," : "") + js(shortcutSlotId(n));
    o += "],\"options\":[";
    for (int i = 0; i < shortcutOptionCount(); i++)
        o += (i ? "," : "") + String("{\"id\":") + js(shortcutOptionId(i)) + ",\"label\":" + js(shortcutOptionLabel(i)) + "}";
    sendJson(o + "]}");
}
static void shortcutsPost()
{
    int n = server->arg("n").toInt();
    shortcutSetSlot(n, server->arg("id").c_str());
    sendJson("{\"ok\":true}");
}

static void rulesList()
{
    static const char *const K[] = {"btname", "wifiname", "maker", "address"};
    String o = "[";
    for (int i = 0; i < customRuleCount(); i++) {
        const CustomRule &r = customRuleAt(i);
        o += String(i ? "," : "") + "{\"kind\":\"" + K[r.kind] + "\",\"pattern\":" + js(r.pattern) + ",\"title\":" + js(r.title) +
             ",\"code\":" + js(r.code) + "}";
    }
    sendJson(o + "]");
}
static void rulesAdd()
{
    const char *why = customRuleAdd(server->arg("kind").c_str(), server->arg("pattern").c_str(), server->arg("title").c_str(),
                                    server->arg("code").c_str());
    if (why) {
        fail(why);
        return;
    }
    sendJson("{}");
}
static void rulesDelete()
{
    if (!customRuleDelete(server->arg("i").toInt())) {
        fail("No such rule");
        return;
    }
    sendJson("{}");
}

// ---- map -----------------------------------------------------------------------------------
static void positions()
{
    static NodePos *list = psramArray<NodePos>(80);
    if (!list) {
        fail("Not enough memory", 503);
        return;
    }
    int32_t la = 0, lo = 0;
    bool mine = posMine(la, lo);
    int n = posList(list, 80);
    String o = "{\"me\":";
    o += mine ? "{\"lat\":" + String(la * 1e-7, 7) + ",\"lon\":" + String(lo * 1e-7, 7) + "}" : String("null");
    o += ",\"nodes\":[";
    for (int i = 0; i < n; i++) {
        const NodePos &p = list[i];
        o += String(i ? "," : "") + "{\"name\":" + js(p.name) + ",\"lat\":" + String(p.lat * 1e-7, 7) +
             ",\"lon\":" + String(p.lon * 1e-7, 7) + ",\"dist\":" + String((int)p.distM) +
             ",\"bearing\":" + String((int)p.bearing) + ",\"age\":" + String((unsigned)p.ageS) + ",\"hops\":" + String(p.hops) +
             "}";
    }
    sendJson(o + "]}");
}

// ---- live Bluetooth view -------------------------------------------------------------------
static void hunt()
{
    if (!scanning) {
        if (const char *why = radioScanStart(SCAN_BLE)) {
            fail(why, 503);
            return;
        }
        scanning = true;
    }
    radioScanPoll();
    static ScanEntry *list = psramArray<ScanEntry>(40);
    if (!list) {
        fail("Not enough memory", 503);
        return;
    }
    int n = radioScanSnapshot(list, 40);
    String o = "[";
    for (int pass = 0; pass < 2; pass++) // marked first
        for (int i = 0; i < n; i++) {
            const ScanEntry &e = list[i];
            RuleHit h;
            bool hit = ruleMatch(e, false, h), alert = hit && h.alert;
            if (alert != (pass == 0))
                continue;
            const char *maker = e.company != NO_COMPANY ? vendorFromCompanyId(e.company) : nullptr;
            if (!maker && e.publicAddr)
                maker = vendorFromAddress(e.mac);
            char addr[24];
            snprintf(addr, sizeof(addr), "%02X:%02X:%02X:%02X:%02X:%02X", e.mac[0], e.mac[1], e.mac[2], e.mac[3], e.mac[4],
                     e.mac[5]);
            FollowInfo f;
            unsigned mins = alert && followGet(e.mac, f) ? f.withYouMs / 60000 : 0;
            if (o.length() > 1)
                o += ",";
            o += "{\"name\":" + js(e.name) + ",\"maker\":" + js(maker ? maker : "") + ",\"addr\":" + js(addr) +
                 ",\"rssi\":" + String(e.rssi) + ",\"code\":" + js(hit ? h.code : "") + ",\"title\":" + js(hit ? h.title : "") +
                 ",\"alert\":" + (alert ? "true" : "false") + ",\"min\":" + String(mins) +
                 ",\"pub\":" + (e.publicAddr ? "true" : "false") + "}";
        }
    sendJson(o + "]");
}

// ---- explore one Bluetooth device (same engine as the badge's "Explore") -------------------
static bool parseMac(const String &s, uint8_t out[6])
{
    int b[6];
    if (sscanf(s.c_str(), "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6)
        return false;
    for (int i = 0; i < 6; i++)
        out[i] = (uint8_t)b[i];
    return true;
}

static void exploreStartReq()
{
    uint8_t mac[6];
    if (!parseMac(server->arg("addr"), mac)) {
        fail("Bad address");
        return;
    }
    bleExploreStart(mac, server->arg("pub") == "1");
    sendJson("{\"ok\":true}");
}

static void exploreGet()
{
    static const char *const ST[] = {"idle", "busy", "done"};
    String o = String("{\"state\":") + js(ST[bleExploreState() % 3]) + ",\"status\":" + js(bleExploreStatus()) + ",\"lines\":[";
    for (int i = 0; i < bleExploreCount(); i++)
        o += (i ? "," : "") + js(bleExploreLine(i));
    sendJson(o + "]}");
}

// ---- firmware update -----------------------------------------------------------------------
static const char *otaError = nullptr;

static int hexVal(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

static void updUpload()
{
    HTTPUpload &u = server->upload();
    if (u.status == UPLOAD_FILE_START) {
        otaError = nullptr;
        String sigHex = server->arg("sig");
        uint8_t sig[64];
        if (sigHex.length() != 128) {
            otaError = "Missing signature";
            return;
        }
        for (int i = 0; i < 64; i++) {
            int a = hexVal(sigHex[2 * i]), b = hexVal(sigHex[2 * i + 1]);
            if (a < 0 || b < 0) {
                otaError = "Bad signature";
                return;
            }
            sig[i] = a << 4 | b;
        }
        otaError = otaBegin((size_t)server->arg("size").toInt(), sig);
        if (!otaError)
            note("Receiving an update");
    } else if (u.status == UPLOAD_FILE_WRITE && !otaError) {
        if (!otaWrite(u.buf, u.currentSize))
            otaError = otaMessage();
    } else if (u.status == UPLOAD_FILE_END && !otaError) {
        otaFinish();
    } else if (u.status == UPLOAD_FILE_ABORTED) {
        otaAbort();
        otaError = "Upload stopped";
    }
}

static void updDone()
{
    if (otaError) {
        fail(otaError);
        return;
    }
    sendJson("{}");
}

static void updStatus()
{
    static const char *const STATES[] = {"idle", "receiving", "confirm", "rebooting", "failed"};
    sendJson(String("{\"state\":") + js(STATES[otaState()]) + ",\"msg\":" + js(otaError ? otaError : otaMessage()) +
             ",\"version\":" + js(otaNewVersion()) + "}");
}

// ---- start / stop --------------------------------------------------------------------------
const char *portalStart(char *ssid, size_t ns, char *pass, size_t np, char *addr, size_t na)
{
    if (server) { // already sharing (kept alive under the lock screen): keep the same network
        snprintf(ssid, ns, "%s", curSsid);
        snprintf(pass, np, "%s", curPass);
        snprintf(addr, na, "%s", curAddr);
        portalLastActive = millis();
        return nullptr;
    }
    portalStop();
    watchPause(true); // the background watch's scans would clash
    radioScanStop();
    const char *why = WiFi.getMode() != WIFI_OFF                             ? "WiFi is in use\n(Meshtastic WiFi on?)"
                      : heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 60000 ? "Not enough memory\nfor WiFi"
                                                                             : nullptr;
    if (why) {
        watchPause(false);
        return why;
    }
    static const char ALPHA[] = "abcdefghjkmnpqrstuvwxyz23456789"; // no look-alikes
    // 12 random characters in three groups, "k7mp-x2qa-9vtn" (the hyphens are part of the password,
    // so what the screen shows is exactly what is typed): ~59 bits, out of reach of offline cracking
    // of a captured WPA2 handshake. A smaller buffer gets as many as fit (WPA2 needs 8).
    size_t len = 0;
    for (int i = 0; i < 12 && len + 1 < np; i++) {
        if (i && i % 4 == 0 && len + 2 < np)
            pass[len++] = '-';
        pass[len++] = ALPHA[esp_random() % (sizeof(ALPHA) - 1)];
    }
    pass[len] = 0;
    snprintf(ssid, ns, "BSides-Badge-%04X", (unsigned)(meshMyNode() & 0xFFFF));
    if (!WiFi.mode(WIFI_AP) || !WiFi.softAP(ssid, pass, 6, 0, 1)) { // one device at a time
        WiFi.mode(WIFI_OFF);
        watchPause(false);
        return "WiFi didn't start";
    }
    snprintf(addr, na, "http://%s", WiFi.softAPIP().toString().c_str());
    server = new WebServer(80);
    static const char *const WANTED[] = {"Origin"};
    server->collectHeaders((const char **)WANTED, 1);
    guarded("/", HTTP_GET, page);
    guarded("/api/info", HTTP_GET, info);
    guarded("/api/log", HTTP_GET, logList);
    guarded("/sightings.csv", HTTP_GET, [] { sendLog(LOG, "sightings.csv"); });
    guarded("/sightings.old.csv", HTTP_GET, [] { sendLog(OLD, "sightings.old.csv"); });
    guarded("/capture.pcap", HTTP_GET, [] { sendLog(CAP, "capture.pcap"); });
    guarded("/wifi.pcap", HTTP_GET, [] { sendLog(WIFICAP, "wifi.pcap"); });
    guarded("/ble.pcap", HTTP_GET, [] { sendLog(BLECAP, "ble.pcap"); });
#if BADGE_OFFENSIVE
    guarded("/wardrive.csv", HTTP_GET, [] { sendLog(WARDRIVE, "wardrive.csv"); });
    guarded("/probes.csv", HTTP_GET, [] { sendLog(PROBES, "probes.csv"); });
#endif
    guarded("/events.csv", HTTP_GET, [] { sendLog(EVENTS, "events.csv"); });
    guarded("/api/pics", HTTP_GET, picList);
    guardedUpload("/api/pics", picDone, picUpload);
    guarded("/api/pics/delete", HTTP_POST, [] { picOp(true); });
    guarded("/api/pics/move", HTTP_POST, [] { picOp(false); });
    guarded("/api/pics/fav", HTTP_POST, picFav);
    guarded("/api/settings", HTTP_GET, settingsGet);
    guarded("/api/settings", HTTP_POST, settingsPost);
    guarded("/api/meshconfig", HTTP_GET, meshConfigGet);
    guarded("/api/meshconfig", HTTP_POST, meshConfigPost);
    guarded("/api/mynets", HTTP_GET, myNetsGetH);
    guarded("/api/mynets", HTTP_POST, myNetsPostH);
    guarded("/api/nodes", HTTP_GET, nodes);
    guarded("/api/message", HTTP_POST, message);
    guarded("/api/hunt", HTTP_GET, hunt);
    guarded("/api/positions", HTTP_GET, positions);
    guarded("/api/rules", HTTP_GET, rulesList);
    guarded("/api/rules", HTTP_POST, rulesAdd);
    guarded("/api/rules/delete", HTTP_POST, rulesDelete);
    guarded("/api/tiles", HTTP_GET, tilesGet);
    guarded("/api/tiles", HTTP_POST, tilesPost);
    guarded("/api/shortcuts", HTTP_GET, shortcutsGet);
    guarded("/api/shortcuts", HTTP_POST, shortcutsPost);
    guarded("/api/time", HTTP_POST, [] {
        bool ok = timeSetFromBrowser((uint32_t)strtoul(server->arg("t").c_str(), nullptr, 10));
        if (server->hasArg("tz"))
            timeSetZone(server->arg("tz").c_str());
        sendJson(ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });
    guarded("/api/update", HTTP_GET, updStatus);
    guardedUpload("/api/update", updDone, updUpload);
    guarded("/api/explore", HTTP_GET, exploreGet);
    guarded("/api/explore", HTTP_POST, exploreStartReq);
    guarded("/api/stream", HTTP_GET, [] { sendJson(String("{\"kind\":") + captureStreamKind() + "}"); });
    guarded("/api/stream", HTTP_POST, [] {
        int k = server->arg("kind").toInt();
        if (k < 0 || k > 2)
            k = 0;
        captureStreamSet(k);
        streamClient.stop(); // make any viewer reconnect so it gets the right pcap header
        streamHdrSent = false;
        sendJson("{\"ok\":true}");
    });
    server->onNotFound([] { fail("Not found", 404); });
    server->begin();
    streamSrv = new WiFiServer(19000);
    streamSrv->begin();
    streamHdrSent = false;
    sessionOk = false;
    event[0] = 0;
    snprintf(curSsid, sizeof(curSsid), "%s", ssid);
    snprintf(curPass, sizeof(curPass), "%s", pass);
    snprintf(curAddr, sizeof(curAddr), "%s", addr);
    portalLastActive = millis();
    return nullptr;
}

void portalStop()
{
    if (!server)
        return;
    server->stop();
    delete server;
    server = nullptr;
    captureStreamSet(STREAM_OFF);
    streamClient.stop();
    if (streamSrv) {
        streamSrv->stop();
        delete streamSrv;
        streamSrv = nullptr;
    }
    free(picBuf);
    picBuf = nullptr;
    if (otaState() == OTA_RECEIVING || otaState() == OTA_CONFIRM)
        otaAbort();
    if (scanning) {
        radioScanStop();
        scanning = false;
    }
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    watchPause(false);
}

static void streamPoll()
{
    if (!streamSrv)
        return;
    if (!streamClient || !streamClient.connected()) {
        WiFiClient c = streamSrv->available();
        if (c) {
            streamClient = c;
            streamHdrSent = false;
        }
    }
    if (!streamClient || !streamClient.connected() || captureStreamKind() == STREAM_OFF)
        return;
    if (!streamHdrSent) { // pcap global header: magic, v2.4, zone, sig, snaplen, linktype
        uint32_t lt = captureStreamLinktype();
        uint8_t gh[24] = {0xd4, 0xc3, 0xb2, 0xa1, 2, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0, 0};
        gh[20] = lt, gh[21] = lt >> 8, gh[22] = lt >> 16, gh[23] = lt >> 24;
        streamClient.write(gh, 24);
        streamHdrSent = true;
    }
    if (captureStreamKind() == STREAM_BLE) { // BLE needs a scan running; mesh is passive
        if (!scanning && !radioScanStart(SCAN_BLE))
            scanning = true;
        if (scanning)
            radioScanPoll();
    }
    uint8_t buf[512];
    int n;
    while (streamClient.connected() && (n = captureStreamDrain(buf, sizeof(buf))) > 0)
        streamClient.write(buf, n);
}

void portalPoll()
{
    if (!server)
        return;
    server->handleClient();
    streamPoll();
    bool joined = WiFi.softAPgetStationNum() > 0;
    if (!joined)
        sessionOk = false; // the device left: the next one to join needs the badge unlocked
    if (joined || otaState() == OTA_RECEIVING || otaState() == OTA_CONFIRM)
        portalLastActive = millis(); // a phone is connected (or mid-update): never time out
    else if (Throttle::hasElapsed(portalLastActive, PORTAL_IDLE_MS))
        portalStop(); // nobody has been connected for a while: stop sharing on its own
}

bool portalRunning()
{
    return server != nullptr;
}

int portalClients()
{
    return server ? WiFi.softAPgetStationNum() : 0;
}

const char *portalLastEvent(unsigned *ageS)
{
    if (!event[0])
        return nullptr;
    *ageS = (millis() - eventAt) / 1000;
    return event;
}

} // namespace badge

#include "configuration.h"

#include "BadgeOta.h"
#include "BadgeUpdateKey.h"

#include <Arduino.h>
#include <Ed25519.h>
#include <SHA256.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <stdio.h>
#include <string.h>

extern uint32_t rebootAtMsec;

namespace badge
{

static OtaState state = OTA_IDLE;
static char msg[96] = "";
static char newVersion[32] = "";
static esp_ota_handle_t handle = 0;
static const esp_partition_t *target = nullptr;
static SHA256 sha;
static uint8_t signature[64];
static size_t expected = 0, received = 0;

static void fail(const char *why)
{
    if (handle) {
        esp_ota_abort(handle);
        handle = 0;
    }
    snprintf(msg, sizeof(msg), "%s", why);
    state = OTA_FAILED;
}

// When an image was built, as a number that sorts by time (yyyymmddhhmmss), from the "Nov 11 2026"
// / "10:18:00" strings the build puts in every image; 0 if they can't be read.
static uint64_t builtAt(const esp_app_desc_t &d)
{
    static const char MONTHS[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4] = "";
    int day = 0, year = 0, hh = 0, mm = 0, ss = 0;
    if (sscanf(d.date, "%3s %d %d", mon, &day, &year) != 3 || sscanf(d.time, "%d:%d:%d", &hh, &mm, &ss) != 3)
        return 0;
    const char *m = strstr(MONTHS, mon);
    if (!m || (m - MONTHS) % 3)
        return 0;
    int month = (int)(m - MONTHS) / 3 + 1;
    return ((((uint64_t)year * 100 + month) * 100 + day) * 100 + hh) * 10000 + mm * 100 + ss;
}

bool otaAvailable()
{
    for (uint8_t b : UPDATE_PUBLIC_KEY)
        if (b)
            return true;
    return false;
}

const char *otaBegin(size_t imageSize, const uint8_t sig[64])
{
    otaAbort();
    if (!otaAvailable())
        return "WiFi updates aren't set up in this build (no update key)";
    // The other firmware slot, by name (not "the next slot", which depends on how many there are).
    const esp_partition_t *running = esp_ota_get_running_partition();
    bool onSlot0 = !running || running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0;
    target = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                      onSlot0 ? ESP_PARTITION_SUBTYPE_APP_OTA_1 : ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
    if (!target)
        return "No second firmware slot: install this version over USB / the web installer first";
    if (!imageSize || imageSize > target->size)
        return "That file is too big for the firmware slot";
    if (esp_ota_begin(target, imageSize, &handle) != ESP_OK) {
        handle = 0;
        return "Couldn't prepare the firmware slot";
    }
    memcpy(signature, sig, 64);
    sha.reset();
    expected = imageSize;
    received = 0;
    newVersion[0] = 0;
    snprintf(msg, sizeof(msg), "Receiving...");
    state = OTA_RECEIVING;
    return nullptr;
}

bool otaWrite(const uint8_t *data, size_t len)
{
    if (state != OTA_RECEIVING)
        return false;
    if (received + len > expected) {
        fail("More data than announced");
        return false;
    }
    if (esp_ota_write(handle, data, len) != ESP_OK) {
        fail("Writing the firmware failed");
        return false;
    }
    sha.update(data, len);
    received += len;
    return true;
}

void otaFinish()
{
    if (state != OTA_RECEIVING)
        return;
    if (received != expected) {
        fail("The upload stopped early");
        return;
    }
    uint8_t digest[32];
    sha.finalize(digest, sizeof(digest));
    if (!Ed25519::verify(signature, UPDATE_PUBLIC_KEY, digest, sizeof(digest))) {
        fail("Signature doesn't match: not an official update for this badge");
        return;
    }
    esp_err_t e = esp_ota_end(handle); // checks the image itself
    handle = 0;
    if (e != ESP_OK) {
        snprintf(msg, sizeof(msg), "The firmware image is damaged (%d)", (int)e);
        state = OTA_FAILED;
        return;
    }
    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(target, &desc) == ESP_OK) {
        snprintf(newVersion, sizeof(newVersion), "%s", desc.version);
        // No downgrades over WiFi: an old signed image stays validly signed for ever, so without
        // this anyone holding one could put a badge back on it. Going back is a USB install.
        esp_app_desc_t mine;
        const esp_partition_t *running = esp_ota_get_running_partition();
        if (running && esp_ota_get_partition_description(running, &mine) == ESP_OK) {
            uint64_t theirs = builtAt(desc), ours = builtAt(mine);
            if (theirs && ours && theirs < ours) {
                snprintf(msg, sizeof(msg), "Older than this badge's firmware: install it over USB");
                state = OTA_FAILED;
                return;
            }
        }
    }
    snprintf(msg, sizeof(msg), "Signed and checked. Press # on the badge to install");
    state = OTA_CONFIRM;
}

void otaAbort()
{
    if (handle) {
        esp_ota_abort(handle);
        handle = 0;
    }
    if (state != OTA_REBOOTING)
        state = OTA_IDLE;
}

void otaConfirm(bool yes)
{
    if (state != OTA_CONFIRM)
        return;
    if (!yes) {
        snprintf(msg, sizeof(msg), "Not installed (cancelled on the badge)");
        state = OTA_FAILED;
        return;
    }
    if (esp_ota_set_boot_partition(target) != ESP_OK) {
        fail("Couldn't switch to the new firmware");
        return;
    }
    snprintf(msg, sizeof(msg), "Installed. The badge restarts now");
    state = OTA_REBOOTING;
    rebootAtMsec = millis() + 2000; // Meshtastic saves its state and restarts
}

OtaState otaState()
{
    return state;
}
const char *otaMessage()
{
    return msg;
}
const char *otaNewVersion()
{
    return newVersion;
}
const char *otaRunningVersion()
{
    static esp_app_desc_t desc;
    const esp_partition_t *p = esp_ota_get_running_partition();
    if (p && esp_ota_get_partition_description(p, &desc) == ESP_OK)
        return desc.version;
    return "?";
}
size_t otaReceived()
{
    return received;
}

} // namespace badge

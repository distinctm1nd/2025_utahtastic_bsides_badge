#include "configuration.h"

#include "BadgeBeep.h"

#include "BadgeRadioScan.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <stdio.h>
#include <string.h>

namespace badge
{

static volatile BeepState state = BEEP_IDLE;
static char message[64] = "";
static uint8_t target[6];
static bool targetPublic = false;

static bool writeTo(BLEClient *c, const char *service, const char *chr, const uint8_t *v, size_t n)
{
    BLERemoteService *s = c->getService(service);
    if (!s)
        return false;
    BLERemoteCharacteristic *ch = s->getCharacteristic(chr);
    return ch && ch->writeValue((uint8_t *)v, n, true);
}

static void beepTask(void *)
{
    radioScanHold(true); // the radio can't scan and connect at once
    vTaskDelay(pdMS_TO_TICKS(300));
    char addr[18];
    snprintf(addr, sizeof(addr), "%02x:%02x:%02x:%02x:%02x:%02x", target[0], target[1], target[2], target[3], target[4],
             target[5]);
    bool connected = false, ok = false;
    if (BLEDevice::getInitialized()) {
        BLEClient *c = BLEDevice::createClient();
        const uint8_t t = targetPublic ? BLE_ADDR_PUBLIC : BLE_ADDR_RANDOM;
        connected = c->connect(BLEAddress(String(addr), t), t, 8000); // 8 s connect timeout (ms)
        if (connected) {
            static const uint8_t airtag[] = {0xAF};
            static const uint8_t dult[] = {0x00, 0x03}; // Sound_Start, little-endian
            ok = writeTo(c, "7DFC9000-7D1C-4951-86AA-8D9728F8D66C", "7DFC9001-7D1C-4951-86AA-8D9728F8D66C", airtag,
                         sizeof(airtag)) ||
                 writeTo(c, "15190001-12F4-C226-88ED-2AC5579F2A85", "8E0C0001-1D68-FB92-BF61-48377421680E", dult, sizeof(dult));
            c->disconnect();
        }
        delete c;
    }
    radioScanHold(false);
    snprintf(message, sizeof(message), "%s",
             ok          ? "Asked it to beep: listen!"
             : connected ? "It didn't take the request (only trackers away from their owner beep)"
                         : "Couldn't connect (out of range, or it changed address)");
    state = ok ? BEEP_DONE : BEEP_FAILED;
    vTaskDelete(nullptr);
}

bool beepStart(const uint8_t mac[6], bool publicAddr)
{
    if (state == BEEP_BUSY)
        return false;
    memcpy(target, mac, 6);
    targetPublic = publicAddr;
    snprintf(message, sizeof(message), "Asking it to beep...");
    state = BEEP_BUSY;
    if (xTaskCreate(beepTask, "badgebeep", 6144, nullptr, 1, nullptr) != pdPASS) {
        snprintf(message, sizeof(message), "Out of memory");
        state = BEEP_FAILED;
        return false;
    }
    return true;
}

BeepState beepState()
{
    return state;
}

const char *beepMessage()
{
    return message;
}

void beepClear()
{
    if (state != BEEP_BUSY)
        state = BEEP_IDLE;
}

} // namespace badge

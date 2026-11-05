#include "configuration.h"

#include "BadgeRfSniff.h"

#include "NodeDB.h"
#include "mesh/RadioLibInterface.h"
#include <Arduino.h>

namespace badge
{

// Meshtastic keeps its RadioLib object protected; a member pointer named through a derived
// class reaches it without changing Meshtastic's code
struct RadioPeek : RadioLibInterface {
    static PhysicalLayer *radio(RadioLibInterface *r) { return r->*(&RadioPeek::iface); }
};

static SX1262 *lora = nullptr; // LLCC68 is an SX1262 as far as these calls go
static bool borrowed = false, savedTx = true;
static float tunedMhz = 0;

const char *rfBorrow()
{
    if (borrowed)
        return nullptr;
    RadioLibInterface *r = RadioLibInterface::instance;
    if (!r)
        return "No LoRa radio";
    savedTx = config.lora.tx_enabled;
    config.lora.tx_enabled = false;                      // Meshtastic drops what it would send
    for (int i = 0; i < 150 && !r->canSleep(false); i++) // let a packet in the air finish
        delay(20);
    lora = static_cast<SX1262 *>(RadioPeek::radio(r));
    if (!lora) {
        config.lora.tx_enabled = savedTx;
        return "No LoRa radio";
    }
    lora->standby();
    borrowed = true;
    tunedMhz = 0;
    return nullptr;
}

void rfReturn()
{
    if (!borrowed)
        return;
    borrowed = false;
    config.lora.tx_enabled = savedTx;
    if (RadioLibInterface::instance)
        RadioLibInterface::instance->reconfigure(); // Meshtastic's frequency, modem and receive again
}

bool rfBorrowed()
{
    return borrowed;
}

float rfRssiAt(float mhz)
{
    if (!borrowed)
        return -200;
    lora->standby();
    // image calibration once per area; steps inside 902-928 MHz don't need it again
    bool skip = tunedMhz > 0 && fabsf(mhz - tunedMhz) < 30;
    lora->setFrequency(mhz, skip);
    if (!skip)
        tunedMhz = mhz;
    // receive with no interrupts at all, so Meshtastic's DIO1 handler never fires
    lora->startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF, 0, 0, 0);
    delayMicroseconds(1200);
    return lora->getRSSI(false);
}

float rfRssiNow()
{
    return borrowed ? lora->getRSSI(false) : -200;
}

} // namespace badge

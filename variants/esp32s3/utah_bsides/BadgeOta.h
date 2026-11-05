// WiFi firmware updates: the badge web page streams a signed update file (tools/sign_update.py)
// into the firmware slot that isn't running. The badge checks the Ed25519 signature against
// BadgeUpdateKey.h and the image itself, then waits for # on the badge before switching.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace badge
{

enum OtaState { OTA_IDLE, OTA_RECEIVING, OTA_CONFIRM, OTA_REBOOTING, OTA_FAILED };

bool otaAvailable(); // this build has an update key
const char *otaBegin(size_t imageSize, const uint8_t sig[64]); // nullptr when ready for data
bool otaWrite(const uint8_t *data, size_t len);
void otaFinish(); // checks everything; OTA_CONFIRM or OTA_FAILED
void otaAbort();
void otaConfirm(bool yes); // # / * on the badge
OtaState otaState();
const char *otaMessage();     // what happened / what's waiting, for the page and the screen
const char *otaNewVersion();  // version of the received firmware
const char *otaRunningVersion();
size_t otaReceived();

} // namespace badge

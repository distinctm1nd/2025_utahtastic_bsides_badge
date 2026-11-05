// The badge has no clock chip or backup battery: the time is lost with power. It comes back
// from (best first) the Meshtastic app over Bluetooth, the Badge WiFi page (the phone's or
// computer's browser sends its time), a mesh node with GPS, or other badges: a badge that
// doesn't know the time asks ("BTQ"), and a nearby badge that knows answers ("BTM" + time).
// Meshtastic's clock screen stays hidden until the time is known.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace badge
{

bool timeKnown();
void timeTick();                                               // every second, from BadgeMesh
void timePacket(uint32_t from, const uint8_t *b, size_t len); // "BTQ" / "BTM" packets
bool timeSetFromBrowser(uint32_t epoch);                       // Badge WiFi page

// time zone (Meshtastic's config.device.tzdef, a POSIX TZ string). No zone set: Utah's.
void timeZoneDefault();                     // from lateInitVariant
bool timeSetZone(const char *posix);        // applies and saves; false if it isn't a usable TZ string
int timeZoneCount();
const char *timeZoneLabel(int i);           // "US/Mountain"...
const char *timeZoneValue(int i);
const char *timeZoneCurrentLabel();         // the label of the zone in use, or "Custom"

} // namespace badge

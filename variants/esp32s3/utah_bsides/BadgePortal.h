// "Badge WiFi": the badge becomes its own WiFi network (WPA2, a new random password each time,
// one phone / laptop at a time) with a web page (tools/portal/index.html) for the sightings
// log, pictures, settings, typing a message, a live Bluetooth view, backup / restore and
// signed firmware updates. Only while the Badge WiFi screen is open.
#pragma once
#include <stddef.h>

namespace badge
{

// nullptr when the network is up (ssid / password / address filled in), otherwise why not
const char *portalStart(char *ssid, size_t ns, char *pass, size_t np, char *addr, size_t na);
void portalStop();
void portalPoll(); // often (also while the lock screen is up): answers the browser, times it out when idle
bool portalRunning(); // the WiFi + web page is still being served
int portalClients();
const char *portalLastEvent(unsigned *ageS); // "Message sent", "Picture saved", ... (nullptr if none)

} // namespace badge

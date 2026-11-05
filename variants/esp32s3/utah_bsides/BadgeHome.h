// Badge home screen (AppHome.cpp): the pet / chat / clock flip that replaces Meshtastic's page
// carousel as the default screen. homePoll() reclaims the screen; homeYield() steps aside while
// the user is on a Meshtastic page; homeGo() brings it back.
#pragma once
#include <stdint.h>

namespace badge
{

void homeBoot();              // from lateInitVariant: set the default per the saved preference
void homePoll(uint32_t now);  // keypad thread: make home the default when nothing else is up
void homeGo();                // open home now (menu entry / shortcut)
void openHomeEdit();          // open home straight into arrange (show/hide + reorder) mode
void homeYield();             // a Meshtastic page was chosen: stop reclaiming until homeGo()
bool homeActive();            // the home app is the one showing

// home tile order (set here or from the Badge WiFi page)
int tileCount();                    // all tiles (shown ones first, then hidden)
const char *tileIdAt(int i);
const char *tileLabelAt(int i);
bool tileShownAt(int i);
void tileSetOrder(const char *shownIds, const char *hiddenIds); // space-separated tile ids
bool homeEnabled();
void homeSetEnabled(bool on); // Settings toggle (saved)

} // namespace badge

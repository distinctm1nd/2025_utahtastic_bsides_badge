// Badge menu (hold *) and welcome screen, drawn with Meshtastic's own menu banners.
#pragma once

namespace badge
{

void menuOpen();          // main badge menu
void menuPoll();          // call often: opens sub-menus queued from menu callbacks
void menuShowSceneName(); // brief banner with the current LED scene

void welcomeShowIfEnabled(); // at boot
bool welcomeActive();
void welcomeClose();

} // namespace badge

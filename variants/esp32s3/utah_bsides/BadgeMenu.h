// Badge menu (hold *) and welcome screen, drawn with Meshtastic's own menu banners.
#pragma once

namespace badge
{

void menuOpen();          // main badge menu
void menuPoll();          // call often: opens sub-menus queued from menu callbacks
void menuShowSceneName(); // brief banner with the current LED scene
void menuQuickSend(int digit); // hold a digit: send canned message N (asks first)
void menuInit();               // applies the saved screen colour (from lateInitVariant)

void welcomeShowIfEnabled(); // at boot
bool welcomeActive();
void welcomeClose();

} // namespace badge

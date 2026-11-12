// Badge menu (hold *) and welcome screen, drawn with Meshtastic's own menu banners.
#pragma once

namespace badge
{

void menuOpen();      // settings & setup menu (hold centre / *)
void menuOpenGames(); // the Games submenu (from a home tile)
void menuOpenFox();   // the Fox hunt submenu (from a home tile)
// hold-a-number shortcuts, exposed for the Badge WiFi page
int shortcutOptionCount();
const char *shortcutOptionId(int i);
const char *shortcutOptionLabel(int i);
const char *shortcutSlotId(int n); // n 0..9
void shortcutSetSlot(int n, const char *id);
void menuPoll();               // call often: opens sub-menus queued from menu callbacks
bool menuBusy();               // a menu / sub-menu flow is active (so Home should not reclaim the screen)
void menuClearReturn();        // close an app without reopening the menu it came from
void menuShowSceneName();      // brief banner with the current LED scene
void menuQuickSend(int digit); // a "Quick message N" shortcut: send canned message N (asks first)
void menuInitEarly();          // sets the saved screen colour before anything is drawn (from earlyInitVariant)
void menuInit();               // applies the saved screen colour (from lateInitVariant)
void menuBack();               // * on a badge menu: reopen the menu above it
void menuAppClosed();          // an app closed: back to the menu it was opened from

// settings shared with the badge web page
int menuColourCount();
const char *menuColourName(int i);
int menuColour();
void menuSetColour(int i);
bool menuWelcome();
void menuSetWelcome(bool on);

void welcomeShowIfEnabled(); // at boot
bool welcomeActive();
bool menuTagAwake();
void menuSetTagAwake(bool on);
void menuShowSettings();  // reopen the Settings menu (after a picker app)
void menuShortcut(int n); // hold a number key (0-9): its shortcut, picked in Settings > Shortcuts
void welcomeClose();

} // namespace badge

// Screen lock: with a PIN set, the badge locks at startup, when the screen goes to sleep, and
// after 5 minutes without a key press. The lock screen is the name tag (your name, or the
// picture picked for it); pressing a key brings up the PIN line. Settings > Screen lock.
#pragma once
#include <stdint.h>

namespace badge
{

bool lockEnabled();
bool lockShowing();
void lockNow();                          // show the lock screen (if a PIN is set)
void lockAttract();                      // rest on the lock screen even with no PIN: any key -> home
void lockBoot();                         // at startup: locked from the start if a PIN is set
bool lockBootReady();                    // late enough in the boot screens to put the lock up
bool lockLocked();                       // locked until the right PIN is typed
class App;
bool lockBlocks(const App *app);         // no other badge screen opens while locked
void lockPoll(uint32_t keys, uint32_t now); // keypad thread, every 10 ms: raw key state
void openLockSetup();                    // set or change the PIN
void lockTurnOff();
const char *lockPicture();           // "@tag" (the name) or a picture name
void lockSetPicture(const char *what);

} // namespace badge

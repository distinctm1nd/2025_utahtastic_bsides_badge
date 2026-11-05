// "Make it beep": connect to a tracker and ask it to play its sound, the way anti-stalking apps
// do. Trackers only answer someone who isn't their owner once they're away from the owner.
//   Apple AirTag: service 7DFC9000-7D1C-4951-86AA-8D9728F8D66C, write 0xAF to 7DFC9001-...
//   Others (the DULT anti-stalking standard): service 15190001-12F4-C226-88ED-2AC5579F2A85,
//   Sound_Start (opcode 0x0300) to the non-owner control point 8E0C0001-1D68-FB92-BF61-48377421680E
#pragma once
#include <stdint.h>

namespace badge
{

enum BeepState { BEEP_IDLE, BEEP_BUSY, BEEP_DONE, BEEP_FAILED };

bool beepStart(const uint8_t mac[6], bool publicAddr); // false if one is already running
BeepState beepState();
const char *beepMessage(); // what happened, for the screen
void beepClear();          // back to idle once the result was shown

} // namespace badge

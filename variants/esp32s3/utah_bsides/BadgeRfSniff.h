// Borrowing Meshtastic's LoRa radio to measure signal levels on any frequency (the 915 MHz
// scanner). While borrowed Meshtastic can't send (its sends are dropped) or receive; giving
// it back restores its settings with Meshtastic's own reconfigure().
#pragma once

namespace badge
{

const char *rfBorrow(); // nullptr when the radio is ours, otherwise why not
void rfReturn();
bool rfBorrowed();
float rfRssiAt(float mhz); // tune there and read the signal level (dBm)
float rfRssiNow();         // read again where we are tuned

} // namespace badge

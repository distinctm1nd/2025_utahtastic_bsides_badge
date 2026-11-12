// MeshCore as the badge's second mesh: the same badge, with the LoRa radio speaking MeshCore
// instead of Meshtastic. One of the two owns the radio; which one is picked at startup.
// (MeshCore's public US setting needs the SX1262 mod; the stock LLCC68 cannot go below 125 kHz.)
#pragma once
#include <stdint.h>

namespace badge
{

bool meshcoreBuilt();          // this firmware includes MeshCore
bool meshcoreMode();           // the radio is MeshCore's this run
const char *meshName();        // "Meshtastic" or "MeshCore": the mesh the radio speaks this run
void meshcoreStart();          // from lateInitVariant(): reads the saved choice
void meshcoreSetMode(bool on); // saved (NVS "utah" / "meshmode") as the choice offered first at startup
// The startup question "Meshtastic or MeshCore?" as a full-screen badge app: the saved choice is
// highlighted and taken by itself after a few seconds. Picking MeshCore hands it the radio there
// and then. When the mesh is chosen the app closes and calls then() in the same breath, so the
// caller can put the next screen up with no frame of Meshtastic's page in between.
void openMeshBootChoice(void (*then)());
bool meshBootPicked(); // someone chose with a key (false: the question timed out, or was never asked)

// ---- in MeshCore mode --------------------------------------------------------------------------
// MeshCore is bridged into the data the badge's apps already use: its contacts appear as nodes
// (numbered from their public keys), its messages arrive as Meshtastic text packets, and Meshtastic
// channel N is MeshCore group channel N with the same key. So the apps need only these:
bool meshcoreSendText(uint32_t to, const char *text, uint8_t channel); // to: a node number, or broadcast for the channel
// a node made from the name on a group message, with no MeshCore contact behind it (yet): it can be
// shown as a sender but not written to directly
bool meshcoreNameOnly(uint32_t node);
void meshcorePause(bool on); // true while something else (the 915 MHz scanner) borrows the radio
// Radio: 910.525 MHz, SF7, and this bandwidth in kHz. 125 for any badge; 62.5 is MeshCore's public
// US setting and needs the SX1262 mod (false: this radio refused, and it stays on what it had).
float meshcoreBandwidth();
bool meshcoreSetBandwidth(float khz);

} // namespace badge

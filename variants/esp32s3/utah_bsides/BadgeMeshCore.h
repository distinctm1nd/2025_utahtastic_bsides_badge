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
// true: this start is the restart after the other mesh was picked, and that mesh is now in use -
// the question is not asked again
bool meshcoreResumeChoice();
// from earlyInitVariant(): "MeshCore" / "Meshtastic" when this start is that restart, else nullptr
const char *meshcoreLoadingInto();
bool meshBootPicked(); // someone chose with a key (false: the question timed out, or was never asked)

// ---- in MeshCore mode --------------------------------------------------------------------------
// MeshCore is bridged into the data the badge's apps already use: its contacts appear as nodes
// (numbered from their public keys), its messages arrive as Meshtastic text packets, and Meshtastic
// channel N is MeshCore group channel N with the same key. So the apps need only these:
bool meshcoreSendText(uint32_t to, const char *text, uint8_t channel); // to: a node number, or broadcast for the channel
// a node made from the name on a group message, with no MeshCore contact behind it (yet): it can be
// shown as a sender but not written to directly
bool meshcoreNameOnly(uint32_t node);
// The two meshes are kept apart: each has its own channel list, its own nodes and its own message
// history, swapped in when the mesh is chosen at startup. In MeshCore mode the badge's lists leave
// out the nodes that are Meshtastic's (true here); in Meshtastic mode MeshCore's are not there at all.
bool meshcoreHidesNode(uint32_t node);
void meshcorePause(bool on); // true while something else (the 915 MHz scanner) borrows the radio

// ---- what the MeshCore settings menu (BadgeMeshCoreSettings.cpp) works on ----------------------
// The radio. Unless changed: 910.525 MHz / 125 kHz / SF7, which every badge can do. MeshCore's public
// meshes mostly use 62.5 kHz, which needs the SX1262 mod: false from meshcoreSetRadio() means this
// badge's radio refused the setting, and it stays on what it had.
struct McRadioCfg {
    float freq = 910.525f; // MHz
    float bw = 125.0f;     // kHz
    uint8_t sf = 7;
    uint8_t cr = 5;    // 5..8, for 4/5..4/8
    int8_t power = 22; // dBm
};
McRadioCfg meshcoreRadio();
bool meshcoreSetRadio(const McRadioCfg &c);
void meshcoreAdvertise(bool flood); // announce this badge: to those in range, or relayed across the mesh
// contacts: everyone this badge can write to (people, and repeaters / rooms it has heard)
struct McContact {
    char name[32];
    uint8_t type; // 1 person, 2 repeater, 3 room server, 4 sensor
    bool favourite;
    int hops;      // of the known route; -1: none known, messages flood
    uint32_t node; // its number among the badge's nodes
};
int meshcoreContactCount();
bool meshcoreContact(int i, McContact &out);
bool meshcoreContactFav(int i, bool on);
bool meshcoreContactResetPath(int i); // forget the route: the next message floods and finds a new one
bool meshcoreContactDelete(int i);
// heard announcing but not added (auto-add off, or the list full)
int meshcoreHeardCount();
const char *meshcoreHeardName(int i);
bool meshcoreHeardAdd(int i);
bool meshcoreAutoAdd(); // add whoever is heard announcing as a contact (on unless turned off)
void meshcoreSetAutoAdd(bool on);
bool meshcoreShareLocation(); // put the badge's position (from Meshtastic's settings) in its announcements
void meshcoreSetShareLocation(bool on);
void meshcoreHashtagKey(const char *name, uint8_t *key16); // the key of MeshCore hashtag channel "#name"

} // namespace badge

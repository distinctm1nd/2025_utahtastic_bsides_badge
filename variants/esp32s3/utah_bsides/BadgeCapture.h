// Packet capture without an SD card: mesh packets the badge receives go into a pcap file on
// its file system (/badge/capture.pcap, up to 1 MB) for Wireshark. LoRaTap framing (link type
// 270, sync word 0x2b) and Meshtastic's 16-byte packet header, as read by the Exploiteers
// Wireshark plugin (github.com/exploiteers/Exploiteers-Wireshark-LoRa). Badge code only sees
// packets Meshtastic could decrypt, and stores them decrypted: set the plugin's key to empty.
// Download it from Badge WiFi.
#pragma once
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <stdint.h>

namespace badge
{

// mesh (LoRa) capture -> /badge/capture.pcap (LoRaTap)
bool captureOn();
void captureSet(bool on); // not saved: off after a reboot
uint32_t captureCount();  // packets in the file
bool captureFull();
void capturePacket(const meshtastic_MeshPacket &p);
void captureClear();

// WiFi capture -> /badge/wifi.pcap (raw 802.11). Frames come from the WiFi sniffer.
bool captureWifiOn();
void captureWifiSet(bool on);
uint32_t captureWifiCount();
bool captureWifiFull();
void captureWifiFrame(const uint8_t *frame, uint16_t len, uint8_t channel);

// BLE advertising capture -> /badge/ble.pcap (BLE link layer). Adverts come from the BLE scan.
bool captureBleOn();
void captureBleSet(bool on);
uint32_t captureBleCount();
bool captureBleFull();
void captureBleAdv(const uint8_t macPrinted[6], bool randomAddr, uint8_t advType, const uint8_t *adv, uint8_t advLen);

void captureClearAll(); // removes all three files

// live stream to Wireshark over Badge WiFi (writes nothing to flash while streaming).
enum { STREAM_OFF = 0, STREAM_MESH = 1, STREAM_BLE = 2 };
void captureStreamSet(int kind);
int captureStreamKind();
uint32_t captureStreamLinktype(); // pcap link type for the current stream (270 mesh, 251 BLE)
int captureStreamDrain(uint8_t *out, int max); // pulls pending stream bytes (for the TCP socket)

} // namespace badge

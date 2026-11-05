# Fox hunt rules

Each `.json` file here marks devices in the WiFi / Bluetooth fox hunt. The format follows the
Helgatchi rule files (name, title, type, tag, criteria), and the patterns here are written
against the official lists in `..` (IEEE address prefixes, Bluetooth SIG company IDs and member
service UUIDs). A device that meets any one criterion gets the rule's title and tag.

- `type`: `ble`, `wifi` or `auto` (both)
- `tag`: Surveillance, Tactical, Camera, Tracker, Attack, Skimmer and Drone are highlighted on the badge; others
  (Networking, Defense, Smart Home, ...) are shown but not highlighted
- criteria (all patterns ignore case):
  - `oui`: address prefixes (`AA:BB:CC`); `oui_org`: regex on the IEEE company name
  - `mfg`: Bluetooth company IDs (`0x067C`); `mfg_org`: regex on the SIG company name
  - `mfg_data`: company ID and first data byte (`004C:12` = Apple Find My)
  - `service`: 16-bit Bluetooth service UUIDs (`FEED`)
  - `ssid` (WiFi network) / `name` (Bluetooth name): whole-name patterns using `.`, `.*`
    and `\d`, e.g. `.*ipcam.*` or `MV\d\d\d\d\d\d.*`
- `ie_sig` and other keys are ignored (the badge's WiFi scan doesn't see them)

Address prefixes only count for registered addresses (not random Bluetooth or
randomised WiFi ones). The build prints how many prefixes / IDs each rule ends up with.
Rule files placed in `../rules.local/` (not committed) are read the same way.

Some marks are built into the scanner (`BadgeRules.cpp`) because they need more than a pattern:
drones (Remote ID over Bluetooth or WiFi beacons), Pineapple-style network spraying, evil twins,
and trackers that say whether they're away from their owner (Apple Find My: the long 0x12 / 0x19
"offline finding" advert; Google's Find My Device network: Eddystone frame 0x41, "unwanted
tracking" mode). Those come before the rule files, so `apple_findmy.json` is only a fallback.

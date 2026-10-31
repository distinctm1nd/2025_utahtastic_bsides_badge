# Utah BSides 2025 badge ("UTAHtastic")

Meshtastic firmware for the Utah BSides 2025 badge, kept as a **branch of a fork of
[meshtastic/firmware](https://github.com/meshtastic/firmware)**. Everything badge-specific is
in this folder, plus three small files elsewhere:

| File | What it is |
|---|---|
| `variants/esp32s3/utah_bsides/` | this folder: pins, keypad, LED scenes, badge menu |
| `userPrefs.jsonc` | badge defaults: US region, MediumFast, UTAHtastic splash image |
| `.github/workflows/badge_build.yml` | builds the badge firmware on GitHub |

No Meshtastic source file is patched, so a new Meshtastic release merges in cleanly.

## Using the badge

| Key | Typing a message | In a menu |
|---|---|---|
| 1–9, 0 | tap for letters (like an old phone), **hold for the digit** | — |
| `#` | send (Enter) | choose |
| `*` | delete a letter; back out of an empty message | close |
| joystick | move the cursor | move up/down |
| joystick press | next screen; hold = Meshtastic menu | next item; hold = choose |
| hold `*` | **badge menu** | |
| hold `#` | next LED scene | |

Typing: tap a key once for its first letter, tap again within a second for the next one
(`2` `2` `2` = `c`). Pause, or press another key, to start a new letter. `0` is space, `1`
is punctuation (`. , ? ! ' - @ :`).

Badge menu (hold `*`): LED scene, speed and brightness, whether the moon LEDs follow the
scene or keep the colour set in the app (Ambient Lighting), key flash, welcome tips, key
help, factory reset. Settings are kept across reboots and firmware updates, and a badge
updated from the 2025 firmware keeps the light pattern its owner had picked.

The Konami code still works (joystick up up down down left right left right, then `2` `2`).

### LED scenes

35 scenes for the 13 key LEDs and the 6 logo LEDs (and the moon, if you let it follow):

- **2025 patterns**: Rainbow, Blue + White, Red + Maroon, Pink + White, Pink + Purple, Green + Blue
- **Animated**: Rainbow Wave, Breathe, Snake, Scanner, Digital Rain, Fire, Plasma, Starry
  Night, Aurora, Heartbeat, Type Ripple (each key press sends a ripple across the keys)
- **Utah**: Red Rocks Sunset, Desert Heat, Canyon Storm
- **Halloween**: Candlelight, Jack-O-Lantern, Lurking Eyes, Ghosts, Blood Moon, Cauldron,
  Graveyard Mist, Embers, Will-O-Wisp, Candy Corn, Possessed, Dead Man's Hand, Nevermore,
  Shadow Walker
- Off

To add one, write a function in `BadgeScenes.cpp` and add it to `SCENES[]` at the bottom.
`tools/scene_preview.cpp` renders every scene to a GIF on your PC so you can see it before
flashing:

```
cd variants/esp32s3/utah_bsides/tools
g++ -std=c++17 -O2 -I.. scene_preview.cpp ../BadgeScenes.cpp -o scene_preview
./scene_preview > scenes.json && python3 scene_preview.py scenes.json   # needs Pillow
```

## What changed from the 2025 firmware

- Based on Meshtastic **2.7.26** (was a copy of 2.5.12 from November 2024).
- **Keyboard**: the old driver had no debounce, so a single press of a key often counted as
  two or three presses, and multi-tap turned that into the wrong letter. Each key now has
  to settle for 30 ms. Multi-tap only replaces a letter it knows reached the message, `#`
  and `*` act on release so holding them can do something else, and the joystick only
  moves (UP no longer sends, DOWN no longer deletes), which matches the 2.7 menus.
  `tools/keypad_test.cpp` checks this on a PC.
- **LEDs**: their own 50 Hz thread instead of a patch inside Meshtastic's
  `AmbientLightingThread.h`. The old patch only animated while Ambient Lighting was on.
- **LoRa**: same pins; the firmware now finds either the stock LLCC68 module or an SX1262
  module (see below). DIO2 now drives the module's RF switch, which is the usual wiring
  for these modules; check range against an old-firmware badge on first test.
- Splash: the UTAHtastic image moved from `userPrefs.h` (gone in 2.7) to the `USERPREFS_OEM_*`
  settings in `userPrefs.jsonc`.

## LongFast: swapping the LoRa module

The stock module (DreamLNK **DL-LLCC68-S-915S**) uses the LLCC68 chip, a cut-down SX1262
that can't do spreading factor 11 at 250 kHz. LongFast is SF11 at 250 kHz, so stock badges
can't join it; MediumFast (SF9) is the fastest common preset they can use.

The closest replacement is the **Heltec HT-RA62** (SX1262): same 16-pad footprint
(Ai-Thinker Ra-01 style), same pins **except BUSY**, which is on pad 10 instead of pad 5.
Its pad 5 is TXEN, an input to its RF switch, so it must not be left connected to the
badge's BUSY line (GPIO14).

| Pad | DL-LLCC68 (stock) | HT-RA62 | Badge net |
|---|---|---|---|
| 1 | ANT | ANT | SMA |
| 3 | 3.3V | VDD | +3V3 |
| 4 | RESET | RST | GPIO21 |
| **5** | **BUSY** | **TXEN** | **GPIO14** |
| 6 | DIO1 | DIO1 | GPIO13 |
| 7 | NC | DIO2 | (unconnected) |
| 8 | DIO3 | DIO3 | LORA_DI03 |
| **10** | NC | **BUSY** | (unconnected) |
| 11 | NC | RXEN | (unconnected) |
| 12–15 | SCK, MISO, MOSI, NSS | same | GPIO3, 9, 10, 11 |
| 2, 9, 16 | GND | GND | GND |

The mod, per badge:

1. Remove the DL-LLCC68 (hot air).
2. Cover the badge's pad 5 with a small piece of Kapton so the HT-RA62's pad 5 (TXEN) does
   not solder to it, and solder the HT-RA62 down.
3. Run a short wire from the HT-RA62's pad 10 (BUSY) to the badge's pad 5 copper
   (GPIO14; any point on that trace works).

No firmware change is needed: this build tries the SX1262 first and falls back to the LLCC68,
so one firmware runs on stock and modded badges. On a modded badge, pick LongFast in the
app. Badges on MediumFast and LongFast can't hear each other, so a group should pick one.

The Ai-Thinker Ra-01SH (SX1262) uses the same pinout as the HT-RA62, with TXEN/RXEN RF-control
pins of its own. If those must be driven by the MCU it needs two more wires and GPIOs, so check
its datasheet first; the HT-RA62 is the swap the Meshtastic docs recommend for the
CircuitMess Chatter, an LLCC68 board with the same problem (and the same BUSY jumper).

## Building

**On GitHub:** every push to the `utah-bsides-badge` branch builds the firmware. The .bin
files are under the run's *Artifacts* on the Actions tab. Push a tag named `badge-v…` (for
example `badge-v2.7.26-1`) to publish them as a release. Actions on a fork start disabled;
enable them once under the repository's Actions tab.

**On your computer** (PlatformIO installed):

```
pio run -e utah_bsides                 # build
pio run -e utah_bsides -t upload       # flash over USB
pio device monitor -b 115200           # serial log
```

Flash with the web flasher instead: use `firmware-utah_bsides-*.factory.bin` for a first
install (erases the badge) and `firmware-utah_bsides-*.bin` to update.

## Updating to a new Meshtastic release

```
git remote add upstream https://github.com/meshtastic/firmware.git   # once
git fetch upstream --tags
git checkout utah-bsides-badge
git merge v2.7.27.xxxxxxx          # the release tag you want
git push
```

Use release tags (stable), not `develop`, and read the release notes for API changes. The
places most likely to need a touch after a big release are the input event names
(`src/input/InputBroker.h`) and the menu banner API (`src/graphics/Screen.h`), both used in
`BadgeKeypad.cpp` and `BadgeMenu.cpp`.

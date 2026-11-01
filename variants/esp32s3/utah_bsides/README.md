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

One rule on every screen:

| Control | Does |
|---|---|
| joystick up / left | previous (item, row, page) |
| joystick down / right | next |
| **joystick centre press** | **select / OK** (in apps it does what `#` does) |
| hold joystick centre (or hold `*`) | badge menu |
| `*` | back / delete a letter |
| tap a digit (on Meshtastic's screens) | new message in the badge editor |
| hold a digit (on Meshtastic's screens) | quick-send canned message N |
| hold `#` | next LED scene |

In the editor, digits type letters (tap again for the next letter, hold for the digit) and
`#` or the centre press sends. In games, the joystick plays and the centre/`#` starts.

Replies from Meshtastic's own message screen use the plain multi-tap typing straight into its
text box. New messages go through the badge editor, which shows the letters of the key you
are tapping in a strip at the bottom, the case mode, and how many characters are left.

### Badge menu (hold the centre press, or hold `*`)

- **Name tag & pictures**: your name in big letters (type it on the badge; until you do it
  shows your Meshtastic name), with an optional tagline under it; then the pictures from
  `images/`. Joystick flips pages, `#` starts a slideshow, hold `#` edits the name. On the
  name page `1` changes the font (Classic, Wood type, Serif, Typewriter, Fancy) and `2` the
  size (Auto = as big as fits, Big, Medium, Small); both are also in Settings.
- **New message**: the badge editor (sends to everyone on the primary channel).
- **Lights**: select *Scene* to open the scene list; the LEDs change as you scroll, select
  keeps it and `*` goes back to the one you had. Also speed, brightness, moon, key flash and
  light shows (select a row to change it). A map of the badge shows which LEDs are lit.
- **Games**: *Skinwalker Mine*, a Doom-style 3D shooter (below); *Chess* against the badge
  (Easy / Normal / Hard) or two players; Simon and Whack-a-mole (on the lit keys), Snake,
  Blocks, Tic-tac-toe against the badge or against another badge (keys 1–9 are the board;
  X red, O blue).
- **Fox hunt**: pick a node; the screen and a bar graph on the keys show its signal as you
  walk ("Warmer" / "Colder"). Any Meshtastic node can be the fox.
- **Badges & achievements**: badges you've met, 16 achievements, scavenger-hunt flags,
  high scores.
- **Minibadges**: minibadges that talk I2C (button presses and text messages).
- **Settings**: joystick setup, name tag name, font, size and tagline, whether the name tag keeps the
  screen on, screen colour, LEDs while the screen sleeps (stay on / dim to 30% / off; stays
  on unless you change it), night mode (red screen, dim LEDs), remote `!commands`, welcome
  tips, factory reset.

### Joystick setup

The joystick is mounted at an angle, so a straight push can close one of its four switches
or two of them, and not the one its label suggests. Out of the box the badge assumes the
usual 45° turn. If directions come out wrong, Settings → *Joystick setup* teaches the badge
how your stick is mounted, whatever the angle (or even if it is mirrored):

1. Push straight up, right, down and left as the screen asks (as you hold the badge),
   letting go each time. The four circles show which switches each push closes.
2. Push around: an arrow shows what each push now means. The line under it (e.g.
   `U14 R12 D23 L34`) is what the badge learned. `0` flips the "lean" (which way a push
   that only catches one switch goes), `9` starts over.
3. Centre (or `#`) saves, `*` cancels.

The badge waits 60 ms after the first switch closes to see whether a second one follows, so
angled pushes count as one direction.

### Mesh features

- **Alerts**: the keys pulse three times in cyan for a direct message, once in green for a
  channel message; the logo "eyes" blink on any mesh traffic.
- **Badges met**: badges are nodes reporting hardware model PRIVATE_HW (like this one). Meeting
  a new one gives a light burst and counts towards achievements.
- **Light shows**: Lights → *Share now* sends your scene and its timing to every badge in
  range, and they all play it in step. *Lead show* re-sends it every minute. Turn
  *Join shows* off to ignore other people's shows. Joined shows are not saved.
- **Remote commands**: a direct message to the badge starting with `!` changes its lights and
  gets a reply: `!lights next`, `!lights list`, `!lights skinwalker`, `!bright high`,
  `!speed slow`, `!help`. Turn it off in Settings.
- **Phone colour**: the APP COLOUR and APP SHIMMER scenes use the colour set in the
  Meshtastic app under Module settings → Ambient Lighting.
- **Scavenger hunt**: see below.

The badge's own traffic is small: light shows and pings are a few bytes on the private app
port (256), sent only when you ask (or once a minute while leading a show / every 20 s
during a fox hunt).

### Skinwalker Mine

A Doom-style shooter: a raycaster draws the old silver mine in 3D at about 15 frames a
second. Seven skinwalkers lurk in it; clear them all. You have a six-shooter (reloads
itself when empty, or press `0`), and there are bullets and medicine lying around.

| Control | Does |
|---|---|
| joystick up / down | walk forward / back |
| joystick left / right | turn |
| `4` / `6` | step sideways |
| `2` / `8` | walk (so you can walk and turn with the stick at once) |
| centre, `#` or `5` | shoot (and start / play again) |
| hold `*` | quit |

The keys show your health (green to red), flash when you shoot and go red when you're hit;
the skull's heartbeat speeds up as a skinwalker closes in. The engine is `BadgeMine.h`;
`tools/mine_preview.cpp` renders frames on a PC and checks the level can be finished.
Real Doom doesn't fit: it needs several MB of RAM (this board has no PSRAM) and a 4 MB
game file.

### Chess

Play White, Black (the board turns round) or two players passing the badge, against the
badge at Easy, Normal or Hard. Move the cursor with the joystick, centre picks up a piece
(dots show where it can go) and puts it down; `*` puts it back, hold `*` quits. Castling,
en passant and promotion (to a queen) are all there; checkmate, stalemate and the 50-move
rule end the game. The engine (`BadgeChess.h`) is checked against the standard perft move
counts in `tools/chess_test.cpp`.

### Pictures and memes

Put `.png .jpg .gif .bmp .webp` files in `images/` and build. `tools/make_images.py` runs
before every build, scales each picture to fit the 160×128 screen and dithers it to one
colour (Meshtastic's screen driver draws a single colour on black; pick the colour in
Settings). Animated GIFs play (up to 24 frames). Files are shown in name order, so number
them (`01_…`, `02_…`). Pictures with light backgrounds usually look better inverted: put
`_inv` in the name. About 2.5 KB per frame; the script stops at 512 KB.

The Old West sample pictures (`04_wanted`, `05_skinwalker` (animated), `06_mesh_marshal`,
`07_ghost_town` (animated), `08_dont_answer`) are drawn by
`tools/make_west_images.py`; delete any you don't want.

### Name tag fonts

`BadgeFonts.h` holds the extra name fonts in Meshtastic's own font format, made by
`tools/make_fonts.py` from open-licence fonts (DejaVu, Liberation, GNU FreeFont) at three
sizes each. Edit the font list in that script and run it to change them.

### Scavenger hunt

Edit the flag table in `BadgeHunt.cpp` (name, 8-character token, clue to the next spot).
Build one beacon badge (or any Meshtastic-capable board using this variant) per flag with
`-D BADGE_HUNT_BEACON=<flag number>` added to `build_flags`; it broadcasts its flag every
3 minutes. Badges that hear it collect the flag and show the clue. Finding all of them
unlocks an achievement.

### Minibadges

The three minibadge slots carry I2C on GPIO5 (SDA) and GPIO6 (SCL). The badge polls
minibadges that follow the I2C part of the minibadge standard every 2 s: a button press gives
a light burst, a text message pops up on screen, and minibadges that accept writes get the
LED brightness and your latest game score. The minibadge CLK is left exactly as it was: 1 Hz.

### LED scenes

Each scene is a colour palette plus its own effect for each part of the badge, so the
logo, eyes, moon and keypad all move differently while the colours belong together. Key
presses turn the key a contrasting colour for a moment so you can see the press counted.
Changing scene crossfades over about half a second.

- **Typing**: while a text box is open (the badge editor or Meshtastic's own), the keys
  light up warm white at least at "High" brightness so the printing on them is readable,
  and a key you press turns green.
- **The skull** left of `*` has its own light: it glows with the scene's eye effect, not
  the keypad's. When a message arrives it strobes (cyan for a direct message, green for a
  channel), then pulses slowly until you press any key.

| Part | Effects |
|---|---|
| Keypad | wave, rain, sparkle, fire, snake, ripple, scanner, breathing rows, plasma, checkerboard, spiral, equaliser, stalking eyes, dust devil, lightning storm, campfire embers, saloon marquee, tumbleweed, rattlesnake |
| Logo (the B sign) | colour flow, marquee chase, flickering neon, breathe, fireflies, heartbeat, colour steps, swinging lantern, sheriff's star glint, saloon bulbs, storm |
| Eyes | glow and blink, look around, candle flicker, demon flare, wink, colour cycle, ping-pong, eyes in the dark, coyote eyeshine |
| Moon | phases, glow, circling halo, eclipse, star twinkle, aurora shimmer, clouds drifting past, howling moon, storm, dust haze |

48 scenes, all named for the theme. **Old West**: Skinwalker, Shapeshifter, Coyote Moon,
Campfire, Saloon, Sheriff's Star, Wanted Poster, Dust Devil, Rattlesnake, Bone Orchard, Dead
Man's Hand, Thunderbird, Ghost Town, Silver Mine, High Noon. Lightning (Thunderbird) strikes
every couple of minutes, plus once a few seconds after you pick the scene; it is worked out
from the scene time, so the keypad, sign and moon flash together, and so do badges in a
light show. Then the earlier colours under new names: Painted Desert (rainbow), Cavalry Blue,
Blood on the Saddle, Desert Rose, Prickly Pear, Turquoise Trail (the 2025 colours), Red Rocks
Sunset, Desert Night, Gold Rush, Midnight Rodeo, Boomtown, Flash Flood, Frozen Pass, Prairie
Fire, Branding Iron, Snake Oil, Spirit Lights, Juniper Fireflies, General Store, Mirage,
Moonshine, Witching Hour, Harvest Lantern, Blood Moon, Ghost Riders, Hexed, Boot Hill,
Possessed, Cornfield, Watering Hole (calm keys that ripple as you type), Your Brand and Your
Brand Glitter (the colour set in the phone app), and Lights Out. The old names still work
in `!lights` and in saved settings.

The moon is part of the scenes by default; Lights → Moon switches it to the Ambient
Lighting colour set in the Meshtastic app instead.

To add a scene, add a line to `SCENES[]` at the bottom of `BadgeScenes.cpp`: a name, a
palette and one effect for each part. New palettes are four colours (base, accent,
highlight, dark) in `PALETTES[]`.
`tools/scene_preview.cpp` renders every scene to a GIF on your PC so you can see it before
flashing:

```
cd variants/esp32s3/utah_bsides/tools
g++ -std=c++17 -O2 -I.. scene_preview.cpp ../BadgeScenes.cpp -o scene_preview
./scene_preview > scenes.json && python3 scene_preview.py scenes.json   # needs Pillow
```

`tools/keypad_test.cpp` tests the keypad and editor behaviour the same way.

### Power

The badge runs on 3 AA cells, and the battery isn't wired to the ESP32, so the firmware
can't show a battery level. The LEDs are the biggest drain you control: Settings can dim
them to 30% or turn them off while the screen sleeps, and night mode caps them. The firmware
also caps the total LED current, because a sagging supply makes the LEDs misread their data
and flash random colours.

## What changed from the 2025 firmware

- Based on Meshtastic **2.7.26** (was a copy of 2.5.12 from November 2024).
- **Keyboard**: the old driver had no debounce, so a single press of a key often counted as
  two or three presses, and multi-tap turned that into the wrong letter. Each key now has
  to settle for 30 ms. Tapping a key again within a second cycles its letters (8 8 = u), `#`
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

If the build stops with the firmware "too big" for the partition, check the marking on the
ESP32-S3 module: an N8 or N16 module can use `board_build.partitions = default_8MB.csv`.

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

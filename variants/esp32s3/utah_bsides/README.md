# Utah BSides 2025 badge ("UTAHtastic")

Meshtastic firmware for the Utah BSides 2025 badge is a fork of
[meshtastic/firmware](https://github.com/meshtastic/firmware)**. Everything Utah BSides badge specific is
in the Utah Bsides variant folder:

| File | What it is |
|---|---|
| `variants/esp32s3/utah_bsides/` | this folder: pins, keypad, LEDs, badge apps, games, scans, messages, web page |
| `userPrefs.jsonc` | badge defaults: US region, MediumFast, the raven boot image |
| `.github/workflows/badge_build.yml` | builds, signs and publishes the badge firmware on GitHub |

Contents: [TL;DR — get started](#tldr--get-started) · [What's new](#whats-new-compared-with-the-2025-firmware) ·
[Using the badge](#using-the-badge) · [Messages](#messages) · [Fox hunt and scanning](#fox-hunt-and-scanning) ·
[Mesh settings](#mesh-settings-no-phone-needed) · [Shortcuts](#shortcuts) ·
[Screen lock](#screen-lock) · [Name tag](#name-tag) · [Games](#games) · [Lights](#lights) · [QR code](#qr-code) · [Stopwatch](#stopwatch--timer) ·
[Time](#time) · [Badge WiFi](#badge-wifi-phone) · [Other mesh features](#other-mesh-features) ·
[Hardware notes](#hardware-notes) · [Building and installing](#building-and-installing) ·
[Updating Meshtastic](#updating-to-a-new-meshtastic-release) · [Third-party code](#third-party-code-and-data)

## TL;DR — get started

**Install it (easiest, no software to install):**

1. On a computer, open the **web installer** in **Chrome or Edge**:
   <https://distinctm1nd.github.io/2025_utahtastic_bsides_badge/> (live once a release is published).
2. Plug the badge into USB and click **Install**. First install erases and reflashes (a 2025 badge
   needs this one full install for the new memory layout). Once you have updated once via web installer, you can continue with web installer for future updates or you can update via WiFi (see below). 
3. That's it. To reflash or move to a newer build later, open the same page and Install again.

(Developers can instead `pio run -e utah_bsides -t upload` — see [Building and installing](#building-and-installing).)

**First boot:**

- Set your **region** again (US) if asked — the one full install starts Meshtastic's settings fresh.
- Set a **name** (Settings → *Name tag*), and a **PIN** if you want the screen lock (Settings → *Screen lock*).

**Drive it (works on every screen):**

| Do this | For |
|---|---|
| **joystick** up / down / left / right | move around |
| **centre press** (or `#`) | select / open |
| `*` | back / delete a letter |
| **hold** centre or `*` | the badge menu |
| **hold** a number | its shortcut |

Note: The joystick is rotated 45 degrees. A generic correction has been applied but to fine tune the joystick for your badge, go to: Main Menu (Tiles) -> Settings and Setup -> Settings -> Joystick setup. Follow the onscreen instructions to calibrate your joystick. 

From the home tiles: **Messages**, **Games**, **Fox hunt**, and more — move with the joystick, press
`#` to open. Type with the number keys (tap again within a second for the next letter, e.g. `8 8` = u).

**Update later without a cable:** menu → **Badge WiFi (phone)**, join the badge's WiFi, open the
page, **Update**.

## What's new compared with the 2025 firmware

The badges shipped with a copy of Meshtastic 2.5.12 with a patched LED thread, and a keyboard driver. This firmware has been updated to the most recent version of Meshtastic **2.8.2** plus the following:

**Messaging**
- Updated keyboard controls and updated messaging features have made the badge a standalone off grid messaging device. It can still be used with the meshtastic phone app, but that is no longer a requirement for full messaging functionality. 
- **Messages**: List of chats (each channel, each person), each chat on its own, newest at the bottom, with a writing box under the conversation. Writing only ever goes to the open chat.
- **Clear privacy labels**: group or person icons, a closed or open lock, and words in the
  title bar ("private", "public group", "no key: can't DM").
- **Private groups**: create or join channels on the badge; **invite another badge** (the key
  goes end-to-end encrypted, the other badge presses `#` to join); a **QR code** for phones;
  **change the key and re-invite** to remove someone; **leave** a group.
- **Names instead of numbers**: the badge asks unknown senders again for their name and key.
- Multi-tap typing with a letter strip, hold `*`/`#` to type those symbols, and the Badge WiFi
  page for typing on a phone.

**Fox hunt and scanning**
- LoRa fox hunt with **RSSI and SNR meters**, "Warmer / Colder" and a bar graph on the keys.
- **WiFi and Bluetooth hunts** with makers from the official IEEE and Bluetooth SIG lists.
- **Surveillance, police, camera and tracker marks** from rule files, highlighted at the top.
- **Following-you detection** (10 minutes with you, or it moved with you), alerts, a
  **background watch** while the hunt is closed.
- **Attack detection**: WiFi deauth floods, Bluetooth pop-up spam, evil twins, Pineapple-style
  network spraying, Flipper Zeros.
- **Drone Remote ID**, a **turn-in-a-circle** direction finder, a **sub-GHz band scanner**,
  a **node radar** map, a **sightings log**, share-on-request, **be the fox**, **packet capture**
  for Wireshark.
- **Packet Capture** for LoRa, WiFi and Bluetooth 

**Security**
- **Screen lock** with a PIN (salted hash); the badge re-locks after the boot intro, on sleep, and
  when idle. By default the lock screen cycles your pictures and GIFs (plus your name, once you've
  set one), or you can pin it to one picture, cycling favourites, or a clock. `*`+`#` locks at once.
- Signed **firmware updates over WiFi**; the badge refuses anything not signed by the badge team.

**Name tag and pictures**
- Name tag with **17 fonts** (12 Old West / dark ones), a preview picker, sizes, a tagline,
  scrolling long names, a status line (nodes near, unread message).
- Old West cryptid pictures and GIFs (raven, skinwalker, jackalope, wendigo, thunderbird, mothman
  and more), the raven on Meshtastic's boot screen, and **your own pictures and GIFs** from a phone.

**Games**
- **Skinwalker Mine** (a Doom-style 3D shooter, five chapters), **Chess** (three levels, two
  players), a **CHIP-8 arcade** with 12 games,
  plus Solitaire (Klondike), Mahjong, Simon, Whack-a-mole, Snake, Blocks and tic-tac-toe against
  the badge or another badge.

**Lights**
- 48 themed **LED scenes** with separate effects for keypad, sign, eyes and moon, smooth
  crossfades, **light shows** in sync across badges, typing lights, a message skull, a
  **flashlight**, and night mode.

**Everyday**
- **Shortcuts**: hold a number key to jump anywhere (ten, configurable on the badge or the Badge WiFi *Home* tab).
- **Explore** a Bluetooth device from a scan (read only): what services and details it offers.
- **QR code** on the screen: your contact card, a link or any text, for people to scan.
- **Stopwatch & countdown timer** (a talk timer).
- A **Badge page** among Meshtastic's screens: time, last message, last node heard, scan
  summary, whether the badge can send.
- **Time without a clock chip**: from the phone app, the Badge WiFi page, GPS nodes or other
  badges; a time zone setting (Utah by default).
- **Mesh settings on the badge**: names, channels, power, hop limit, region and more without
  the phone app.
- **Badge WiFi**: the badge's own WiFi network and web page for pictures, settings, messages,
  the live scan, a map, backup and updates.

**Under the hood**
- **Keyboard**: the old driver had no debounce, so a press often counted two or three times and
  multi-tap typed the wrong letter. 
- **LEDs**: their own 50 Hz thread (was a patch inside Meshtastic's `AmbientLightingThread.h`
  that only animated while Ambient Lighting was on), and their own RMT driver (see *Lights*).
- **LoRa**: the firmware finds either the stock LLCC68 module or an SX1262 module; DIO2 drives
  the RF switch.
- **Memory**: uses the module's full 16 MB of flash and 2 MB of PSRAM (two 6 MB firmware slots
  for WiFi updates, ~3.9 MB of file space). Pictures are stored packed, so every Meshtastic
  module stays in.
- The boot image moved from `userPrefs.h` (gone in 2.7) to `USERPREFS_OEM_*` in `userPrefs.jsonc`.

## Using the badge

One rule on every screen:

| Control | Does |
|---|---|
| joystick up / left | previous (item, row, page) |
| joystick down / right | next |
| **joystick centre press** | **select / OK** (in badge screens it does what `#` does) |
| hold joystick centre, or hold `*` | badge menu (inside a group chat: the group's menu) |
| `*` | back / delete a letter |
| tap a number key (on Meshtastic's pages) | Messages: pick a chat to write in |
| **hold a number key** | its **shortcut** (Settings → *Shortcuts*; out of the box 1 sightings log … 0 flashlight) |
| hold `#` | next LED scene |
| `*` and `#` together (anywhere) | lock now (once a PIN is set) |

**Typing** (messages, names, taglines): number keys type letters; tap the same key again
within a second for the next letter (8 8 = u), hold it for the digit. A strip at the bottom
shows the key's letters. `0` is a space, `1` is punctuation (`. , ? ! ' - @ : + / =`), hold `*`
or `#` to type that symbol, joystick left/right moves the cursor, up changes capitals (abc /
Abc / ABC), `*` deletes (with nothing typed it closes the box), `#` sends or saves. Replies
from Meshtastic's own screens use the same multi-tap straight into its text box.

### Home screen

On login the badge shows its **tile launcher** -- a grid of the apps. The joystick moves around
the grid (**left / right** between columns, **up / down** between rows), and **`#`** opens the
highlighted tile. The tiles are: **Messages, Games, Fox hunt, Badges, Minibadges, BT remote, QR
code, Timer, Flashlight, Nodes, Mesh pages, Clock, Settings**. "Mesh pages" opens an overlay menu
(over the tiles) to jump straight to one of Meshtastic's own screens; "Clock" shows the time (`*`
returns to the tiles). Closing any app returns to the tiles.

**Arrange the tiles on the badge**: tap `0` on the launcher (or *Settings → Arrange home tiles*).
In arrange mode the joystick moves the highlight, `#` picks a tile up / puts it down (move it with
the joystick while carried), `0` shows / hides a tile (hidden ones are struck through), and `*`
saves and exits. You can also do it from the Badge WiFi page (*Home* tab: tick to show, arrows to
reorder). Turn the tile home off entirely with **Settings → Home screen** (`badge` ↔ `Meshtastic`)
for the stock Meshtastic carousel.

Hold the centre press or `*` for the **Settings & setup** menu. With a PIN lock set, the lock
screen comes first; the tiles appear after you unlock.

### Settings & setup menu

Hold the centre press (or `*`) anywhere. This menu is just the configuration items -- the apps
are on the home tiles.

- **Name tag & pictures**: your name, then the pictures (see *Name tag*).
- **Lights**: scene list with live preview (the LEDs change as you scroll; select keeps it,
  `*` goes back to the one you had), speed, brightness, moon, brand colour, key flash, light
  shows. A map of the badge shows which LEDs are lit.
- **Mesh settings**: see *Mesh settings*.
- **Badge WiFi (phone)**: see *Badge WiFi*.
- **Settings**: joystick setup, shortcuts, screen lock, home screen (tiles / Meshtastic), time
  zone, name tag name / font / size / long names / tagline / stays on, screen colour, LEDs while
  the screen sleeps, night mode (red screen, dim LEDs), remote `!commands`, welcome tips, factory
  reset.
- **Key help**.

The apps (**Games, Fox hunt, Badges & achievements, Minibadges, QR code,
Stopwatch & timer, Flashlight, Messages, Nodes, Meshtastic pages**) are on the **home tiles**, not
here. `*` goes back a level, and closing an app returns to the home tiles.

### Badge page

Among Meshtastic's pages (joystick left/right) there's a *Badge* page with the everyday
things: date and time, the latest message (who, when, the start of it; "New" while unread),
the last node heard (with its signal when direct, hops otherwise), the background watch
(marked devices near, trackers, anything following you) and whether the badge can send
(region, preset, channel; a warning if the region isn't set).

### Joystick setup

The joystick is mounted at 45°, so a straight push closes two neighbouring switches. The badge
acts as soon as both have closed; if only one has, it waits up to 120 ms for the other (a quick
tap that's let go sooner counts with whatever it closed). Out of the box it assumes the usual
45° turn. If directions come out wrong, Settings → *Joystick setup* teaches it how your stick
is mounted, whatever the angle (or even mirrored):

1. Push straight up, right, down and left as the screen asks (as you hold the badge),
   letting go each time. The four circles show which switches each push closes.
2. Push around: an arrow shows what each push now means. The line under it (e.g.
   `U12 R23 D34 L14`) is what the badge learned. `0` flips the "lean" (which way a push that
   only catches one switch goes), `9` starts over.
3. Centre (or `#`) saves, `*` cancels.

When the learned pushes fit the 45° layout, each gets the whole pair of switches, so a push
recorded as one switch (a down push that only caught the bottom-left switch) doesn't claim that
switch for itself; it's also half of left.

## Messages

The badge's own messages, like a phone. It takes the place of Meshtastic's Messages page
(which mixes every channel and person, newest first): flipping to that page opens this
instead, and joystick left/right from here lands on the
pages either side of it. Also: the **Messages** home tile, a shortcut, or tapping
a number key on Meshtastic's pages.

- **Chats**: each channel and each person you've messaged, the most recently active first,
  with "N new" or how long ago the last message was. Two icons say what each is: a group (two
  people) or one person, and a closed lock when it's private (a channel with its own key; a
  person whose key the badge has) or an open lock when it isn't (a channel on the well-known
  default key or with no key; a person whose key hasn't arrived: Meshtastic won't send them a
  DM until it has, and says "DM failed"; opening that chat asks them for it). `#`/centre opens
  one; `*` or left/right leave. **+ New chat** → *Message a person* (the nodes the badge has
  heard, most recent first, with their lock) or *Join or create a channel* (name, then a new
  random key, a typed key, the default key or none).
- **A chat**: only that chat's messages, oldest at the top, newest at the bottom, each under a
  heading (who, how long ago). Joystick up/down scrolls (an arrow shows newer ones are below),
  `*` goes back to the chats. The title bar says how private it is: *private*, *private group*,
  *public group*, *NO encryption*, *no key: can't DM*.
- **Writing**: a number key or `#` opens a box under the messages: "To *name* only" or "To all
  in #*channel*", with a warning when it isn't private or can't be sent yet. Multi-tap as
  above; `#` sends and you stay in the chat, your message appearing at the bottom. Writing only
  ever goes to the chat that's open.
- **Group menu**: in a group's chat, hold `*` (the menu key, as everywhere on the badge):
  - *Invite a badge*: pick the person (closed lock = the badge has their key). The group's name
    and key go to their badge as an end-to-end encrypted DM: never readable by anyone else, and
    never sent at all if it can't be encrypted that way (their badge rejects invites that didn't
    arrive like that). Their badge shows "*name* invites you to the group #*group*"; `#` joins
    and opens the chat, `*` says no. If it's a group they're already in with a different key
    (someone changed it), it says "*name* sent a new key for #*group*: only if you trust them,
    it replaces your key"; `#` switches that group to the new key (no second group of the same
    name), `*` keeps theirs. Invites wait while someone is in a game, a scan or writing.
  - *QR code for a phone*: the channel's Meshtastic link (`https://meshtastic.org/e/?add=true#…`,
    with the badge's region and preset), which the Meshtastic phone app scans to add the
    channel. It shows the key to anyone who can see or photograph the screen: only show it to
    people you trust. Any key closes it.
  - *Change key and re-invite* (how you remove someone): groups have no member list (anyone with
    the key is in), so this lists the group's people (everyone this badge invited, and everyone
    who has posted in it), all ticked. Untick who should go (`#` ticks/unticks), then *New key,
    send to N*: the group gets a new random key, sent end to end encrypted to the ticked ones
    (people whose key the badge doesn't have yet are asked for it: invite them once it's
    arrived). Anyone left out can't read or post anything new; what they already have stays with
    them. People on phones need the new QR code.
  - *Leave this group*, then `#` to confirm. Coming back takes an invite or the key.

  The main channel (usually LongFast) can't be re-keyed or left here; the Meshtastic app does
  that. There's no group owner in Meshtastic: anyone with the key can change it on their own
  badge and send the new one to whoever they choose, which splits the group.
- **How the encryption works**: a channel is one shared key (AES); the default key is public, so
  treat LongFast as public. A DM uses the two badges' own key pair (X25519 + AES-CCM), so relays
  and others on the channel can't read it; who talked to whom, when and how much is still
  visible. Keys travel in node info automatically (at start, every few hours, and when a node is
  new or a DM can't be decoded). A full factory reset makes a new key pair, so DMs fail until the
  new node info has gone round.
- **Names instead of numbers**: a message from a node the badge has no name for shows its number;
  Meshtastic asks it once for its node info, and the badge asks again after 1½ and 5 minutes in
  case that request or the reply was lost.
- **Alerts**: the keys pulse three times in cyan for a direct message, once in green for a
  channel message, and the skull strobes then pulses until you press a key.

## Fox hunt and scanning

Badge menu → *Fox hunt*, grouped top to bottom. **Scanners**: **LoRa node**, **WiFi network**,
**Bluetooth device**, **WiFi + Bluetooth**, **Sub-GHz scanner**, **Node radar**, **Packet
capture**. **Logs / watch / lists**: **Flagged devices**, **Sightings log**, **Scan stats**, and
the **Background** watch switch (off / BT / BT + WiFi). **Settings**: **Be the fox** and **Alert
settings**. Most of them are also shortcut choices (Settings → *Shortcuts*).

| Screen | Controls |
|---|---|
| LoRa node list | joystick up/down (or `2` / `8`) scroll, left/right (or `4` / `6`) change modem preset, `7` auto-sweep presets, `0` search, `1` sort (recent / signal), `#` hunt the node, `*` done |
| LoRa node hunt | `#` ping the fox now (it's pinged every 20 s anyway, and every packet heard from it counts), `0` reset the best-reading ticks, `*` done |
| WiFi / Bluetooth / both list | joystick (or `2` / `8`) move, `0` search, `#` (or `5`) hunt the highlighted device, `*` done |
| WiFi / Bluetooth hunt | the screen shows the best signal, how long since it was last and first heard; `#` details (including first and last seen as clock times), `9` turn in a circle, `1` share this sighting (asks first), `0` reset the best-reading tick, `*` back to the list; in details the joystick scrolls and `#` goes back |
| any device (list or hunt) | **hold `*`**: its menu: nickname, trusted, alert when it turns up / when it's gone, make it beep (trackers), explore (Bluetooth) |
| Flagged devices | joystick (or `2` / `8`) pick, `0` search, `#` details, `#` again hunt it, hold `*` its menu, `*` back / done |
| Sub-GHz scanner | warning: `#` start, `*` cancel; band: joystick left / right move the marker (`4` / `6`: faster), `2` / `8` (up / down) change band, `#` meter there, `*` done; meter: `9` turn in a circle, `0` reset the best tick, `*` back to the band |
| Node radar | joystick up / down zoom, left / right pick a node, `*` done |
| Sightings log | joystick (or `2` / `8`) scroll, `0` search, `#` download over Badge WiFi, hold `*` clear (asks first), `*` done |
| An alert message | any key closes it |

In these screens, **tap** the number keys: they act when you let go, because holding a number
runs its shortcut instead (holding `1` opens the sightings log rather than sharing).

**Search** (`0`) is on every list -- the LoRa node list, the WiFi / Bluetooth lists, flagged
devices and the sightings log. Press `0`, type a few letters (multi-tap, like the message editor),
and the list shows only rows that contain the text (name, maker, address, node number or mark,
depending on the list). `#` keeps the text, `*` deletes, and `*` on an empty box clears the search.

- **LoRa node** (Meshtastic): opens a **scrollable list of every Meshtastic node the badge knows**
  -- kept separate from the WiFi / Bluetooth scans, since a busy event can bring hundreds. Each row
  shows the name and whether it's direct (with SNR), how many hops away, or heard over MQTT, plus
  how long ago it was heard (favourites marked `*`). It also lists nodes on **other channels** that
  share the badge's frequency: their packet header (sender, signal, hops) is in the clear even
  without the channel key, so they show as "other ch" with no name. (Nodes on a *different*
  frequency -- a different primary channel name or modem preset -- can't be heard without retuning,
  so they don't appear; nor do nodes the badge has never heard, unless seen over MQTT.) `0` searches
  by name or node number, `1` sorts by most-recently-heard or by signal (closest first). Pick one
  with `#` to hunt it -- the screen and a bar graph on the keys show its signal as you walk ("Warmer"
  / "Colder"), with RSSI and SNR meters; the tick on each is the best reading so far. The fox hunt
  works on any node on the badge's frequency, even one on another channel.
  The title shows the **modem preset** (LongFast, LongSlow, ...) the radio is tuned to. A single
  LoRa radio can only hear one preset at a time, so to see other presets there's a **built-in
  switcher**: left / right (or `4` / `6`) step through the presets live (no reboot), and `7`
  auto-sweeps them (about 5 s each). The list **keeps the nodes it has gathered** as you move, and
  tags each row with the preset it was heard on (`LF`, `LS`, `LM`, `MS`, `MF`, `SS`, `SF`, `ST`), so
  sweeping builds one combined picture across presets. Picking a node to hunt switches to the preset
  it was heard on; leaving the list (or the hunt) puts the radio back on the badge's own preset -- it
  doesn't change your saved settings. (The named channel still isn't broadcast -- only a one-byte
  hash of name + key rides in the clear -- so an other-channel node shows by that hash, `other ch
  #A3`, not its name.)
- **WiFi network** / **Bluetooth device**: lists what's around, strongest first (joystick to
  move, `#` to choose). The hunt screen shows an RSSI meter, the best reading and the key bar
  graph; `*` goes back to the list. Devices show their maker: for Bluetooth from the company ID
  in the advert (Bluetooth SIG list), otherwise from the first half of the address (IEEE list);
  the maker scrolls when it doesn't fit, and the address is shown. Phones and other devices
  with random addresses (`rnd`) only show a maker if their advert carries a registered company
  ID; they change address every few minutes and come back as new entries. The lists live in
  `tools/data`; refresh them with `tools/make_vendors.py --update`. WiFi is switched on only
  while the hunt is open; Bluetooth needs Meshtastic's Bluetooth on, so it isn't available while
  Meshtastic's WiFi is on. Neither works while the Paxcounter module is on.
- **WiFi + Bluetooth (both)**: one list with everything, marks and alerts the same as the
  separate lists; each address line ends in `WiFi` or `BT`. The badge has one 2.4 GHz radio, so
  it takes turns (a 3 s Bluetooth scan, then a sweep of the WiFi channels), and entries
  refresh a little slower than in the separate lists. Picking one hunts it with its own radio
  only, for quick readings; `*` from the hunt goes back to the combined list. If one half can't
  run (Bluetooth off, or not enough memory for WiFi) the list shows the other half only.
- **Marks**: devices that meet a rule in `tools/data/rules` show what they likely are, e.g.
  `SURV Flock plate reader`, `POL Police body cam / in-car video`, `CAM Home camera / doorbell`
  or `TAG Find My tracker`. Surveillance, police, camera and tracker devices go to the top with a
  bar down the left, the title counts them (`!2`), and the keys flash red / blue while any are
  near (white for trackers only). Other rules (vehicle routers, smart-home devices, ...) are shown but
  not highlighted. The rules are written against the official IEEE / Bluetooth SIG lists; the
  format and how to add your own are in `tools/data/rules/README.md`. These are hints, not
  proof: a hidden camera with an ordinary WiFi chip and a renamed network won't be marked, and
  a maker's other products (or a network someone named "Camera") will be.
- **What gets marked** (rule files in `tools/data/rules`, plus a few patterns built in):

  | Mark | What it means | How it's recognised | On the badge |
  |---|---|---|---|
  | `SURV` | surveillance: Flock and Neology licence-plate readers, red-light / speed / toll / plate cameras (Jenoptik, Redflex, Kapsch, Leonardo / ELSAG), ShotSpotter gunshot sensors, FLIR thermal cameras, security camera / video system makers | maker (address prefix or Bluetooth company), device or network names | top of the list, keys flash, alert |
  | `POL` | police gear: body cams and in-car video (Axon, WatchGuard, Utility and others), police radios (Motorola, which also makes Vigilant plate readers), radar, forensics kit, Skydio drones (police "drone as first responder" programmes) | maker, names | top of the list, keys flash, alert |
  | `CAM` | cameras: home cameras and doorbells; hidden / cheap WiFi cameras; camera glasses (Ray-Ban Meta and other Meta Reality Labs devices, which includes Quest headsets) | maker; Bluetooth company (Meta Platforms Technologies, Luxottica); for hidden cameras the network or device name patterns cheap cameras use (e.g. `ipcam`, model-number names) | top of the list, keys flash, alert |
  | `TAG` | trackers: Apple Find My (AirTag and compatible), Google's Find My Device network (Android: Pebblebee, Moto Tag, Chipolo POINT, Eufy and others), Tile, Samsung SmartTag, Chipolo | Bluetooth company ID and advert type; for Find My and the Android network also whether the tracker says it's **away from its owner** | a tracker away from its owner: top of the list, keys flash white, alert once it's been with you 10 minutes, follows you, or turns up again. One whose owner is nearby (your own AirTag, a friend's) is only labelled "owner nearby" |
  | `SKIM` | a Bluetooth serial module with its factory name (`HC-08`, `JDY-…`, `BT05`, `AT-09`, `HMSoft`, …), the kind card skimmers at gas pumps and ATMs often hide inside | its Bluetooth name | top of the list, keys flash, alert. Hobby electronics use the same modules; one at a pump or ATM is worth a look. The older HC-05 / HC-06 are Classic Bluetooth, which this chip can't hear |
  | `DRON` | a drone broadcasting Remote ID; or drone gear (DJI, Parrot) and drones flying over their own WiFi network (Tello, Spark, Mavic, Anafi, Bebop, toy "WIFI_UFO" drones, names with "drone") | the Remote ID advert over Bluetooth, in WiFi beacons or WiFi NAN frames; maker, network names | top of the list, keys flash, alert; Remote ID details show drone and pilot position |
  | `ATK` | an attack tool or attack: a WiFi Pineapple's default network name, one radio sending many network names, WiFi deauth floods, Bluetooth pop-up spam | names and behaviour | top of the list / alert |
  | `TWIN` | possible evil twin: the same network name both open and protected | behaviour | top of the list |
  | `HACK` | Flipper Zero | its Bluetooth name | shown, not highlighted |
  | `NET` | vehicle routers (common in police cars, also buses and fleets) | maker | shown, not highlighted |
  | `DEF` | defense contractor equipment | maker | shown, not highlighted |
  | `HOME` | smart-home devices: Amazon (Echo, Fire TV), Google / Nest, Philips Hue, Sonos, ecobee, Shelly, Meross, Govee, LIFX, Aqara, Belkin / Wemo, Lutron, iRobot, SmartThings, Roku, Chamberlain / myQ, August / Yale and other ASSA ABLOY and Allegion locks; and Tuya, the module many cheap no-name gadgets and cameras are built on (its own title) | maker; Google only by address range (its Bluetooth ID is on Android phones too); TP-Link and Xiaomi aren't marked because most of their address ranges are routers and phones (they still show the maker) | shown, not highlighted |
  | `FOLW` | any marked device that has been with you 10 minutes (or moved with you) | the following-you check below | very top, keys flash fast red, alert |

  Everything marked is a likely match, not proof (see *Marks* above). A hidden camera that uses an
  ordinary WiFi chip and a normal network name won't be marked, and a camera maker's other
  products will. Phones and other devices with random Bluetooth addresses can only be marked by
  their advert (company ID, name, services), not by maker.
- **Following you**: a marked device that stays in range 10 minutes (gaps under 5) shows as
  `FOLW` at the very top, with its minutes, and the keys flash fast red. Fixed devices around
  when it showed up (registered-address speakers, TVs, access points) act as landmarks: when
  most of them are gone and the tracker is still there, you moved and it came along ("Followed
  you"). Trackers on someone sitting next to you count too, so move and check again.
- **Details**: on a hunt screen `#` shows a 2-minute signal graph and everything the badge
  knows (maker, company ID and data type, address, rule, minutes with you, landmarks still
  around, service IDs, earlier sightings); joystick scrolls, `#` goes back. Walking around with
  the graph shows whether the signal moves with you (steady and strong) or stays somewhere.
- **Alerts**: a message pops up when a surveillance, police or camera device first shows up,
  when a tracker has been with you 10 minutes, when it followed you, and when a tracker turns up
  again 30+ minutes after it was last seen ("seen before ... 5 h ago"); at most one every 15 s.
  Fox hunt → *Alerts* turns them off.
- **Background watch**: Fox hunt → *Background watch* keeps scanning Bluetooth (*BT*) or
  Bluetooth plus a 4-second WiFi sweep every 2 minutes (*BT + WiFi*) while the hunt is closed,
  with the same alerts as banners. Off by default; it listens about 10% of the time, but expect
  more battery drain with it on. The Badge page summarises it.
- **Attacks** (WiFi needs the badge to own WiFi, i.e. Meshtastic's WiFi off): WiFi deauth floods
  (30+ kick-off frames in 10 s), Bluetooth pop-up spam (20+ addresses sending fake pairing
  adverts in 10 s, e.g. Flipper Zero spam), evil twins (`TWIN`: the same network name both open
  and protected), and one radio sending several network names (`ATK`, how a WiFi Pineapple lures
  phones). A network named like a Pineapple's default is marked `ATK` too, and Flipper Zeros by
  their Bluetooth name (`HACK`). Hak5 has no address block of its own, so Pineapples can't be
  spotted by maker.
- **Drones** (`DRON`): Remote ID broadcasts over Bluetooth (legacy and Bluetooth 5 message
  packs), inside WiFi beacons (the ASTM format many DJI drones use) and as WiFi NAN frames
  (channel 6, used by many add-on Remote ID modules; the list names the drone by its ID). Details
  show the ID, operator ID, the drone's position, height and speed and where the pilot is.
  Without Remote ID, a drone is still marked by its maker (DJI, Parrot: *Drone maker*; DJI's
  ranges also cover its controllers, gimbals and Osmo cameras) or by its own WiFi network
  (*Drone's own WiFi network*). Not seen: drones under 250 g with nothing switched on, DJI's own
  DroneID (on DJI's radio link, not WiFi), and 5.8 GHz video / control links (the badge's radio
  is 2.4 GHz only). WiFi NAN and beacon Remote ID need the badge's own WiFi (not while
  Meshtastic's WiFi is connected). Untested with a real drone so far.
- **Turn in a circle**: on a hunt screen (WiFi, Bluetooth, sub-GHz meter) `9`: hold the badge in
  front of you and turn slowly right once in 20 s; the plot shows which way the signal was
  strongest (your body blocks it from behind). LoRa node readings come too slowly for this; a
  directional 915 MHz antenna on the SMA connector works better there.
- **Node radar (map)**: the badge in the middle, the nodes that share their position around it
  (north up) with range rings. Joystick up/down zooms (100 m to 250 km), left/right picks a node
  (name, distance, direction, when heard, hops). Nodes past the edge show as hollow dots on the
  ring. The badge has no GPS, so it needs its own position from the Meshtastic app (share the
  phone's location, or set a fixed position). Badge WiFi has a bigger map.
- **Sub-GHz scanner** (pauses Meshtastic): warns first, then borrows the LoRa radio. The band view
  sweeps one band at a time -- `2` / `8` switch between **315 MHz** (US car fobs / TPMS),
  **433 MHz** (remotes, sensors, weather stations, EU TPMS), **868 MHz** (EU IoT / alarms / meters /
  LoRaWAN EU) and **902-928 MHz** (LoRa / LoRaWAN, Z-Wave, meters, 33 cm ham). Each frequency shows
  what it's most likely associated with (like the scan marks), and `#` on a channel gives a level
  meter with Warmer / Colder and the turn helper. **Receive only**: the badge's antenna is tuned for
  ~900 MHz, so the lower bands hear only near or strong signals, and nothing is transmitted off the
  900 MHz band. Meshtastic drops what it would send and hears nothing until you close it; then its
  radio settings are restored. 2 m and 70 cm ham foxes are out of the radio's reach.
- **Sightings log**: a diary of the marked devices the scans came across, so you can look back
  at what was around, notice a tracker that keeps turning up, or keep a record. See *The
  sightings log* below.
- **Scan stats**: a glanceable dashboard of what's on the air right now, no laptop needed. `#` (or
  left / right) swaps between **WiFi** and **Bluetooth** (one radio, one at a time). WiFi shows
  network count, open / hidden, busiest channel, evil twins, drones, deauths in the last 10 s,
  how many are flagged, and the busiest makers. Bluetooth shows device count, public vs random
  addresses, named devices, trackers (and how many say they're away from their owner), Find My /
  FMDN count, pairing-spam bursts, and the busiest makers. Numbers are "around you now" (the last
  ~30 s), not an all-time total.
- **Sharing** only happens when you ask, one device at a time: on a hunt or details screen tap
  `1`; it asks "Share this sighting with other badges?" and `#` sends that device (mark, title,
  address, signal; at most one every 10 s; logged as SHARED). It goes on the badges' private app
  port, so other badges show it as an alert and in their log; plain Meshtastic radios and the
  phone app don't see it. There's no "share the whole list".
- **Be the fox**: a short packet every 15 s that only badges in direct range hear (not relayed),
  so others can hunt this badge with *LoRa node*. Off after a reboot.
- **Packet capture** (off by default and after a reboot): opens a screen with three independent
  captures to pcap files for Wireshark (download them from Badge WiFi). `2` / `8` pick, `#` turns
  one on / off, hold `*` clears the files.
  - **Mesh (LoRa)** -> `capture.pcap`: the Meshtastic packets the badge receives. Passive, so it
    keeps running in the background once on. LoRaTap framing + Meshtastic's header, read by the
    [Exploiteers Meshtastic plugin](https://github.com/exploiteers/Exploiteers-Wireshark-LoRa)
    (the badge stores packets decrypted, so set the plugin's key to empty). It holds message text,
    including direct messages to you -- treat it as private.
  - **WiFi (802.11)** -> `wifi.pcap`: management frames in full (beacons, probe requests, deauth,
    ...) plus the headers of data frames (payloads of encrypted WiFi stay encrypted). Raw 802.11,
    opens straight in Wireshark. Runs while the capture screen is open.
  - **Bluetooth** -> `ble.pcap`: BLE advertisements (BLE link-layer framing). Runs while the screen
    is open. The badge can't follow BLE *connections* (that needs dedicated sniffer hardware), and
    has no classic-Bluetooth radio.

  The badge has one 2.4 GHz radio, so WiFi and Bluetooth capture can't run at the same time
  (turning one on stops the other); the mesh capture is on the separate LoRa radio and runs
  alongside either. You can't run Wireshark on the badge itself -- download the `.pcap` and open it
  on a computer. Capture passively and only on networks you're allowed to; encrypted payloads you
  don't hold the key to stay unreadable.

  **Files never overwrite.** Each one stops at a size cap (mesh 1 MB, WiFi 512 KB, BLE 256 KB) and
  sets a "full" flag rather than wrapping; download and clear it to carry on. Mesh fills slowly
  (~10,000 packets ≈ 1 MB, hours), but in a busy place **WiFi and BLE fill in seconds to minutes**
  because beacons and adverts are constant -- so for anything sustained, use the live stream.

  **Live stream to Wireshark** (no file, nothing written to flash): open Badge WiFi, join it, and
  on the *Live* tab set the stream source to **Mesh** or **Bluetooth**. On your computer run
  `nc <badge-IP> 19000 | wireshark -k -i -` (or `... | tshark -i -`) and frames appear as they're
  heard, with no size limit. Mesh streams cleanly (separate radio); Bluetooth shares the 2.4 GHz
  radio with the badge's WiFi. WiFi 802.11 can't stream while the badge's own WiFi is up (one
  radio) -- capture WiFi to a file instead.

### Your devices: trusted, nicknames, watchlist

Hold `*` on a device (in a scan list, on its hunt screen, or in Flagged devices) for its menu:

- **Nickname**: up to 16 characters ("My car", "Sam's AirTag"); shown instead of its own name in
  the lists, the hunt screen, alerts and Flagged devices.
- **Trusted (no alerts)**: yours or known. It stays in every scan list (with its mark, and a `~`
  before its nickname) but never alerts, never flashes the keys and isn't moved to the top.
- **Alert when it turns up**: tells you when it appears again after being away 5+ minutes
  ("Watched device is here").
- **Alert when it's gone**: tells you when it hasn't been heard for 3 minutes ("Watched device is
  gone"), e.g. a tag on your bag.
- **Make it beep** (trackers): the badge connects to it and asks it to play its sound, the way
  anti-stalking apps do, to find one hidden in a bag or car. AirTags get Apple's request; other
  brands the cross-industry anti-stalking (DULT) one. Trackers only answer someone who isn't
  their owner once they're away from their owner, and not every model supports it. The screen
  says whether it took the request.
- **Explore** (Bluetooth, read only): the badge connects to the device and lists what it offers --
  its services, and the everyday values anyone may read: name, maker, model, firmware, battery,
  what it looks like. It also **probes each readable characteristic's security**: every one shows
  `open - NO pairing`, `needs encryption`, `needs auth pairing (MITM)`, `needs authorization` or
  `needs bigger key`, with a tally (`Readable: X open, Y protected`). That's a quick "is this device
  exposing data without encryption?" check -- access-control testing from a client, not over-the-air
  link decryption (which needs a dedicated sniffer). It never writes anything, and never pairs.
  Handy for working out what an unknown device is, and whether it protects its data.

These are kept in `/badge/devices.bin` (up to 64 devices) and remembered by address, so a device
that changes its address (phones, and trackers while their owner is near) counts as a new one.
The watchlist works while a WiFi / Bluetooth hunt is open or the background watch is on, and only
from the scan of the device's own kind.

### Alert settings

Fox hunt → *Alert settings*:

| Setting | Choices |
|---|---|
| Alerts | on / off (all alert messages) |
| With you after | 5, **10** or 20 minutes in range before something counts as with you |
| Signal | alert at any strength (default), or only for devices stronger than -90 / -80 / -70 dBm |
| Trackers, Cameras, Surveillance, Police gear, Skimmers, Drones, Attacks | each alert / don't alert |
| Quiet hours | off, 22:00-07:00, 23:00-07:00, 21:00-06:00: no pop-ups then (still logged; needs the time) |
| Lists show faint devices | **yes** / no: hide devices weaker than -85 dBm from the scan lists |

Settings only change alerts: everything still shows in the lists (unless faint ones are hidden)
and is still logged.

### Flagged devices

Fox hunt → *Flagged devices* (also a shortcut choice): one row per flagged device, combining what
the watch has near now with the sightings log and your saved devices. Anything following you
comes first (`FOLW`), then what's near now, then the rest, newest first; each row shows the mark,
the nickname or name, and "near" or how long ago. `#` shows the details (joystick scrolls; what it likely is, name,
address, near now or not and how many times, **first and last seen** (clock time when the badge
knows it, and how long ago), strongest signal, whether it moved with you, trusted / watched); `#` again hunts it (a device only known from the log is hunted over Bluetooth
unless it's a WiFi-only mark). Hold `*` for the device menu.

### Your own rules (no rebuild)

Badge WiFi → **Rules**: add a rule that marks devices in the scans, used straight away. Match on
a Bluetooth device name, a WiFi network name, the maker's name (anywhere, ignoring capitals), or
the start of an address; give it a description and a mark (`SURV`, `POL`, `CAM`, `TAG`, `SKIM`,
`ATK`, `DRON` alert; `HOME` and `NOTE` only label). Your rules come before the built-in ones.
They're kept in `/badge/rules.txt` (up to 32); delete them on the same page.

### The sightings log

Only the **highlighted** marks are logged and watched for following you (`SURV`, `POL`, `CAM`,
`TAG` away from its owner, `SKIM`, `DRON`, `ATK`, `TWIN`, and attacks), never ordinary phones, speakers or networks, and not
the shown-only marks (`HACK`, `NET`, `DEF`, `HOME`). Logging happens while a WiFi / Bluetooth
hunt is open or the background watch is on. It stays on the badge and is never sent anywhere unless you share a
sighting or download the file.

**Events** (one line each):

| Event | When |
|---|---|
| `SEEN` | a marked device showed up |
| `AGAIN` | it showed up and the badge has logged it before (with how many times and how long ago) |
| `FOLW` | it has been with you 10 minutes |
| `MOVED` | it was with you and you moved: it followed you |
| `GONE` | it left, with how many minutes it was around |
| `ATTACK` | a WiFi deauth flood or Bluetooth pop-up spam |
| `SHARED` | you shared this sighting with other badges |
| `MESH` | another badge shared a sighting with you |

**On the badge** (Fox hunt → *Sightings log*, or shortcut 1): newest first, two lines per entry.
The first has when (the time if the badge knows it, else boot number and uptime), the event, the
mark and what it likely is; the second has the address, the signal (dBm) and the device's name or
network name, with `@` if the badge's position was recorded. Joystick (or `2` / `8`) scrolls,
`#` opens Badge WiFi to download it, hold `*` clears it (asks first), `*` closes.

**The file** (`/badge/sightings.csv`, download from Badge WiFi → *Sightings*): one line per event
with the time (seconds since 1970, 0 if the badge didn't know it), the boot number, seconds since
that boot, the event, the mark, the address, the signal, the badge's latitude and longitude
(1e-7 degrees; only if your phone shares its location with Meshtastic, else 0), the rule title
and extra details (the name, minutes for `GONE`). At 512 KB it moves to `sightings.old.csv` (also
downloadable) and a new one starts. `/badge/seen.bin` remembers each logged address with how
many times and when it was last seen, for `AGAIN` and the "seen before" alert.

## Mesh settings (no phone needed)

Badge menu → *Mesh settings*. Every change is saved the way Meshtastic's own admin code saves
it, so the phone app (if you use one) shows the same settings. Menus reopen on the item you
last used.

| Item | What it does |
|---|---|
| Node name / Short name | the names other people see on the mesh (the name tag has its own) |
| Channels | your channels; pick one to see its key, rename, re-key or remove it. *+ Join / new channel*: type a name, then make a random key, type a key someone gave you, use the public default key, or no key |
| Send messages to | the channel quick messages go to (chats always send to their own chat) |
| Transmit power | max allowed (default), 20, 17, 14, 10 or 7 dBm; lower saves the AA cells. The badge restarts to apply it |
| Hop limit | 1–7 (3 is usual): how many times a message is passed on |
| Quick messages | the canned messages: edit, add, move up, delete. Settings → *Shortcuts* puts them on a number key |
| Region, preset, role | Meshtastic's own pickers |
| Bluetooth on/off | Meshtastic's own toggle |

The main (primary) channel is view-only here: renaming it moves the badge to a different
frequency slot and off the shared mesh. New channels are added as secondary channels. Keys are
base64, like the app shows them: 24 characters for a random badge key, `AQ==` for the default
key. Expert settings (custom LoRa bandwidth/spreading factor, MQTT, security keys, router roles)
stay in the app.

## Shortcuts

Hold a number key (`1`–`9`, `0`) to jump straight to something. Settings → *Shortcuts (hold a
number)* picks what each number does: a screen (sightings log, Bluetooth or WiFi scan, LoRa fox
hunt, node radar, sub-GHz scanner, Messages, new message, name tag, Badge WiFi, lights, games,
CHIP-8, Skinwalker Mine, badges, mesh settings, settings, badge menu, flagged devices), a switch (background watch
mode, scan alerts, packet capture, be the fox, next LED scene, night mode, flashlight, lock now)
or a quick message (canned message 1–9, sent to the *Send messages to* channel after asking).

Out of the box: 1 sightings log, 2 Bluetooth scan, 3 WiFi scan, 4 node radar, 5 Messages,
6 Badge WiFi, 7 lights, 8 lock now, 9 night mode, 0 flashlight.

They work on Meshtastic's pages and in most badge screens (name tag, Messages, scans, radar,
sightings log, lights, Badge WiFi, flashlight...): there a quick tap of a number still does what
that screen uses it for (when you let go), and holding it runs the shortcut. They don't work
where numbers are typed or played: writing a message or a name, the PIN screens, joystick setup,
the games and CHIP-8.

## Screen lock

Settings → *Screen lock (PIN)* sets a 4–8 digit PIN (typed twice). From then on the badge locks
at **every start** (reset button, power loss, a crash), when the screen goes to sleep, and after
5 minutes without a key press. `*` and `#` together lock at once, from anywhere, even inside a
game or scan. The Screen lock menu also has *Lock now*, *Shows*, *Change PIN* and *Turn screen
lock off*.

- **What it shows** (*Shows*): your name (with the envelope when a message arrived, never what
  it says), *Clock and date* (the time big in your name font, the date, your name), one picture
  or GIF that stays put, or *Cycle: name + pictures* / *Cycle: pictures only* / *Cycle:
  favorites* (a new one every 8 seconds). Favorites are the pictures you marked with `0` in the
  name tag (marked with `*` in this list); with none marked it cycles all the pictures. With *Name tag stays on* (the default) it keeps the screen on.
- **Unlocking**: press any key and a PIN line appears at the bottom (that key doesn't count as a
  digit); then type the PIN. After 5 wrong PINs it makes you wait (30 s, doubling up to 8
  minutes, kept across restarts). 
- **Hard to get around**: after a start the lock takes over before Meshtastic's first page is
  ever drawn; until the PIN screen is up the keys do nothing; no other badge screen can open
  while locked; anything that draws over it (e.g. Bluetooth pairing) is covered again within a
  second or two; Meshtastic's message pop-ups are hidden. A phone already paired with the
  Meshtastic app can still connect.
- Only a salted SHA-256 of the PIN is stored. Forgot it: a full erase and reinstall clears it,
  along with everything else.

## Name tag

Badge menu → *Name tag & pictures*: your name in big letters (until you type one it shows your
Meshtastic name), with an optional tagline under it; then the pictures. Joystick flips pages,
`#` starts a slideshow, hold `#` edits the name. On the name page `1` changes the font and `2`
the size (Auto = as big as fits, Big, Medium, Small). On a picture, tap `0` to make it a
favorite (or not); the lock screen's *Cycle: favorites* shows only those. The Badge WiFi
page's *Pictures* tab has the same toggle (the star next to each picture). The top corners show how many nodes are
near (heard recently) and an envelope while a message is unread. Your name only appears on the
lock screen's cycle once you've actually set one — the untouched Meshtastic default stays hidden.

- **Stays on**: Settings → *Name tag stays on* (default yes, also on the Badge WiFi page): the
  name tag and the lock screen keep the screen on, so whatever you picked stays visible while you
  wear the badge. Set it to no to let them sleep and save the batteries; other screens always
  sleep.
- **Long names**: Settings → *Long names*: *two lines* (default) splits a name that's too wide;
  *scroll* keeps it on one line at the chosen size and scrolls it across (name tag and lock
  screen).
- **Fonts**: Settings → *Name font* shows your name in each font: joystick to flip through, `#`
  to use it. Classic, Wood type, Serif, Typewriter and Fancy, and Old West / dark ones from
  Google Fonts in `tools/fonts/` (licences next to them): Wanted (Ultra, a wanted-poster slab),
  Frontier (Holtwood One SC, wood-type capitals), Old print (IM Fell English SC, weathered 1700s
  printing), Gazette (Old Standard, 1800s newspaper), Tombstone (Cinzel, carved capitals), Gothic
  (Grenze Gotisch), Grimoire (Pirata One, blackletter), Undertaker (Playfair Display SC Black,
  Victorian), Fraktur (UnifrakturMaguntia), Scripture (Texturina, an old book hand), Saloon (Vast
  Shadow, shadowed wood type) and Weathered (Fredericka the Great, a scratched sign).
  `tools/make_fonts.py` turns them into `BadgeFonts.h` (Meshtastic's own font format, three
  sizes each); add new families at the end of its list, since the badge remembers the font by
  its number.
- **At startup**: Meshtastic's boot logo, then the still raven (both are Meshtastic boot screens,
  so the logo comes first), then the badge rests on the lock screen, cycling its content until a key
  is pressed. With a PIN set that first key brings up the lock's PIN line; with no PIN any key goes
  to the tile home. There's no separate animated-raven intro by default — the raven is just one of
  the cycling pictures (no longer the first one). The Badge WiFi page's *Startup* setting can still
  make a chosen picture play first. The home tiles never flash up during this sequence.

### Pictures

Put `.png .jpg .gif .bmp .webp` files in `images/` and build. `tools/make_images.py` runs before
every build, scales each picture to fit the 160×128 screen and dithers it to one colour
(Meshtastic's screen driver draws a single colour on black; pick the colour in Settings).
Animated GIFs play at their own frame timing (up to 96 frames shown). Files are shown in name
order, so number them (`01_…`, `02_…`). Pictures with light backgrounds usually look better
inverted: put `_inv` in the name. Frames are stored packed (`BadgeImageCodec.h`): black areas
and the parts of an animation that don't change take almost no space; the script stops at 2 MB
packed. `tools/image_test.cpp` checks every frame unpacks exactly. More pictures and GIFs can be
added without rebuilding from Badge WiFi's *Pictures* page (up to 40, kept in the badge's file
space).

The Old West pictures are drawn by `tools/make_west_images.py` (`python3 make_west_images.py
../images ../../../../userPrefs.jsonc`; delete any you don't want):

- `22_mesh_relay`: a timber lattice LoRa mast on the ridge, a blinking beacon throwing signal rings
- `23_windmill`: a ranch windmill turning on the mesa, a relay box on its tower beaming rings out
- `24_telegraph`: the talking wire -- telegraph poles to the horizon, a packet of light running the line
- `99_utahtastic`: the raven from the badge PCB (its silhouette is `tools/raven_pcb.png`, cut out
  of the PCB artwork) on the UTAH BSIDES sign, cawing and blinking. The same raven, still, is
  Meshtastic's boot screen: the script writes it into `USERPREFS_OEM_IMAGE_*` in `userPrefs.jsonc`
  when that path is given. It's last in the series, so it isn't the first thing the lock cycles.
- `03_jackalope`: a jackrabbit with a buck's antlers under the moon; it twitches, then bounds off-screen
- `04_wanted`, `05_skinwalker`, `06_mesh_marshal`, `07_ghost_town`, `08_dont_answer`
- `09_campfire`: eyes at the edge of the firelight
- `10_howl`: a coyote on the ridge rears up and stands like a man, its head tips over too far;
  then it's much closer; then only its eyes, in the dark, right in front of you
- `11_tracks`: boot prints turn into paw prints
- `12_mine`: the mine entrance; the lantern gutters and something looks out
- `13_lightning`: each flash shows the figure closer, then only its eyes
- `14_skull`: a cow skull on a fence post; something looks out of its eyes
- `15_mothman`: red eyes on the bridge; it spreads its wings and lifts off
- `16_wendigo`: a gaunt, antlered thing among the pines, backlit by the moon in drifting fog
- `17_thunderbird`: a great pale eagle flies the length of the sky, wings beating, and passes off
- `18_chupacabra`: a spiny-backed hound creeps out of the dark, across the moon, and away
- `19_dogman`: a wolf that stands like a man, throwing its head back to howl at the full moon
- `20_phantom_rider`: a pale horse and a hatted rider gallop across the flat and off, trailing dust
- `26_rattlesnake`: a rattler coiled on the sand, head swaying, tongue flicking, its rattle a blur
- `27_lora_brand`: a branding iron glowing in the dark -- an antenna throwing waves, hot and sparking
- `28_tumbleweed`: a tumbleweed bounds across the flat and off into the dark

## Games

Games tile → *Games*. **Every game is played on the keypad** (the joystick's 45° mount makes fine,
fast moves unreliable), and each game page prints its keys. The number pad is a D-pad: **2** up,
**8** down, **4** left, **6** right, **5** or **#** act/select, **`*`** quits.

- **Skinwalker Mine**: below.
- **CHIP-8 arcade**: below.
- **Chess**: below.
- **Solitaire** (Klondike): `4` / `6` move left/right; `2` / `8` move up/down — between the top row
  (stock, waste, the four foundations) and the seven columns, and up/down through a column's face-up
  cards to choose how deep into the run to grab; `5` / `#` draws from the stock, picks a card or run
  up, or drops it; `*` cancels a pickup; hold `*` quits.
  Each card shows its rank and suit letter (e.g. `7H`, `10D`, `AS`, `KC`); **red cards are drawn
  as white tiles with black text, black cards as black tiles with white text**, so colour reads at
  a glance. A column too long for the screen squeezes its spacing so the whole run stays reachable.
  Press `0` for an on-screen help card with the controls and the rank/suit legend.
- **Mahjong** (solitaire): a 104-tile, three-layer board (16 tile faces). Match two *free* tiles of
  the same face to clear them. A tile is free when nothing sits on top of it and one side is open.
  `4` / `6` / `2` / `8` move to the nearest free tile left / right / up / down, `5` / `#` select then
  match, `*` deselects, `0` shows help, hold `*` quits. Every deal is built to be solvable; `#`
  reshuffles if you run out of moves.
- **Simon** and **Whack-a-mole**: press the lit number keys.
- **Snake**: `2` / `8` / `4` / `6` steer, `5` or `#` start / pause.
- **Blocks** (falling blocks): `4` / `6` move, `5` rotate, `8` drop to the bottom, `#` starts, `*` quits.
- **Tic-tac-toe** against the badge or another badge over the mesh — keys **1–9** are the board
  (X red, O blue).

### Skinwalker Mine

A Doom-style shooter: a raycaster draws the old silver mine in 3D at about 15 frames a second.
Five chapters, each with a story card: *The Old Shaft*, *Flooded Drifts*, *The Ore Chamber*,
*Collapsed Tunnels* and *The Deep Vein*, where the Old One (antlers, takes about eleven shots)
waits. Skinwalkers take two or three shots; coyotes that aren't coyotes are fast but go down in
one. Clear a chapter to go deeper: your health (+30) and bullets (+12) come with you. Dying
restarts the chapter, and the furthest chapter is remembered (*Continue* on the title screen).
You have a six-shooter (reloads itself when empty, or press `0`), and there are bullets and
medicine lying around.

| Control | Does |
|---|---|
| `2` / `8` | walk forward / back |
| `1` / `3` | turn left / right |
| `4` / `6` | step sideways (strafe) |
| `5` or `#` | shoot (and start / play again) |
| `0` | reload |
| hold `*` | quit |

The keys show your health (green to red), flash when you shoot and go red when you're hit; the
skull's heartbeat speeds up as a skinwalker closes in. The engine is `BadgeMine.h`;
`tools/mine_preview.cpp` renders frames on a PC and checks the level can be finished.

### CHIP-8 arcade

A CHIP-8 interpreter (`BadgeChip8.h`; it passes the Timendus test suite's opcode and flags
tests, and `tools/chip8_test.cpp` runs every game) with 12 public-domain games from John
Earnest's CHIP-8 Archive (CC0; `chip8/`): Outlaw, Ghost Escape, Glitch Ghost, Cave Explorer,
Dino Run, Br8kout, Super Pong, Space Racer, Flight Runner, Slippery Slope, Snek, Mini Lights
Out. The 64x32 screen is drawn twice the size. Joystick = CHIP-8 keys 5 / 7 / 8 / 9 (Octo's W A
S D), centre or `#` = 6 (Octo's E), digits = the same keys, `*` = A, hold `*` quits; the keys
flash for the beep. Add games by putting a `.ch8` in `chip8/` and an entry in
`chip8/games.json`.

### Chess

Your pieces are always at the bottom: play White, Black (the board turns round) or two players
passing the badge (the board turns to face whoever's move it is), against the badge at Easy,
Normal or Hard. Move the cursor with `2`/`8`/`4`/`6`, `#` or `5` picks up a piece (dots show where it
can go) and puts it down; `*` puts it back, hold `*` quits. Castling, en passant and promotion
(to a queen) are all there; checkmate, stalemate and the 50-move rule end the game. The engine
(`BadgeChess.h`) is checked against the standard perft move counts in `tools/chess_test.cpp`.

## Lights

Each scene is a colour palette plus its own effect for each part of the badge, so the logo,
eyes, moon and keypad all move differently while the colours belong together. Changing scene
crossfades over about half a second. Hold `#` for the next scene, or pick one in Lights with a
live preview.

| Part | Effects |
|---|---|
| Keypad | wave, rain, sparkle, fire, snake, ripple, scanner, breathing rows, plasma, checkerboard, spiral, equaliser, stalking eyes, dust devil, lightning storm, campfire embers, saloon marquee, tumbleweed, rattlesnake |
| Logo (the B sign) | colour flow, marquee chase, flickering neon, breathe, fireflies, heartbeat, colour steps, swinging lantern, sheriff's star glint, saloon bulbs, storm |
| Eyes | glow and blink, look around, candle flicker, demon flare, wink, colour cycle, ping-pong, eyes in the dark, coyote eyeshine |
| Moon | phases, glow, circling halo, eclipse, star twinkle, aurora shimmer, clouds drifting past, howling moon, storm, dust haze |

48 scenes: Skinwalker, Shapeshifter, Coyote Moon,
Campfire, Saloon, Sheriff's Star, Wanted Poster, Dust Devil, Rattlesnake, Bone Orchard, Dead
Man's Hand, Thunderbird, Ghost Town, Silver Mine, High Noon. Lightning (Thunderbird) strikes
every couple of minutes, worked out from the scene time, so the keypad, sign and moon flash
together, and so do badges in a light show. Then the earlier colours under new names: Painted
Desert (rainbow), Cavalry Blue, Blood on the Saddle, Desert Rose, Prickly Pear, Turquoise Trail
(the 2025 colours), Red Rocks Sunset, Desert Night, Gold Rush, Midnight Rodeo, Boomtown, Flash
Flood, Frozen Pass, Prairie Fire, Branding Iron, Snake Oil, Spirit Lights, Juniper Fireflies,
General Store, Mirage, Moonshine, Witching Hour, Harvest Lantern, Blood Moon, Ghost Riders,
Hexed, Boot Hill, Possessed, Cornfield, Watering Hole (calm keys that ripple as you type), Your
Brand and Your Brand Glitter (the colour set in the phone app), and Lights Out. The old names
still work in `!lights` and in saved settings.

- **Key presses** turn the key a contrasting colour for a moment so you can see it counted
  (Lights → *Key flash*).
- **Typing**: while a text box is open, the keys light up warm white at least at "High"
  brightness so the printing is readable, and a key you press turns green.
- **The skull** left of `*` glows with the scene's eye effect; when a message arrives it strobes
  (cyan for a direct message, green for a channel), then pulses until you press a key.
- **Moon**: part of the scenes by default; Lights → *Moon* switches it to the Ambient Lighting
  colour set in the Meshtastic app. Lights → *Brand colour* sets the Your Brand colour on the
  badge.
- **Light shows**: Lights → *Share now* sends your scene and its timing to every badge in range,
  and they all play it in step. *Lead show* re-sends it every minute; turn *Join shows* off to
  ignore other people's shows.
- **Flashlight**: hold `0` (default shortcut) or badge menu → *Flashlight*.
- **Power**: Settings → *LEDs, screen asleep* (stay on / dim to 30% / off) and night mode cap
  them; the firmware also caps the total LED current, because a sagging supply makes the LEDs
  misread their data.
- **Driver**: `BadgeLedOut.cpp` sends the LED data itself on RMT channels set up once and kept
  (the NeoPixel library re-routes the pin for every update and feeds a one-block buffer from a
  low-priority interrupt, which made LEDs flash white when the radio or screen delayed it).
  Every LED is also resent once a second, and a fading channel can't flicker between off and its
  lowest step.

To add a scene, add a line to `SCENES[]` at the bottom of `BadgeScenes.cpp`: a name, a palette
and one effect for each part. New palettes are four colours (base, accent, highlight, dark) in
`PALETTES[]`. `tools/scene_preview.cpp` renders every scene to a GIF on your PC:

```
cd variants/esp32s3/utah_bsides/tools
g++ -std=c++17 -O2 -I.. scene_preview.cpp ../BadgeScenes.cpp -o scene_preview
./scene_preview > scenes.json && python3 scene_preview.py scenes.json   # needs Pillow
```

## Time

The badge has no clock chip or backup battery, so the time is gone after power off. It comes
back from the Meshtastic app over Bluetooth, from the Badge WiFi page (the phone's or
computer's browser sends its time and time zone when the page opens), from a mesh node with
GPS, or from other badges: a badge without the time asks every 2 minutes, and a badge within 2
hops that knows answers (only the time is sent, and only when asked). Meshtastic's clock page
stays hidden until the time is known.

Settings → *Time zone* (the same list as Meshtastic's). With none set the badge uses US Mountain
time (Utah).

## QR code

Badge menu -> *QR code* (or a shortcut): shows a QR code on the screen for people to scan. Left /
right (or `4` / `6`) switch between three cards:

- **My contact**: a vCard built from your name tag name (scanning it adds you as a contact).
- **Link**: a URL you set.
- **Text**: any text you set.

`#` edits the Link or Text on the badge; both can also be set from the Badge WiFi page (*Settings*
tab). `*` leaves. The screen stays on while it's shown so it can be scanned.

## Stopwatch & timer

Badge menu -> *Stopwatch & timer* (or a shortcut). `#` switches between the two; `*` done.

- **Stopwatch**: `5` start / stop, `9` lap (keeps a list), `0` reset.
- **Countdown timer** (e.g. a talk timer): while stopped, `2` / `8` set minutes and `4` / `6` set
  seconds; `5` starts / pauses, `0` resets. At zero the keypad flashes red and the screen says
  "Time!" (the badge has no buzzer). Any key clears it.

## Badge WiFi (phone)

Badge menu → *Badge WiFi (phone)* (or `#` in the Sightings log): the badge becomes its own WiFi
network, WPA2 with a new random 8-character password each time and one device at a time. It
keeps serving even when the lock screen comes up (so your phone's session isn't dropped); `*` on
the badge's Badge WiFi screen stops it on purpose, and it also stops on its own after about 5
minutes with no phone connected. While it's sharing, a small **WiFi** badge shows on the name
tag, the lock screen and the Badge page, so it's obvious the badge is still transmitting even
after you've left the Badge WiFi screen. A new session always gets a fresh password; it only
stays the same while that session is still up (e.g. kept alive under the lock screen). The web
page tells you clearly when the badge has stopped
sharing (a red dot by the status line, then a full "Badge stopped sharing" notice when it can no
longer reach the badge). Its web page (`tools/portal/index.html`, styled like the badge: night
sky, the raven on the sign, gold keypad buttons; built into the firmware) has:

- **Sightings**: download the sightings log CSV and the packet captures (mesh `capture.pcap`, `wifi.pcap`, `ble.pcap`).
- **Pictures**: add pictures and GIFs (the browser converts them to the badge's screen; they show
  on the name tag after the built-in ones), reorder or delete them.
- **Settings**: everything the badge's own *Settings* menu has (apart from the interactive
  joystick calibration and factory reset): name tag text, font and size, long names (scroll vs two
  lines), what shows at startup, the lock screen (clock / cycle / cycle pictures / cycle favourites
  / name / one chosen picture), lights, light speed, brightness, LEDs while the screen sleeps,
  screen colour, time zone, night mode, name tag stays on, welcome tips, home screen
  (badge tiles vs Meshtastic pages), `!commands` by DM, canned messages, QR codes and BT remote macros.
- **Message**: type a message with the phone's keyboard and send it from the badge. The page warns
  that the phone's keyboard app may keep or upload what's typed, and that messages to the whole
  channel are readable by anyone with a Meshtastic radio.
- **Live**: the Bluetooth devices around the badge with their marks. A search box filters by name,
  maker, address or mark; **tap a device to explore it** -- the badge connects and lists what it
  offers (services, name, maker, model, firmware, battery, ...), read only, the same as *Explore*
  on the badge screen.
- **Rules**: your own scan rules (see *Your own rules*).
- **Map**: the nodes that share their position, drawn round the badge (tap a dot for its name),
  plus a list. No map tiles: the phone has no internet while it's on the badge's WiFi.
- **Backup**: download / restore the badge settings (not Meshtastic's settings or keys).
- **Update**: signed firmware updates (see *Building and installing*).

Opening the page also sets the badge's time and time zone from the phone. Not while
Meshtastic's own WiFi is on.

## Other mesh features

- **Badges met**: badges are nodes reporting hardware model PRIVATE_HW (like this one). Meeting a
  new one gives a light burst and counts towards achievements.
- **Remote commands**: a direct message to the badge starting with `!` changes its lights and gets
  a reply: `!lights next`, `!lights list`, `!lights skinwalker`, `!bright high`, `!speed slow`,
  `!help`. Turn it off in Settings.
- **Phone colour**: the APP COLOUR and APP SHIMMER scenes use the colour set in the Meshtastic app
  under Module settings → Ambient Lighting.

The badge's own traffic is small and goes on the private app port (256): light shows, fox pings,
time requests/answers, invites, shared sightings, hunt beacons, mostly only when you ask.

### Scavenger hunt

Organisers hide one spare badge (a "beacon") per flag around the venue. A badge collects a flag
only by walking up to it: beacons send with hop limit 0 so the mesh never relays them, and a
badge only takes the flag when it hears the beacon directly at -75 dBm or stronger (about the
same room); weaker, it says "Hunt flag nearby: get closer". Beacons send every 20 s. Finding all
of them unlocks an achievement.

1. Edit the flag table in `BadgeHunt.cpp`: name, 8-character token, and the clue to the next
   spot. Flags are numbered from 0 in table order.
2. Build and install each beacon; `platformio.ini` has one ready-made env per flag:

   ```
   pio run -e utah_bsides_beacon0 -t upload --upload-port /dev/ttyACM0
   ```

   (`beacon1` ... `beacon4` for the others; add envs if you add flags.)
3. Put each beacon at its spot on a USB power bank. It still works as a normal badge.

Tune the range with `-D HUNT_MIN_RSSI=-70` (stricter) or `-80` (looser) in the normal
`utah_bsides` build, and the beacon rate with `-D HUNT_BEACON_MS=...` in the beacon builds.
Walk the route with a badge first to check the range in each spot. The tokens are in the
public source, so the hunt is for fun, not cheat-proof.

### Minibadges

The three minibadge slots carry I2C on GPIO5 (SDA) and GPIO6 (SCL). The badge polls minibadges
that follow the I2C part of the minibadge standard every 2 s: a button press gives a light burst,
a text message pops up on screen, and minibadges that accept writes get the LED brightness and
your latest game score. The minibadge CLK is left exactly as it was: 1 Hz. The slots carry no SD
card lines, so an SD card can't be added through them.

The *Minibadges* screen has a second page (`#`): an **I2C scanner** of the main bus (GPIO1 / GPIO2,
the one the keypad shares). It lists the addresses that answer, with a guess at common parts (OLED,
RTC, IMU, temp/humidity, ...); `#` rescans. Useful when wiring up an I2C sensor or a new minibadge.

**Minibadge ideas.** Because the slots are I2C, a minibadge can be more than a light-and-button toy:
it can carry a sensor or input the firmware can actually use. Each would need firmware support to be
read, but the I2C scanner (page 2 above) confirms a new part is wired and answering first. Some that
would extend the badge:

- **Battery fuel gauge** (MAX17048): the AA pack isn't wired to the ESP32, so the badge can't show a
  battery level today — an inline I2C gauge could.
- **Real-time clock** (DS3231): an accurate clock of its own, so the Clock tile and message
  timestamps are right without a phone or GPS.
- **GPS** (I2C, e.g. u-blox MAX-M10S): a fix of its own, so the Nodes map works without the phone
  sharing a location.
- **Compass / magnetometer** (QMC5883L, LIS3MDL): a real heading for the node map and fox-hunt
  direction instead of north-up only.
- **Environment sensor** (BME280 / BME680): temperature, humidity, pressure and air quality to show
  on-screen and share over the mesh as telemetry.
- **Motion sensor / IMU** (LSM6DS3, MPU-6050): a step counter, shake-to-act, or tilt as an extra input.
- **Haptic driver + motor** (DRV2605L): a buzz on a new message or a fox-hunt hit.
- **NFC reader** (PN532): tap two badges to trade contacts, or tap a tag to unlock.
- **Rotary encoder or extra keys** (an I2C GPIO expander like the TCA9535 already on the badge):
  smoother scrolling and menu navigation.
- **Ambient light / colour sensor** (VEML7700, TCS34725): auto-dim the screen and LEDs, or match the
  LED colour to the room.
- **Capacitive touch** (MPR121): touch pads as extra buttons or a slider.
- **Second display** (a small I2C OLED, or an e-ink name tag): a persistent readout that doesn't tie
  up the main screen.
- **Buzzer / speaker**: richer alert tones for messages and games (the badge already drives beeps).

## Hardware notes

### Dev pins (GPIO39–42)

Four ESP32-S3 GPIOs — **39, 40, 41, 42** — are broken out as dev/expansion pins. The stock firmware
leaves them free, so they're yours to build on (drive them from code in the badge folder). They are
**3.3 V digital** lines: no ADC and no touch (on the S3 those are GPIO1–20), so they're for digital
I/O and routed peripherals, not analog reads. None are strapping pins, so they're safe to pull or
drive at boot. They are also the chip's external **JTAG** pins (MTCK / MTDO / MTDI / MTMS =
39 / 40 / 41 / 42), so a JTAG probe (e.g. ESP-Prog) can use all four for hardware breakpoint
debugging — and the built-in USB Serial/JTAG still works if you'd rather use them as plain GPIO.

Through the ESP32-S3 GPIO matrix almost any digital peripheral can route to them. Project ideas:

- **Hardware debugging:** wire a JTAG probe for breakpoints and single-stepping.
- **Second serial port (UART):** talk to a GPS, another microcontroller, or a serial sensor; or a
  wired debug console.
- **Extra I2C (software):** the two hardware I2C controllers already run the main bus (GPIO1/2) and
  the minibadge bus (GPIO5/6), but a bit-banged I2C bus works fine here for more sensors or a display.
- **SPI (software, or a shared bus):** both hardware SPI controllers are already taken — LoRa on one,
  the ST7735 display on SPI3 — so a third SPI device here uses bit-banged SPI (fine for an SD card or
  a sensor, just slower), or wire it onto the display's bus with one of these pins as its chip-select.
- **PWM outputs (LEDC):** servos, a motor driver, a piezo buzzer/speaker, or extra dimmable LEDs.
- **Addressable LEDs (RMT):** drive a WS2812 strip for more lights.
- **IR (RMT):** transmit/receive — a remote control, badge-to-badge IR, or a TV-B-Gone.
- **Pulse counting (PCNT):** a rotary encoder as a scroll wheel, a Geiger counter, an anemometer, or
  frequency measurement.
- **Digital inputs:** buttons, switches, a reed/Hall sensor, a PIR motion sensor, or external
  interrupt triggers.
- **Switching outputs:** a relay or transistor to drive a load, trigger a camera, or signal another
  device.
- **More I/O:** a shift register or GPIO expander to get many more pins from just a few.

Keep it to 3.3 V logic and modest current — switch anything bigger than an LED or a logic signal
through a transistor or driver, and run motors or relays from their own supply with a shared ground.

### UART (GPIO43 / GPIO44)

A 4-pin serial header is broken out and is labeled UART on the badge. The four pins are:

| Pin | Signal | ESP32-S3 pin | Notes |
| --- | --- | --- | --- |
| **3V3** | 3.3 V power | — (power rail, not a GPIO) | powers a small attached device |
| **GND** | ground | — (ground) | common ground |
| **TX** | transmit (out of the badge) | **GPIO43** (U0TXD) | wire to the other device's RX |
| **RX** | receive (into the badge) | **GPIO44** (U0RXD) | wire to the other device's TX |

Only TX and RX are GPIOs (43 and 44), fixed in firmware (`UART_TX` / `UART_RX`); 3V3 and GND are the
power rails and have no GPIO number. 

This is 3.3 V logic and the ESP32-S3's default **UART0** — the serial **console**: Meshtastic's boot
and log messages come out on TX at **115200** baud (the USB port is the other console). Uses:

- **Serial logs / console:** connect a 3.3 V USB-TTL adapter (badge TX → adapter RX, badge RX →
  adapter TX, grounds common) and open it at 115200 (`pio device monitor -b 115200`, or any serial
  terminal) to watch the boot and Meshtastic logs and type to the console.
- **GPS:** wire a 3.3 V serial (NMEA) GPS and enable it in Meshtastic's position settings to give the
  badge its own fix for the Nodes map — it ships with no GPS.
- **Serial module:** Meshtastic's Serial module can send and receive mesh text over this port, proxy
  it to another device, or emit NMEA — bridging the mesh to a computer or microcontroller.
- **Another microcontroller or a serial sensor.**

3.3 V only — don't wire 5 V or RS-232 levels straight in (use a 3.3 V adapter or a level shifter).
Because it doubles as the log console, if you repurpose it for a device, set that up in Meshtastic's
config and expect some log bytes on TX at boot.

**Warning — the 3V3 pin is a power output, not a power input.** It is the badge's 3.3 V rail (the
onboard regulator's output, fed by USB or the AA cells). Only *draw* from it to power a small
attached device; do **not** feed an external supply into it while USB or batteries are connected.
Two regulated sources tied to one rail back-feed each other and can damage the onboard regulator —
and if the external source runs high, or someone applies 5 V here by mistake, it can damage the
ESP32 and everything on the 3.3 V rail. To power the badge *from* this pin instead, use a single
clean, current-limited 3.3 V source with USB unplugged and the AA cells removed, and never exceed
3.3 V. Always share ground.

### Power

The badge runs on 3 AA cells, and the battery isn't wired to the ESP32, so the firmware can't
show a battery level. The LEDs are the biggest drain you control (see *Lights*); the background
watch and a name tag that stays on add to it.

### LongFast: swapping the LoRa module

The stock module (DreamLNK **DL-LLCC68-S-915S**) uses the LLCC68 chip, a cut-down SX1262 that
can't do spreading factor 11 at 250 kHz. LongFast is SF11 at 250 kHz, so stock badges can't join
it; MediumFast (SF9) is the fastest common preset they can use.

The closest replacement is the **Heltec HT-RA62** (SX1262): same 16-pad footprint (Ai-Thinker
Ra-01 style), same pins **except BUSY**, which is on pad 10 instead of pad 5. Its pad 5 is TXEN,
an input to its RF switch, so it must not be left connected to the badge's BUSY line (GPIO14).

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
2. Cover the badge's pad 5 with a small piece of Kapton so the HT-RA62's pad 5 (TXEN) does not
   solder to it, and solder the HT-RA62 down.
3. Run a short wire from the HT-RA62's pad 10 (BUSY) to the badge's pad 5 copper (GPIO14; any
   point on that trace works).

No firmware change is needed: this build tries the SX1262 first and falls back to the LLCC68, so
one firmware runs on stock and modded badges. On a modded badge, pick LongFast in the app. Badges
on MediumFast and LongFast can't hear each other, so a group should pick one.

The Ai-Thinker Ra-01SH (SX1262) uses the same pinout as the HT-RA62, with TXEN/RXEN RF-control
pins of its own. If those must be driven by the MCU it needs two more wires and GPIOs, so check
its datasheet first; the HT-RA62 is the swap the Meshtastic docs recommend for the CircuitMess
Chatter, an LLCC68 board with the same problem (and the same BUSY jumper).

## Building and installing

**On GitHub:** every push to the `utah-bsides-badge` branch builds the firmware; the .bin files
are under the run's *Artifacts* on the Actions tab. Pushing a tag named `badge-v…` (for example
`badge-v2.7.26-1`) publishes a release: the *sign* job (in the protected `release` environment,
which needs approval) signs the WiFi update with the `BADGE_SIGNING_KEY` secret, and the web
installer is published to GitHub Pages. Actions on a fork start disabled; enable them once under
the repository's Actions tab.

**On your computer** (PlatformIO installed):

```
pio run -e utah_bsides                 # build
pio run -e utah_bsides -t upload       # flash over USB
pio device monitor -b 115200           # USB serial log (ESP32 core messages; Meshtastic logs to UART0)
```

The build runs these scripts first: `make_images.py` (pictures), `make_vendors.py` (maker lists
and rules), `make_portal.py` (the web page, with `tools/portal/raven.png` inlined) and
`make_chip8.py` (games); and `sign_update.py` afterwards (the signed `.update.bin`, when the key
is present). PC tests: `tools/keypad_test.cpp` (keypad, joystick, editor, shortcuts),
`chess_test.cpp`, `chip8_test.cpp`, `image_test.cpp`, `mine_preview.cpp`.

The badge's module is an ESP32-S3-WROOM-1-**N16R2**: 16 MB of flash and 2 MB of PSRAM. The build
uses both (`partition-table-16MB.csv`): two 6 MB firmware slots, so the badge can update itself
over WiFi (the new firmware goes into the slot that isn't running, then it switches), and ~3.9 MB
of file space. It won't boot on a module with less flash (N4 / N8).

### Installing on badges that already went out (once)

Badges with the 2025 firmware need **one full install with the new memory layout**. Meshtastic's
settings, node list, keys, channels and messages live in the file space, which moves, so they
start fresh (set the region again); the badge's own settings in NVS (LED scene, name tag,
joystick setup, achievements) stay. Easiest for attendees: the **web installer**
(`installer/index.html`, on GitHub Pages with every release): Chrome or Edge on a computer, plug
the badge in, press Install. Or by hand:

```
pio run -e utah_bsides -t erase        # optional: wipes everything, including badge settings
pio run -e utah_bsides -t upload
```

(Any esptool / web flasher works too: `firmware-utah_bsides-*.factory.bin` at 0x0.)

### Updates after that: over WiFi, no computer

Every signed build also makes `firmware-utah_bsides-*.update.bin`. On the badge: menu → **Badge
WiFi (phone)**, join the network it shows, open the address, **Update**, send the file, then
press **#** on the badge. If an update misbehaves,
install over USB / the web installer.


## Updating to a new Meshtastic release

```
git remote add upstream https://github.com/meshtastic/firmware.git   # once
git fetch upstream --tags
git checkout utah-bsides-badge
git merge v2.7.27.xxxxxxx          # the release tag you want
git push
```

Use release tags (stable), not `develop`, and read the release notes for API changes. The places
most likely to need a touch after a big release (all in this folder; the build stops with an
error rather than misbehaving):

- input event names (`src/input/InputBroker.h`) and the menu banner API (`src/graphics/Screen.h`),
  used in `BadgeKeypad.cpp`, `BadgeMenu.cpp` and `BadgeMeshSettings.cpp`;
- the Messages page takeover (`AppMessages.cpp`): it reads Meshtastic's `Screen::ui`,
  `framesetInfo` and `showingNormalScreen` (private members, through explicit template
  instantiation) to know which page is showing, and holds the auto-scroll of
  `MessageRenderer` (`scrollStartDelay`, `scrollStarted`);
- the clock page hiding and Badge page (`BadgeTime.cpp`, `BadgeMesh.cpp`): `toggleFrameVisibility`,
  `isFrameHidden`, `setFrames`;
- the 915 MHz scanner's radio access (`BadgeRfSniff.cpp`).

## Third-party code and data

- `qrcodegen.c` / `qrcodegen.h`: Project Nayuki's QR Code generator (MIT licence, notice in the
  files), v1.8.0, for the channel QR codes.
- `tools/fonts/`: the name fonts from Google Fonts (SIL Open Font License 1.1 or Apache 2.0,
  licence files next to each font). The original five use DejaVu, Liberation and GNU FreeFont.
- `chip8/`: games from John Earnest's CHIP-8 Archive (CC0).
- `tools/data/`: the IEEE OUI and Bluetooth SIG company lists, and the rule files.

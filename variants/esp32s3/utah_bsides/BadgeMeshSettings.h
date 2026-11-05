// Mesh settings on the badge, so the phone app is optional: node name, channels (join,
// create, share keys), which channel messages go to, transmit power, hop limit and the
// quick-send messages. Region, preset and role use Meshtastic's own pickers.
#pragma once

namespace badge
{

void meshSettingsOpen(); // the Mesh settings menu
void meshSettingsPoll();
bool meshSettingsBusy(); // from menuPoll(): opens queued sub-menus

// where the badge editor's messages go (Mesh settings > Send messages to)
const char *meshSendChannelName();
void meshSendPrepareTo(uint32_t dest, uint8_t channel); // a reply: that node (DM) or channel
void meshSendPrepare();
void meshJoinChannel(); // join or create a channel: name, then its key
const char *meshLeaveChannel(int idx); // remove it from this badge; nullptr, or why not
const char *meshRekeyChannel(int idx); // a new random key; nullptr, or why not
// the channel with this name (-1 none); *sameKey: whether it already has this key
int meshChannelNamed(const char *name, const uint8_t *psk, int len, bool *sameKey);
// join a channel from an invite (a group of that name with another key gets the new key); returns its index (-1 if not); *why: a note to show, or nullptr
int meshJoinWithKey(const char *name, const uint8_t *psk, int len, const char **why); // point Meshtastic's text box at that channel before typing into it

// "Your Brand" colour (Meshtastic's Ambient Lighting colour), picked on the badge
int brandColourCount();
const char *brandColourName(int i); // i < 0: "Custom (app)"
int brandColour();                  // -1 if the app set a colour that isn't one of these
void brandColourSet(int i);

} // namespace badge

// Mesh settings on the badge, so the phone app is optional: node name, channels (join,
// create, share keys), which channel messages go to, transmit power, hop limit and the
// quick-send messages. Region, preset and role use Meshtastic's own pickers.
#pragma once

namespace badge
{

void meshSettingsOpen(); // the Mesh settings menu
void meshSettingsPoll(); // from menuPoll(): opens queued sub-menus

// where the badge editor's messages go (Mesh settings > Send messages to)
const char *meshSendChannelName();
void meshSendPrepare(); // point Meshtastic's text box at that channel before typing into it

// "Your Brand" colour (Meshtastic's Ambient Lighting colour), picked on the badge
int brandColourCount();
const char *brandColourName(int i); // i < 0: "Custom (app)"
int brandColour();                  // -1 if the app set a colour that isn't one of these
void brandColourSet(int i);

} // namespace badge

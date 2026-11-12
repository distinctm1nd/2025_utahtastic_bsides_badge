// MeshCore settings on the badge (MeshCore mode only), so the MeshCore phone app is not needed:
// announce yourself, contacts (favourite, forget the route, delete, add someone heard), hashtag
// channels, the radio (regional presets or each value), auto-add and location sharing.
// Private channels are made in Messages > New chat, exactly as in Meshtastic mode: a channel's
// name and key are the same thing on both meshes.
#pragma once
namespace badge
{
void mcSettingsOpen(); // the MeshCore settings menu
void mcSettingsPoll(); // from menuPoll(): opens queued sub-menus
bool mcSettingsBusy();
} // namespace badge

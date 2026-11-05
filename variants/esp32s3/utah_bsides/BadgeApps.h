// Openers for every badge app (each lives in its own App*.cpp file).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace badge
{

void openCompose(int firstKey = -1); // AppEditor.cpp: message editor (sends via Meshtastic)
void openTaglineEditor();            // AppEditor.cpp: edit the name tag's tagline
// AppEditor.cpp: any short text; done(text) when saved, done(nullptr) if cancelled.
// caseMode: 0 abc, 1 Abc, 2 ABC
void openTextEditor(const char *title, const char *initial, int maxLen, int caseMode, void (*done)(const char *text));
void openReply(uint32_t dest, uint8_t channel, const char *name); // AppEditor.cpp: reply to a DM / channel
void openMessages();                 // AppMessages.cpp: the badge's chat-style message view
void openMessagesCompose(int firstKey); // ...opened to write a new message (firstKey: digit tapped, -1 none)
void openMessagesChannel(uint8_t ch);   // ...opened straight into a channel (from the home screen)
void messagesPoll(uint32_t now);
bool messagesTakePageTurn(int dir); // a page turn onto Meshtastic's Messages page opens ours instead     // keypad thread: show it in place of Meshtastic's Messages page
void openNameEditor();               // AppEditor.cpp: edit the name tag's name
void openJoystickSetup();            // AppJoystick.cpp: correct for the angled joystick
void openLightsApp();                // AppLights.cpp: LED scenes with live preview
class App;
App *nametagView(const char *what); // the name tag, set to show "@tag" (the name) or a picture
int nametagChoiceCount();           // the name, the built-in pictures, the uploaded ones
const char *nametagChoice(int i);   // "@tag" or a picture's name
bool nametagIsFavorite(const char *name); // AppNameTag.cpp: pictures marked with 0 in the name tag
bool nametagSetFavorite(const char *name, bool on);
int nametagFavoriteCount();
bool nametagScroll();                 // long names scroll on one line (else two lines)
void nametagSetScroll(bool on);
const uint8_t *nametagStyleFont(int which); // the name font in use: 0 big, 1 medium, 2 small
void openFontPicker();                // AppNameTag.cpp: pick the name font, seeing your name in each
void openNameTag();                  // AppNameTag.cpp: name tag + pictures
bool nametagShowStartup();           // AppNameTag.cpp: at boot, the name tag / picture chosen for startup
void openSimon();                    // AppGames.cpp
void openWhack();                    // AppGames.cpp
void openSnake();                    // AppGames.cpp
void openTetris();                   // AppTetris.cpp
void openChess();                    // AppChess.cpp
void openSolitaire();                // AppSolitaire.cpp: Klondike solitaire
void openMahjong();                  // AppMahjong.cpp: Mahjong solitaire (tile matching)
void openMine();                     // AppMine.cpp: Skinwalker Mine, the Doom-style shooter
void openSkinwalkerPet();            // AppSkinwalker.cpp: a creature you raise over the conference
void openChip8();                    // AppChip8.cpp: public-domain CHIP-8 games
void openFlashlight();               // AppFlashlight.cpp: all LEDs white
void openBleRemote();                // AppBleRemote.cpp: badge as a Bluetooth mouse / slide clicker / media keys
void openHuntFor(bool bluetooth, const uint8_t mac[6], bool publicAddr = false); // AppRadioHunt.cpp: hunt one device
void openFlagged();                  // AppFlagged.cpp: flagged devices, one row each
void openFlaggedAt(const uint8_t mac[6]);
void openRadar();                    // AppRadar.cpp: nodes that share their position, around the badge
void openTicTacToeCpu();             // AppTicTacToe.cpp: against the badge
void openTicTacToeMesh(uint32_t opponent, bool weInvited); // AppTicTacToe.cpp: against another badge
void tttPoll(); // call often: shows tic-tac-toe invitations
void openFoxHunt(uint32_t target);   // AppFoxHunt.cpp
void openNodeList();                 // AppNodeList.cpp: all LoRa/Meshtastic nodes, pick one to hunt
void openRadioHunt(bool bluetooth);  // AppRadioHunt.cpp: hunt a WiFi access point or Bluetooth device
void openEverythingHunt();          // AppRadioHunt.cpp: WiFi and Bluetooth in one list
void openRfScanner();                // AppRfScan.cpp: 902-928 MHz band and level meter (pauses Meshtastic)
void openSightingsLog();             // AppSightLog.cpp: what the fox hunt watch logged
void openBadgeWifi();                // AppPortal.cpp: the badge's own WiFi + web page (phone)
void openQrCode();                   // AppQr.cpp: show a QR (contact / link / text) to be scanned
void openTimer();                    // AppTimer.cpp: stopwatch and countdown timer
void openCaptureApp();               // AppCapture.cpp: mesh / WiFi / BLE packet capture to pcap
void openScanStats();                // AppScanStats.cpp: WiFi / BLE summary dashboard
void openPagesMenu();                // AppPages.cpp: overlay menu to jump straight to a Meshtastic page
void pagesPoll();                    // ...keypad thread: carries out a page jump picked in that overlay
void qrGet(bool link, char *out, size_t n); // AppQr.cpp: the QR Link / Text payloads
void qrSet(bool link, const char *val);
void macrosGet(char *out, size_t n); // AppBleRemote.cpp: the BT-remote keystroke macros
void macrosSet(const char *val);
void openBadgesApp();                // AppBadges.cpp: badges met, achievements, hunt flags
void openMinibadgesApp();            // AppBadges.cpp: minibadges on the I2C bus

// name tag text (NVS "utah": "tagname", "tagline"); the name starts as the Meshtastic name
const char *nametagName();
bool nametagNameSet(); // true only when a custom badge name has been typed in
void nametagSetName(const char *text);
const char *taglineGet(); // optional second line, empty by default
void taglineSet(const char *text);
// name tag font and size (AppNameTag.cpp)
int nametagFontCount();
const char *nametagFontName(int i);
int nametagFont();
void nametagSetFont(int i);
const char *nametagSizeName(int i); // 0 Auto, 1 Big, 2 Medium, 3 Small
int nametagSize();
void nametagSetSize(int i);

} // namespace badge

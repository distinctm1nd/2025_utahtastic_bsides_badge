// Openers for every badge app (each lives in its own App*.cpp file).
#pragma once
#include <stdint.h>

namespace badge
{

void openCompose(int firstKey = -1); // AppEditor.cpp: message editor (sends via Meshtastic)
void openTaglineEditor();            // AppEditor.cpp: edit the name tag's tagline
void openLightsApp();                // AppLights.cpp: LED scenes with live preview
void openNameTag();                  // AppNameTag.cpp: name tag + pictures
void openSimon();                    // AppGames.cpp
void openWhack();                    // AppGames.cpp
void openSnake();                    // AppGames.cpp
void openTetris();                   // AppTetris.cpp
void openTicTacToeCpu();             // AppTicTacToe.cpp: against the badge
void openTicTacToeMesh(uint32_t opponent, bool weInvited); // AppTicTacToe.cpp: against another badge
void tttPoll(); // call often: shows tic-tac-toe invitations
void openFoxHunt(uint32_t target);   // AppFoxHunt.cpp
void openBadgesApp();                // AppBadges.cpp: badges met, achievements, hunt flags
void openMinibadgesApp();            // AppBadges.cpp: minibadges on the I2C bus

// tagline shown on the name tag (NVS "utah"/"tagline")
const char *taglineGet();
void taglineSet(const char *text);

} // namespace badge

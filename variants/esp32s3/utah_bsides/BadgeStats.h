// Badge collection, achievements, scavenger-hunt flags and game high scores, kept in NVS
// (namespace "utahstat") so they survive reboots and firmware updates.
#pragma once
#include <stdint.h>

namespace badge
{

enum Achievement : uint8_t {
    ACH_FIRST_WORDS, // sent a message from the badge
    ACH_SOCIAL,      // met 5 badges
    ACH_NETWORKER,   // met 15 badges
    ACH_LEGEND,      // met 40 badges
    ACH_KONAMI,      // entered the Konami code
    ACH_SIMON,       // reached level 10 in Simon
    ACH_WHACK,       // 40 points in Whack-a-mole
    ACH_SNAKE,       // 25 points in Snake
    ACH_TETRIS,      // cleared 20 lines in Tetris
    ACH_TTT,         // beat another badge at tic-tac-toe
    ACH_FOX,         // got close to the fox (strong signal)
    ACH_SHOW,        // started or joined a synced light show
    ACH_FAR,         // heard a node 3 or more hops away
    ACH_MINIBADGE,   // attached a minibadge that talks I2C
    ACH_HUNT,        // found every scavenger-hunt flag
    ACH_NAMETAG,     // wore the name tag for 10 minutes
    ACH_COUNT
};

const char *achievementName(int a);
const char *achievementHint(int a);

void statsLoad();                   // once at start-up
bool statsHas(int a);
int statsAchievedCount();
void statsAchieve(int a);           // unlocks (banner + lights) the first time

// badges met (nodes reporting hardware model PRIVATE_HW, like this badge)
bool statsMetBadge(uint32_t node);  // true if this is a new one (already stored)
int statsBadgeCount();
uint32_t statsBadgeAt(int i);

void statsMessageSent();

// high scores: game ids
enum Game : uint8_t { GAME_SIMON, GAME_WHACK, GAME_SNAKE, GAME_TETRIS, GAME_TTT_WINS, GAME_COUNT };
uint16_t statsHigh(int game);
bool statsReportScore(int game, uint16_t score); // true on a new high score

// scavenger hunt flags (table in BadgeHunt.cpp)
bool statsHasFlag(int i);
void statsSetFlag(int i);
int statsFlagCount();

} // namespace badge

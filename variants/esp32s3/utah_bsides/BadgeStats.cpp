// Persistent badge stats. See BadgeStats.h.
#include "configuration.h"

#include "BadgeApp.h"
#include "BadgeHunt.h"
#include "BadgeLeds.h"
#include "BadgeMinibadge.h"
#include "BadgeStats.h"

#include "graphics/Screen.h"
#include <Preferences.h>
#include <stdio.h>
#include <string.h>

namespace badge
{

static const char *NS = "utahstat";
static const int MAX_BADGES = 250;

static uint32_t achieved = 0;
static uint32_t flags = 0;
static uint16_t highs[GAME_COUNT];
static uint32_t badges[MAX_BADGES];
static int nbadges = 0;
static bool loaded = false;

static const char *NAMES[ACH_COUNT] = {
    "First words",   "Social",      "Networker",  "Mesh legend", "Konami",   "Simon says", "Whack master",    "Snake charmer",
    "Block stacker", "Tic-tac-pro", "Fox hunter", "Light show",  "Far away", "Minibadge",  "Treasure hunter", "Show off",
};
static const char *HINTS[ACH_COUNT] = {
    "Send a message from the badge",
    "Meet 5 badges",
    "Meet 15 badges",
    "Meet 40 badges",
    "Up up down down...",
    "Reach level 10 in Simon",
    "Score 40 in Whack-a-mole",
    "Score 25 in Snake",
    "Clear 20 lines in Tetris",
    "Beat a badge at tic-tac-toe",
    "Get close to the fox",
    "Start or join a light show",
    "Hear a node 3+ hops away",
    "Plug in a talking minibadge",
    "Find every hunt flag",
    "Wear the name tag 10 min",
};

const char *achievementName(int a)
{
    return (a >= 0 && a < ACH_COUNT) ? NAMES[a] : "?";
}
const char *achievementHint(int a)
{
    return (a >= 0 && a < ACH_COUNT) ? HINTS[a] : "";
}

static void save()
{
    Preferences p;
    if (!p.begin(NS, false))
        return;
    p.putUInt("ach", achieved);
    p.putUInt("flags", flags);
    p.putBytes("highs", highs, sizeof(highs));
    p.putBytes("badges", badges, sizeof(uint32_t) * nbadges);
    p.end();
}

void statsLoad()
{
    if (loaded)
        return;
    loaded = true;
    memset(highs, 0, sizeof(highs));
    Preferences p;
    if (!p.begin(NS, true))
        return;
    achieved = p.getUInt("ach", 0);
    flags = p.getUInt("flags", 0);
    p.getBytes("highs", highs, sizeof(highs));
    size_t n = p.getBytesLength("badges");
    if (n > sizeof(badges))
        n = sizeof(badges);
    if (n)
        p.getBytes("badges", badges, n);
    nbadges = (int)(n / sizeof(uint32_t));
    p.end();
}

bool statsHas(int a)
{
    return a >= 0 && a < ACH_COUNT && (achieved & (1u << a));
}

int statsAchievedCount()
{
    int c = 0;
    for (int i = 0; i < ACH_COUNT; i++)
        c += statsHas(i);
    return c;
}

void statsAchieve(int a)
{
    if (a < 0 || a >= ACH_COUNT || statsHas(a))
        return;
    achieved |= 1u << a;
    save();
    static char msg[64];
    snprintf(msg, sizeof(msg), "Achievement!\n%s", NAMES[a]);
    ledsFlourish();
    if (screen && !appActive())
        screen->showSimpleBanner(msg, 3500);
    LOG_INFO("Badge achievement: %s", NAMES[a]);
}

bool statsMetBadge(uint32_t node)
{
    for (int i = 0; i < nbadges; i++)
        if (badges[i] == node)
            return false;
    if (nbadges < MAX_BADGES) {
        badges[nbadges++] = node;
        save();
    }
    if (nbadges >= 5)
        statsAchieve(ACH_SOCIAL);
    if (nbadges >= 15)
        statsAchieve(ACH_NETWORKER);
    if (nbadges >= 40)
        statsAchieve(ACH_LEGEND);
    return true;
}

int statsBadgeCount()
{
    return nbadges;
}
uint32_t statsBadgeAt(int i)
{
    return (i >= 0 && i < nbadges) ? badges[i] : 0;
}

void statsMessageSent()
{
    statsAchieve(ACH_FIRST_WORDS);
}

uint16_t statsHigh(int game)
{
    return (game >= 0 && game < GAME_COUNT) ? highs[game] : 0;
}

bool statsReportScore(int game, uint16_t score)
{
    if (game < 0 || game >= GAME_COUNT)
        return false;
    if (game != GAME_TTT_WINS)
        minibadgeSendScore(score); // minibadges that show scores
    switch (game) {
    case GAME_SIMON:
        if (score >= 10)
            statsAchieve(ACH_SIMON);
        break;
    case GAME_WHACK:
        if (score >= 40)
            statsAchieve(ACH_WHACK);
        break;
    case GAME_SNAKE:
        if (score >= 25)
            statsAchieve(ACH_SNAKE);
        break;
    case GAME_TETRIS:
        if (score >= 20)
            statsAchieve(ACH_TETRIS);
        break;
    default:
        break;
    }
    if (score <= highs[game])
        return false;
    highs[game] = score;
    save();
    return true;
}

bool statsHasFlag(int i)
{
    return i >= 0 && i < 32 && (flags & (1u << i));
}

void statsSetFlag(int i)
{
    if (i < 0 || i >= huntFlagCount() || statsHasFlag(i))
        return;
    flags |= 1u << i;
    save();
    if (statsFlagCount() == huntFlagCount())
        statsAchieve(ACH_HUNT);
}

int statsFlagCount()
{
    int c = 0;
    for (int i = 0; i < huntFlagCount(); i++)
        c += statsHasFlag(i);
    return c;
}

} // namespace badge

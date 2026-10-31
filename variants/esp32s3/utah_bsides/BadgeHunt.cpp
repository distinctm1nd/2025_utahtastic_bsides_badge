// Scavenger hunt flags. Change these for your event: anyone who reads this file (or sniffs
// the radio) can find the tokens, which at a hacker con is part of the game.
#include "BadgeHunt.h"
#include <string.h>

namespace badge
{

static const HuntFlag FLAGS[] = {
    {"Registration", "R3G1STER", "Where the talks begin: find the main stage."},
    {"Main stage", "ST4GE0N3", "Hungry? The fox likes snacks. Check the food."},
    {"Snack table", "N0MN0MN0", "Solder smoke rises. Look for the hardware village."},
    {"Hardware village", "S0LD3R3D", "Last one: where the badges were born. Ask the badge team."},
    {"Badge team", "B4DG3T3M", "You found them all. Show the badge team your badge!"},
};

int huntFlagCount()
{
    return (int)(sizeof(FLAGS) / sizeof(FLAGS[0]));
}

const HuntFlag &huntFlag(int i)
{
    if (i < 0 || i >= huntFlagCount())
        i = 0;
    return FLAGS[i];
}

// payload after "BHN": 'F', flag index, 8-byte token
int huntHandlePacket(const uint8_t *p, size_t len)
{
    if (len < 10 || p[0] != 'F')
        return -1;
    int idx = p[1];
    if (idx >= huntFlagCount())
        return -1;
    if (memcmp(p + 2, FLAGS[idx].token, 8) != 0)
        return -1;
    return idx;
}

size_t huntBeaconPacket(uint8_t *out, size_t max)
{
#ifdef BADGE_HUNT_BEACON
    const int idx = BADGE_HUNT_BEACON;
    if (idx < 0 || idx >= huntFlagCount() || max < 13)
        return 0;
    memcpy(out, "BHNF", 4);
    out[4] = (uint8_t)idx;
    memcpy(out + 5, FLAGS[idx].token, 8);
    return 13;
#else
    (void)out;
    (void)max;
    return 0;
#endif
}

} // namespace badge

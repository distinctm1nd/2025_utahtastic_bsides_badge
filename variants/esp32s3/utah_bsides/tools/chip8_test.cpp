// Runs every CHIP-8 game in chip8/ for 10 simulated seconds and checks it didn't crash and drew
// something (the build makes generated/BadgeChip8Roms.h first).
//   g++ -std=c++17 -I.. chip8_test.cpp -o chip8_test && ./chip8_test
#include "../generated/BadgeChip8Roms.h"
#include <stdio.h>
using namespace badge;

int main()
{
    int bad = 0;
    for (int g = 0; g < CHIP8_GAME_COUNT; g++) {
        Chip8 c;
        c.load(CHIP8_GAMES[g].rom, CHIP8_GAMES[g].len, CHIP8_GAMES[g].quirks);
        for (int f = 0; f < 600; f++) {
            c.keys[6] = (f / 30) % 2; // the action key now and then, past title screens
            c.frame(CHIP8_GAMES[g].tickrate);
        }
        int lit = 0;
        for (int y = 0; y < Chip8::H; y++)
            for (int x = 0; x < Chip8::W; x++)
                lit += c.pixel(x, y);
        bool ok = !c.crashed && lit > 0;
        bad += !ok;
        printf("%s  %-16s %4d pixels lit\n", ok ? "PASS" : "FAIL", CHIP8_GAMES[g].title, lit);
    }
    printf(bad ? "\n%d failed\n" : "\nall passed\n", bad);
    return bad != 0;
}

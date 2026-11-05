// A CHIP-8 interpreter (the 1970s COSMAC VIP virtual machine): 4 KB of memory, 16 registers,
// a 64x32 one-bit screen, a 16-key hex keypad, 60 Hz timers. Pure C++ so it can be tested on
// a PC (tools/chip8_test.cpp). The quirks follow Octo's flags, as the CHIP-8 Archive's games
// list them.
#pragma once
#include <stdint.h>
#include <string.h>

namespace badge
{

struct Chip8Quirks {
    bool shift = false;     // 8XY6 / 8XYE shift VX in place (ignore VY)
    bool loadStore = false; // FX55 / FX65 leave I unchanged
    bool clip = false;      // sprites clip at the screen edge instead of wrapping
    bool jump = false;      // BNNN jumps to NNN + VX (X = high nibble of NNN)
    bool vBlank = false;    // drawing waits for the next frame
    bool logic = false;     // 8XY1 / 8XY2 / 8XY3 reset VF
};

class Chip8
{
  public:
    static const int W = 64, H = 32;
    uint8_t screen[W * H / 8]; // one bit per pixel, row-major, MSB = leftmost
    bool keys[16] = {};
    bool dirty = true;  // the screen changed
    bool crashed = false;

    void load(const uint8_t *rom, size_t len, const Chip8Quirks &q)
    {
        memset(mem, 0, sizeof(mem));
        static const uint8_t FONT[80] = {
            0xF0, 0x90, 0x90, 0x90, 0xF0, 0x20, 0x60, 0x20, 0x20, 0x70, 0xF0, 0x10, 0xF0, 0x80, 0xF0, 0xF0, 0x10, 0xF0, 0x10, 0xF0,
            0x90, 0x90, 0xF0, 0x10, 0x10, 0xF0, 0x80, 0xF0, 0x10, 0xF0, 0xF0, 0x80, 0xF0, 0x90, 0xF0, 0xF0, 0x10, 0x20, 0x40, 0x40,
            0xF0, 0x90, 0xF0, 0x90, 0xF0, 0xF0, 0x90, 0xF0, 0x10, 0xF0, 0xF0, 0x90, 0xF0, 0x90, 0x90, 0xE0, 0x90, 0xE0, 0x90, 0xE0,
            0xF0, 0x80, 0x80, 0x80, 0xF0, 0xE0, 0x90, 0x90, 0x90, 0xE0, 0xF0, 0x80, 0xF0, 0x80, 0xF0, 0xF0, 0x80, 0xF0, 0x80, 0x80};
        memcpy(mem + 0x50, FONT, sizeof(FONT));
        if (len > sizeof(mem) - 0x200)
            len = sizeof(mem) - 0x200;
        memcpy(mem + 0x200, rom, len);
        memset(v, 0, sizeof(v));
        memset(screen, 0, sizeof(screen));
        memset(keys, 0, sizeof(keys));
        pc = 0x200, I = 0, sp = 0, dt = st = 0;
        waitKey = -1;
        quirks = q;
        dirty = true;
        crashed = false;
        rng = 0x2545F491;
    }

    // one frame (1/60 s): up to `cycles` instructions, then the timers tick
    void frame(int cycles)
    {
        drewThisFrame = false;
        for (int i = 0; i < cycles && !crashed; i++) {
            if (waitKey >= 0) { // FX0A: wait for a key to be pressed and released
                if (pressedKey < 0) {
                    for (int k = 0; k < 16; k++)
                        if (keys[k])
                            pressedKey = k;
                } else if (!keys[pressedKey]) {
                    v[waitKey] = pressedKey;
                    waitKey = pressedKey = -1;
                }
                break;
            }
            step();
            if (quirks.vBlank && drewThisFrame)
                break;
        }
        if (dt)
            dt--;
        if (st)
            st--;
    }

    bool beeping() const { return st > 0; }
    bool pixel(int x, int y) const { return screen[(y * W + x) >> 3] & (0x80 >> (x & 7)); }

  private:
    uint8_t mem[4096];
    uint8_t v[16];
    uint16_t I = 0, pc = 0x200, stack[16];
    uint8_t sp = 0, dt = 0, st = 0;
    int waitKey = -1, pressedKey = -1;
    bool drewThisFrame = false;
    Chip8Quirks quirks;
    uint32_t rng;

    uint8_t rand8()
    {
        rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5;
        return rng >> 24;
    }

    void draw(int x0, int y0, int n)
    {
        v[15] = 0;
        x0 %= W, y0 %= H;
        for (int r = 0; r < n; r++) {
            int y = y0 + r;
            if (y >= H) {
                if (quirks.clip)
                    break;
                y %= H;
            }
            uint8_t row = mem[(I + r) & 0xFFF];
            for (int c = 0; c < 8; c++) {
                if (!(row & (0x80 >> c)))
                    continue;
                int x = x0 + c;
                if (x >= W) {
                    if (quirks.clip)
                        continue;
                    x %= W;
                }
                int idx = y * W + x;
                uint8_t bit = 0x80 >> (idx & 7);
                if (screen[idx >> 3] & bit)
                    v[15] = 1;
                screen[idx >> 3] ^= bit;
            }
        }
        dirty = drewThisFrame = true;
    }

    void step()
    {
        uint16_t op = mem[pc & 0xFFF] << 8 | mem[(pc + 1) & 0xFFF];
        pc = (pc + 2) & 0xFFF;
        uint8_t x = (op >> 8) & 0xF, y = (op >> 4) & 0xF, n = op & 0xF, nn = op & 0xFF;
        uint16_t nnn = op & 0xFFF;
        switch (op >> 12) {
        case 0x0:
            if (op == 0x00E0) {
                memset(screen, 0, sizeof(screen));
                dirty = true;
            } else if (op == 0x00EE) {
                if (!sp) {
                    crashed = true;
                    return;
                }
                pc = stack[--sp];
            }
            break;
        case 0x1:
            pc = nnn;
            break;
        case 0x2:
            if (sp >= 16) {
                crashed = true;
                return;
            }
            stack[sp++] = pc;
            pc = nnn;
            break;
        case 0x3:
            if (v[x] == nn)
                pc += 2;
            break;
        case 0x4:
            if (v[x] != nn)
                pc += 2;
            break;
        case 0x5:
            if (v[x] == v[y])
                pc += 2;
            break;
        case 0x6:
            v[x] = nn;
            break;
        case 0x7:
            v[x] += nn;
            break;
        case 0x8: {
            uint8_t a = v[x], b = v[y], f = 0;
            switch (n) {
            case 0x0: v[x] = b; return;
            case 0x1: v[x] = a | b; if (quirks.logic) v[15] = 0; return;
            case 0x2: v[x] = a & b; if (quirks.logic) v[15] = 0; return;
            case 0x3: v[x] = a ^ b; if (quirks.logic) v[15] = 0; return;
            case 0x4: v[x] = a + b; f = (a + b) > 0xFF; break;
            case 0x5: v[x] = a - b; f = a >= b; break;
            case 0x6: if (!quirks.shift) a = b; v[x] = a >> 1; f = a & 1; break;
            case 0x7: v[x] = b - a; f = b >= a; break;
            case 0xE: if (!quirks.shift) a = b; v[x] = a << 1; f = a >> 7; break;
            default: return;
            }
            v[15] = f; // VF is written last, after the result
            break;
        }
        case 0x9:
            if (v[x] != v[y])
                pc += 2;
            break;
        case 0xA:
            I = nnn;
            break;
        case 0xB:
            pc = (nnn + (quirks.jump ? v[x] : v[0])) & 0xFFF;
            break;
        case 0xC:
            v[x] = rand8() & nn;
            break;
        case 0xD:
            draw(v[x], v[y], n);
            break;
        case 0xE:
            if (nn == 0x9E && keys[v[x] & 0xF])
                pc += 2;
            else if (nn == 0xA1 && !keys[v[x] & 0xF])
                pc += 2;
            break;
        case 0xF:
            switch (nn) {
            case 0x07: v[x] = dt; break;
            case 0x0A: waitKey = x; pressedKey = -1; break;
            case 0x15: dt = v[x]; break;
            case 0x18: st = v[x]; break;
            case 0x1E: I = (I + v[x]) & 0xFFF; break;
            case 0x29: I = 0x50 + (v[x] & 0xF) * 5; break;
            case 0x33:
                mem[I & 0xFFF] = v[x] / 100;
                mem[(I + 1) & 0xFFF] = (v[x] / 10) % 10;
                mem[(I + 2) & 0xFFF] = v[x] % 10;
                break;
            case 0x55:
                for (int i = 0; i <= x; i++)
                    mem[(I + i) & 0xFFF] = v[i];
                if (!quirks.loadStore)
                    I = (I + x + 1) & 0xFFF;
                break;
            case 0x65:
                for (int i = 0; i <= x; i++)
                    v[i] = mem[(I + i) & 0xFFF];
                if (!quirks.loadStore)
                    I = (I + x + 1) & 0xFFF;
                break;
            }
            break;
        }
    }
};

} // namespace badge

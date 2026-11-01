// Base64 for channel keys (the Meshtastic app shows and accepts keys this way, e.g. "AQ==").
// Pure C++, tested in tools/keypad_test.cpp.
#pragma once
#include <stdint.h>
#include <string.h>

namespace badge
{

// writes a NUL-terminated string; out must hold 4 * ((n + 2) / 3) + 1 chars
inline void base64Encode(const uint8_t *in, int n, char *out)
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int o = 0;
    for (int i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < n)
            v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < n)
            v |= in[i + 2];
        out[o++] = T[(v >> 18) & 63];
        out[o++] = T[(v >> 12) & 63];
        out[o++] = i + 1 < n ? T[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < n ? T[v & 63] : '=';
    }
    out[o] = 0;
}

// returns the number of bytes, or -1 if the text isn't base64 (spaces are ignored, padding
// optional, '-' and '_' accepted for '+' and '/')
inline int base64Decode(const char *s, uint8_t *out, int maxOut)
{
    uint32_t acc = 0;
    int bits = 0, n = 0;
    for (; *s; s++) {
        char c = *s;
        int v;
        if (c >= 'A' && c <= 'Z')
            v = c - 'A';
        else if (c >= 'a' && c <= 'z')
            v = c - 'a' + 26;
        else if (c >= '0' && c <= '9')
            v = c - '0' + 52;
        else if (c == '+' || c == '-')
            v = 62;
        else if (c == '/' || c == '_')
            v = 63;
        else if (c == '=' || c == ' ')
            continue;
        else
            return -1;
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n >= maxOut)
                return -1;
            out[n++] = (uint8_t)(acc >> bits);
        }
    }
    return n;
}

} // namespace badge

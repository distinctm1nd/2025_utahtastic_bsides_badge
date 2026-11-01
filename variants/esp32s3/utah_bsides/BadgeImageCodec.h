// Unpacks the pictures in BadgeImagesData.h (made by tools/make_images.py). Pure C++, tested
// in tools/image_test.cpp.
//
// Each frame is stored as the difference from the frame before (XOR; the first frame against
// a blank one), run-length packed with PackBits: a header byte n = 0..127 is followed by n+1
// literal bytes; n = 129..255 means the next byte repeated 257-n times; 128 is skipped.
// The frames' packed streams follow one another.
#pragma once
#include <stdint.h>
#include <string.h>

namespace badge
{

// XOR one frame's stream into `frame` (n bytes, the previous frame); returns the next stream
inline const uint8_t *unpackFrame(const uint8_t *src, uint8_t *frame, int n)
{
    int o = 0;
    while (o < n) {
        uint8_t h = *src++;
        if (h < 128) {
            for (int i = 0; i <= h && o < n; i++)
                frame[o++] ^= *src++;
        } else if (h > 128) {
            uint8_t v = *src++;
            for (int i = 0; i < 257 - h && o < n; i++)
                frame[o++] ^= v;
        }
    }
    return src;
}

// FNV-1a, to check unpacking against the original pixels
inline uint32_t imageCheck(uint32_t h, const uint8_t *p, int n)
{
    for (int i = 0; i < n; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

} // namespace badge

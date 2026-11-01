// Checks that the badge's unpacker (BadgeImageCodec.h) gives back exactly the pixels
// make_images.py packed, for every frame of every picture in BadgeImagesData.h.
//   g++ -std=c++17 -O2 -I.. image_test.cpp -o image_test && ./image_test
#include "../BadgeImageCodec.h"
#include "../BadgeImagesData.h"
#include <cstdio>
using namespace badge;
int main()
{
    int fails = 0;
    static uint8_t frame[20 * 128];
    for (int i = 0; i < BADGE_IMAGE_COUNT; i++) {
        const BadgeImage &im = BADGE_IMAGES[i];
        int n = (im.w + 7) / 8 * im.h;
        memset(frame, 0, sizeof(frame));
        const uint8_t *src = im.bits;
        uint32_t check = 2166136261u;
        for (int f = 0; f < im.frames; f++) {
            src = unpackFrame(src, frame, n);
            check = imageCheck(check, frame, n);
        }
        bool ok = check == im.check;
        fails += !ok;
        printf("%s  %-18s %2d frame(s), %5d bytes packed\n", ok ? "PASS" : "FAIL", im.name, im.frames, (int)(src - im.bits));
    }
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails;
}

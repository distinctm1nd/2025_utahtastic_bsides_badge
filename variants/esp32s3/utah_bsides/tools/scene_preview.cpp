// Renders every LED scene on a PC so you can check a new scene before flashing.
//   g++ -std=c++17 -O2 -I.. scene_preview.cpp ../BadgeScenes.cpp -o scene_preview
//   ./scene_preview > scenes.json        (10 s per scene at 25 fps, colours 0..255)
//   python3 scene_preview.py scenes.json  (writes scenes.gif, needs Pillow)
#include "../BadgeScenes.h"
#include <cmath>
#include <cstdio>

using namespace badge;

int main()
{
    const int fps = 25, seconds = 10;
    printf("{\"leds\":[");
    for (int i = 0; i < NUM_LEDS; i++)
        printf("%s[%.3f,%.3f,%d]", i ? "," : "", LEDS[i].x, LEDS[i].y, LEDS[i].group);
    printf("],\"fps\":%d,\"scenes\":[", fps);
    int bad = 0;
    for (int s = 0; s < sceneCount(); s++) {
        Rgb out[NUM_LEDS] = {};
        SceneCtx c;
        c.out = out;
        printf("%s{\"name\":\"%s\",\"frames\":[", s ? "," : "", sceneAt(s).name);
        float lit = 0;
        for (int f = 0; f < fps * seconds; f++) {
            c.t = (float)f / fps;
            c.dt = 1.0f / fps;
            c.reset = (f == 0);
            c.npressed = 0;
            if (f % 20 == 10) { // pretend someone types, for the reactive scenes
                c.pressed[0] = (int8_t)((f / 20) % KEYPAD_LEDS);
                c.npressed = 1;
            }
            sceneAt(s).fn(c);
            printf("%s[", f ? "," : "");
            for (int i = 0; i < NUM_LEDS; i++) {
                int v[3];
                for (int j = 0; j < 3; j++) {
                    float x = out[i][j];
                    if (std::isnan(x))
                        bad++;
                    x = x < 0 ? 0 : (x > 1 ? 1 : x);
                    lit += x;
                    v[j] = (int)(x * 255 + 0.5f);
                }
                printf("%s%d,%d,%d", i ? "," : "", v[0], v[1], v[2]);
            }
            printf("]");
        }
        printf("],\"avg\":%.4f}", lit / (fps * seconds * NUM_LEDS * 3));
    }
    printf("],\"nan\":%d}\n", bad);
    return bad ? 1 : 0;
}

// Pictures uploaded from the badge web page (the browser converts them), kept on the file
// system in /badge/pics as NN.bpi: "BPI1", u16 width, height, frames, frame ms (little-endian),
// a 24-byte name, then the frames packed as in BadgeImageCodec.h. Shown on the name tag after
// the built-in pictures.
#pragma once
#include "BadgeImagesData.h"
#include <stddef.h>
#include <stdint.h>

namespace badge
{

static const int MAX_PICTURES = 40;

int picCount();
const char *picName(int i);
// load picture i (frames stay in memory until the next load / picUnload); false if unreadable
bool picLoad(int i, BadgeImage &out);
void picUnload();
// one past the last byte of `im`'s frame data when it is the loaded (uploaded) picture, for
// unpackFrame()'s bounds check; nullptr for a built-in picture
const uint8_t *picDataEnd(const BadgeImage &im);
// a converted picture from the web page: header + frames as described above; nullptr if saved
const char *picSave(const uint8_t *data, size_t len);
bool picDelete(int i);
bool picMove(int i, int dir); // swap with the one before (-1) / after (+1)
size_t picFreeSpace();        // bytes left on the file system

// what shows at startup (NVS "startpic"): "" welcome tips, "@tag" the name tag, or a picture's name
const char *picStartup();
void picSetStartup(const char *what);

} // namespace badge

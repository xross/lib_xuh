#include <stdio.h>
#include <string.h>
#include "yuv_rgb565.h"
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

/* Independent colour-conversion oracle: upstream PL_MPEG produces RGB888;
 * only RGB565 packing is shared with the expected output representation. */
int main(void)
{
    enum { WIDTH = 320 };
    uint8_t strip[WIDTH * 3], rgb[WIDTH * 2 * 3];
    struct { uint8_t before[16], data[WIDTH * 2 * 2], after[16]; } output;
    plm_frame_t frame = {0};
    frame.width = WIDTH; frame.height = 2;
    frame.y.width = WIDTH; frame.y.height = 2; frame.y.data = strip;
    frame.cb.width = frame.cr.width = WIDTH / 2;
    frame.cb.height = frame.cr.height = 1;
    frame.cb.data = strip + 2 * WIDTH;
    frame.cr.data = frame.cb.data + WIDTH / 2;
    for (unsigned seed = 0; seed < 256; ++seed) {
        for (unsigned i = 0; i < WIDTH * 2; ++i) strip[i] = (i * 37 + seed) & 255;
        for (unsigned i = 0; i < WIDTH / 2; ++i) {
            frame.cb.data[i] = (i * 53 + seed) & 255;
            frame.cr.data[i] = (i * 71 + 255 - seed) & 255;
        }
        memset(&output, 0xa5, sizeof(output));
        yuv420_strip_to_rgb565(strip, output.data, WIDTH);
        plm_frame_to_rgb(&frame, rgb, WIDTH * 3);
        for (unsigned i = 0; i < WIDTH * 2; ++i) {
            unsigned pixel = ((rgb[3 * i] & 0xf8) << 8) |
                             ((rgb[3 * i + 1] & 0xfc) << 3) | (rgb[3 * i + 2] >> 3);
            if (output.data[2 * i] != pixel >> 8 || output.data[2 * i + 1] != (pixel & 255))
                return 1;
        }
        for (unsigned i = 0; i < 16; ++i)
            if (output.before[i] != 0xa5 || output.after[i] != 0xa5) return 1;
    }
    puts("PASS: 163840 pixels match upstream RGB-to-RGB565 packing; buffer guards intact");
    return 0;
}

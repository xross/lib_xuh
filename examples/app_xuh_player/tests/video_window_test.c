#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "video_window.h"
#include "fps_overlay.h"

static unsigned char frame[PIPELINE_LCD_WIDTH * PIPELINE_LCD_HEIGHT * 2];

static void check_picture(unsigned width, unsigned height)
{
    assert(video_window_valid(width, height));
    video_window_t window = video_window_center(width, height);
    if (width == 320 && height == 180)
        assert(FPS_OVERLAY_Y + FPS_OVERLAY_HEIGHT + 4 <= window.y);
    memset(frame, 0, sizeof(frame));
    for (unsigned y = 0; y < height; ++y)
        memset(frame + video_window_rgb_offset(window, y), 0x55, width * 2);
    fps_overlay_t fps = {0};
    fps_overlay_frame(&fps, 0, frame);
    for (unsigned y = 0; y < PIPELINE_LCD_HEIGHT; ++y) {
        for (unsigned x = 0; x < PIPELINE_LCD_WIDTH; ++x) {
            const unsigned char *pixel = frame + (y * PIPELINE_LCD_WIDTH + x) * 2;
            if (x >= FPS_OVERLAY_X && x < FPS_OVERLAY_X + FPS_OVERLAY_WIDTH &&
                y >= FPS_OVERLAY_Y && y < FPS_OVERLAY_Y + FPS_OVERLAY_HEIGHT)
                continue;
            unsigned expected = x >= window.x && x < window.x + width &&
                y >= window.y && y < window.y + height ? 0x55 : 0;
            assert(pixel[0] == expected && pixel[1] == expected);
        }
    }
    /* Same gauge origin and target marker, whether inside or outside video. */
    assert(frame[(4 * PIPELINE_LCD_WIDTH + 5) * 2] == 0x42);
    assert(frame[(20 * PIPELINE_LCD_WIDTH + 68) * 2] == 0xff);
}

int main(void)
{
    assert(!video_window_valid(0, 180));
    assert(!video_window_valid(320, 0));
    assert(!video_window_valid(324, 180));
    assert(!video_window_valid(320, 242));
    assert(!video_window_valid(318, 180));
    assert(!video_window_valid(316, 180)); /* Half-width chroma rows are whole words. */
    assert(!video_window_valid(320, 181));
    video_window_t window = video_window_center(320, 180);
    assert(window.x == 0 && window.y == 30);
    window = video_window_center(256, 144);
    assert(window.x == 32 && window.y == 48);
    check_picture(320, 240);
    check_picture(320, 180);
    check_picture(256, 144);
    check_picture(8, 2);
    check_picture(312, 238);
    puts("PASS: centred video windows, black borders, stride, bounds and fixed FPS coordinates");
    return 0;
}

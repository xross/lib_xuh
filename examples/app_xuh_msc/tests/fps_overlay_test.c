#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fps_overlay.h"
#include "video_config.h"

static unsigned char frame[PIPELINE_LCD_WIDTH * PIPELINE_LCD_HEIGHT * 2];

static unsigned pixel(unsigned x, unsigned y)
{
    const unsigned char *p = frame + 2 * (y * PIPELINE_LCD_WIDTH + x);
    return ((unsigned)p[0] << 8) | p[1];
}

static void render_rate(unsigned tenths)
{
    fps_overlay_t state = {.started = 1, .measured = 1, .tenths = tenths};
    memset(frame, 0x55, sizeof(frame));
    fps_overlay_frame(&state, 1, frame);
}

static void check_window(unsigned start)
{
    fps_overlay_t state = {0};
    memset(frame, 0x55, sizeof(frame));
    fps_overlay_frame(&state, start, frame);
    assert(state.started && !state.measured && !state.tenths);
    assert(pixel(9, 27) == 0x18c3); /* Empty track, placeholder already visible. */
    assert(pixel(68, 27) == 0xffff); /* 25-FPS target. */
    for (unsigned i = 1; i <= 13; ++i)
        fps_overlay_frame(&state, start + i * 4000000u, frame);
    assert(state.measured && state.tenths == 250); /* 25 FPS over 0.52 seconds. */
    assert(pixel(8, 27) == 0xca67); /* The left end stays red at 25 FPS. */
    unsigned white = 0, red = 0;
    for (unsigned y = 0; y < PIPELINE_LCD_HEIGHT; ++y) {
        for (unsigned x = 0; x < PIPELINE_LCD_WIDTH; ++x) {
            unsigned color = pixel(x, y);
            if (x >= 4 && x < 108 && y >= 4 && y < 38) {
                white += color == 0xffff;
                red += color == 0xca67;
            } else assert(color == 0x5555);
        }
    }
    assert(white && red);
    /* A subsequent half-second window at 10 FPS blends with the 25-FPS
     * reading, rather than making the gauge jump abruptly. */
    for (unsigned i = 1; i <= 5; ++i)
        fps_overlay_frame(&state, start + 52000000u + i * 10000000u, frame);
    assert(state.tenths == 175);
}

int main(void)
{
    check_window(0);
    check_window(UINT32_MAX - 10000000u);
    render_rate(25);
    assert(pixel(8, 27) == 0xca67);
    unsigned low = pixel(9, 27);
    assert(pixel(14, 27) == 0x18c3); /* Fill is only 6 of 96 pixels. */
    render_rate(125);
    assert(pixel(9, 27) == low); /* The existing fill does not change color. */
    render_rate(250);
    assert(pixel(8, 27) == 0xca67 && pixel(68, 27) == 0xffff);
    assert(pixel(38, 27) == 0xd4e7); /* Fixed amber midpoint. */
    unsigned target_color = pixel(67, 27);
    assert(((target_color >> 5) & 63) > 3 * (target_color >> 11));
    assert(pixel(69, 27) == 0x18c3);
    render_rate(1000);
    assert(pixel(8, 27) == 0xca67 && pixel(9, 27) == low);
    assert(pixel(69, 27) == 0x556a && pixel(103, 27) == 0x556a);
    assert(pixel(104, 27) == 0x4208); /* Above-scale fill never overruns. */
    unsigned previous_green = 0;
    for (unsigned x = 8; x < 104; ++x) {
        unsigned color = pixel(x, 27);
        if (x == 68) continue; /* White target marker overlays the ramp. */
        unsigned r = color >> 11, g = (color >> 5) & 63, b = color & 31;
        assert(r >= 10 && r <= 26 && g >= previous_green && g <= 43);
        assert(b >= 7 && b <= 10);
        previous_green = g;
    }
    render_rate(0);
    assert(pixel(9, 27) == 0x18c3); /* Zero remains a valid, empty reading. */
#ifdef FPS_OVERLAY_TEST_CAPTURE
    render_rate(275);
    FILE *capture = fopen(FPS_OVERLAY_TEST_CAPTURE, "wb");
    assert(capture);
    assert(fwrite(frame, 1, sizeof(frame), capture) == sizeof(frame));
    assert(fclose(capture) == 0);
#endif
    puts("PASS: FPS gauge colors, target, fill/clamping, smoothing, timer wrap and bounds");
    return 0;
}

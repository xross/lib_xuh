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
    assert(pixel(9, 20) == 0x18c3); /* Empty track, placeholder already visible. */
    assert(pixel(68, 20) == 0xffff); /* 25-FPS target. */
    for (unsigned i = 1; i <= 13; ++i)
        fps_overlay_frame(&state, start + i * 4000000u, frame);
    assert(state.measured && state.tenths == 250); /* 25 FPS over 0.52 seconds. */
    assert(pixel(8, 20) == 0xca67); /* The left end stays red at 25 FPS. */
    unsigned white = 0, red = 0;
    for (unsigned y = 0; y < PIPELINE_LCD_HEIGHT; ++y) {
        for (unsigned x = 0; x < PIPELINE_LCD_WIDTH; ++x) {
            unsigned color = pixel(x, y);
            if (x >= FPS_OVERLAY_X && x < FPS_OVERLAY_X + FPS_OVERLAY_WIDTH &&
                y >= FPS_OVERLAY_Y && y < FPS_OVERLAY_Y + FPS_OVERLAY_HEIGHT) {
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

static void check_source_rates(void)
{
    static const unsigned targets[] = {0, 240, 250, 300, 500, 599, 600, 1};
    static const unsigned columns[] = {60, 57, 60, 72, 95, 95, 95, 0};
    for (unsigned i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i) {
        fps_overlay_t state = {.started = 1, .measured = 1, .tenths = 1000,
                               .target_tenths = targets[i]};
        memset(frame, 0x55, sizeof(frame));
        fps_overlay_frame(&state, 1, frame);
        assert(state.tenths == 1000); /* Source metadata is not achieved FPS. */
        assert(pixel(8 + columns[i], 20) == 0xffff);
        unsigned white = 0;
        for (unsigned x = 8; x < 104; ++x) white += pixel(x, 20) == 0xffff;
        assert(white == 1);
        assert(pixel(104, 20) == 0x4208); /* High-rate tick stays inside the bar. */
        for (unsigned y = 0; y < PIPELINE_LCD_HEIGHT; ++y)
            for (unsigned x = 0; x < PIPELINE_LCD_WIDTH; ++x)
                if (x < FPS_OVERLAY_X || x >= FPS_OVERLAY_X + FPS_OVERLAY_WIDTH ||
                    y < FPS_OVERLAY_Y || y >= FPS_OVERLAY_Y + FPS_OVERLAY_HEIGHT)
                    assert(pixel(x, y) == 0x5555);
    }
}

int main(void)
{
    assert(FPS_OVERLAY_X == 4 && FPS_OVERLAY_Y == 4);
    assert(FPS_OVERLAY_WIDTH == 104);
    assert(FPS_OVERLAY_Y + FPS_OVERLAY_HEIGHT == 26); /* Four rows before y=30 video. */
    check_window(0);
    check_window(UINT32_MAX - 10000000u);
    check_source_rates();
    render_rate(25);
    assert(pixel(8, 20) == 0xca67);
    unsigned low = pixel(9, 20);
    assert(pixel(14, 20) == 0x18c3); /* Fill is only 6 of 96 pixels. */
    render_rate(125);
    assert(pixel(9, 20) == low); /* The existing fill does not change color. */
    render_rate(250);
    assert(pixel(8, 20) == 0xca67 && pixel(68, 20) == 0xffff);
    assert(pixel(38, 20) == 0xd4e7); /* Fixed amber midpoint. */
    unsigned target_color = pixel(67, 20);
    assert(((target_color >> 5) & 63) > 3 * (target_color >> 11));
    assert(pixel(69, 20) == 0x18c3);
    render_rate(1000);
    assert(pixel(8, 20) == 0xca67 && pixel(9, 20) == low);
    assert(pixel(69, 20) == 0x556a && pixel(103, 20) == 0x556a);
    assert(pixel(104, 20) == 0x4208); /* Above-scale fill never overruns. */
    unsigned previous_green = 0;
    for (unsigned x = 8; x < 104; ++x) {
        unsigned color = pixel(x, 20);
        if (x == 68) continue; /* White target marker overlays the ramp. */
        unsigned r = color >> 11, g = (color >> 5) & 63, b = color & 31;
        assert(r >= 10 && r <= 26 && g >= previous_green && g <= 43);
        assert(b >= 7 && b <= 10);
        previous_green = g;
    }
    render_rate(0);
    assert(pixel(9, 20) == 0x18c3); /* Zero remains a valid, empty reading. */
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

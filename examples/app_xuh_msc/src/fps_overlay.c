#include "fps_overlay.h"
#include "jpeg_pipeline.h"

#if MSC_VIDEO_PIPELINE && APP_XUH_MSC_FPS_OVERLAY
enum { GLYPH_WIDTH = 5, GLYPH_HEIGHT = 7, SCALE = 2, MARGIN = 2, ORIGIN_X = 4, ORIGIN_Y = 4,
       CELL = (GLYPH_WIDTH + 1) * SCALE, TEXT_LENGTH = 8,
       GLYPH_DOT = 10, GLYPH_F, GLYPH_P, GLYPH_S, GLYPH_SPACE,
       WINDOW_TICKS = PIPELINE_TICKS_PER_SECOND / 2 };

/* 5 x 7 glyphs, one row per byte, most significant of the low five bits left. */
static const unsigned char glyphs[][GLYPH_HEIGHT] = {
    {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}, {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e},
    {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f}, {0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e},
    {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}, {0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e},
    {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e}, {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e}, {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c}, {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10},
    {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10}, {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

static void draw(unsigned char *frame, unsigned tenths)
{
    if (tenths > 999) tenths = 999;
    const unsigned char text[TEXT_LENGTH] = {
        tenths >= 100 ? tenths / 100 : GLYPH_SPACE, tenths / 10 % 10, GLYPH_DOT, tenths % 10,
        GLYPH_SPACE, GLYPH_F, GLYPH_P, GLYPH_S };
    /* White text on a black box; both colours have equal RGB565 bytes. */
    unsigned box_width = TEXT_LENGTH * CELL + 2 * MARGIN;
    unsigned box_height = GLYPH_HEIGHT * SCALE + 2 * MARGIN;
    for (unsigned y = 0; y < box_height; ++y) {
        unsigned char *row = frame + ((ORIGIN_Y + y) * PIPELINE_LCD_WIDTH + ORIGIN_X) * 2;
        unsigned glyph_y = (y - MARGIN) / SCALE;    /* Wraps to a large value in the margin. */
        for (unsigned x = 0; x < box_width; ++x) {
            unsigned text_x = x - MARGIN, lit = 0;  /* As above for the left margin. */
            if (glyph_y < GLYPH_HEIGHT && text_x < TEXT_LENGTH * CELL) {
                unsigned column = text_x % CELL / SCALE;
                lit = column < GLYPH_WIDTH &&
                      ((glyphs[text[text_x / CELL]][glyph_y] >> (GLYPH_WIDTH - 1 - column)) & 1);
            }
            row[2 * x] = row[2 * x + 1] = lit ? 0xff : 0x00;
        }
    }
}

void fps_overlay_frame(fps_overlay_t *state, unsigned now, unsigned char *frame)
{
    if (!state->started) {
        state->started = 1;
        state->window_start = now;
        state->frames = 0;
    } else {
        ++state->frames;
        unsigned elapsed = now - state->window_start;   /* Unsigned: wrap safe. */
        if (elapsed >= WINDOW_TICKS) {
            state->tenths = (unsigned)((unsigned long long)state->frames *
                                       PIPELINE_TICKS_PER_SECOND * 10 / elapsed);
            state->window_start = now;
            state->frames = 0;
        }
    }
    if (state->tenths) draw(frame, state->tenths);
}
#endif

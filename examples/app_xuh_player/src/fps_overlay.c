#include "fps_overlay.h"
#include "video_config.h"

#if MSC_VIDEO_PIPELINE && APP_XUH_PLAYER_FPS_OVERLAY
enum { GLYPH_WIDTH = 5, GLYPH_HEIGHT = 7, SCALE = 2, ORIGIN_X = 4, ORIGIN_Y = 4,
        CELL = (GLYPH_WIDTH + 1) * SCALE, TEXT_LENGTH = 8,
        GLYPH_DOT = 10, GLYPH_F, GLYPH_P, GLYPH_S, GLYPH_SPACE, GLYPH_DASH,
        TEXT_X = 4, TEXT_Y = 3,
        BAR_X = TEXT_X, BAR_Y = 22, BAR_WIDTH = TEXT_LENGTH * CELL, BAR_HEIGHT = 8,
        BOX_WIDTH = BAR_WIDTH + 2 * BAR_X, BOX_HEIGHT = 34,
        TARGET_TENTHS = 250, MAX_TENTHS = 400,
        BACKGROUND = 0x0841, BORDER = 0x4208, TRACK = 0x18c3, LABEL = 0xd6ba,
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
    {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00},
};

static void fill(unsigned char *frame, unsigned x, unsigned y,
                 unsigned width, unsigned height, unsigned color)
{
    for (unsigned row = 0; row < height; ++row) {
        unsigned char *pixel = frame + ((ORIGIN_Y + y + row) * PIPELINE_LCD_WIDTH + ORIGIN_X + x) * 2;
        for (unsigned column = 0; column < width; ++column, pixel += 2) {
            /* The LCD consumes big-endian RGB565, not native-endian words. */
            pixel[0] = (unsigned char)(color >> 8);
            pixel[1] = (unsigned char)color;
        }
    }
}

static unsigned gradient_color(unsigned column)
{
    /* Fixed spatial ramp: richer brick, amber and leaf green, reaching green
     * at the 25-FPS target and retaining it above that. No neon primaries.
     * Revealing more of the bar never recolours its red left edge. */
    unsigned target = TARGET_TENTHS * BAR_WIDTH / MAX_TENTHS;
    if (column >= target) return (10u << 11) | (43u << 5) | 10u;
    unsigned middle = target / 2;
    unsigned first = column <= middle;
    unsigned position = first ? column : column - middle;
    unsigned span = first ? middle : target - middle;
    unsigned red_a = first ? 25 : 26, red_b = first ? 26 : 10;
    unsigned green_a = first ? 19 : 39, green_b = first ? 39 : 43;
    unsigned blue_a = 7, blue_b = first ? 7 : 10;
    unsigned red = (red_a * (span - position) + red_b * position + span / 2) / span;
    unsigned green = (green_a * (span - position) + green_b * position + span / 2) / span;
    unsigned blue = (blue_a * (span - position) + blue_b * position + span / 2) / span;
    return (red << 11) | (green << 5) | blue;
}

static void draw(unsigned char *frame, unsigned tenths, unsigned measured)
{
    unsigned capped = tenths > MAX_TENTHS ? MAX_TENTHS : tenths;
    unsigned filled = measured ? capped * BAR_WIDTH / MAX_TENTHS : 0;
    if (tenths > 999) tenths = 999;
    unsigned char text[TEXT_LENGTH] = {
        tenths >= 100 ? tenths / 100 : GLYPH_SPACE, tenths / 10 % 10, GLYPH_DOT, tenths % 10,
        GLYPH_SPACE, GLYPH_F, GLYPH_P, GLYPH_S };
    if (!measured) text[0] = text[1] = text[3] = GLYPH_DASH;

    fill(frame, 0, 0, BOX_WIDTH, BOX_HEIGHT, BACKGROUND);
    fill(frame, 1, 0, BOX_WIDTH - 2, 1, BORDER);
    fill(frame, 1, BOX_HEIGHT - 1, BOX_WIDTH - 2, 1, BORDER);
    fill(frame, 0, 1, 1, BOX_HEIGHT - 2, BORDER);
    fill(frame, BOX_WIDTH - 1, 1, 1, BOX_HEIGHT - 2, BORDER);
    for (unsigned character = 0; character < TEXT_LENGTH; ++character) {
        for (unsigned y = 0; y < GLYPH_HEIGHT; ++y) {
            for (unsigned x = 0; x < GLYPH_WIDTH; ++x) {
                if ((glyphs[text[character]][y] >> (GLYPH_WIDTH - 1 - x)) & 1)
                    fill(frame, TEXT_X + character * CELL + x * SCALE, TEXT_Y + y * SCALE,
                         SCALE, SCALE, measured || character >= 4 ? LABEL : BORDER);
            }
        }
    }
    fill(frame, BAR_X - 1, BAR_Y - 1, BAR_WIDTH + 2, BAR_HEIGHT + 2, BORDER);
    fill(frame, BAR_X, BAR_Y, BAR_WIDTH, BAR_HEIGHT, TRACK);
    for (unsigned x = 0; x < filled; ++x)
        fill(frame, BAR_X + x, BAR_Y, 1, BAR_HEIGHT, gradient_color(x));
    /* Fixed target marker remains visible both ahead of and inside the fill. */
    unsigned target_x = BAR_X + TARGET_TENTHS * BAR_WIDTH / MAX_TENTHS;
    fill(frame, target_x, BAR_Y - 2, 1, BAR_HEIGHT + 4, 0xffff);
    fill(frame, target_x - 1, BAR_Y - 3, 3, 1, 0xffff);
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
            unsigned measured = (unsigned)((unsigned long long)state->frames *
                                           PIPELINE_TICKS_PER_SECOND * 10 / elapsed);
            state->tenths = state->measured ? (state->tenths + measured + 1) / 2 : measured;
            state->measured = 1;
            state->window_start = now;
            state->frames = 0;
        }
    }
    draw(frame, state->tenths, state->measured);
}
#endif

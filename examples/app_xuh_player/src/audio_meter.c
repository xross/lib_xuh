#include "audio_meter.h"

#if AUDIO_METER_ENABLED
enum { GLYPH_WIDTH = 5, GLYPH_HEIGHT = 7,
       BAR_X = 4, BAR_WIDTH = AUDIO_METER_BAR_WIDTH, BAR_HEIGHT = 5,
       LABEL_X = BAR_X + BAR_WIDTH + 3, ROW_Y = 2, ROW_PITCH = 10,
       /* Colours of the FPS gauge. */
       BACKGROUND = 0x0841, BORDER = 0x4208, TRACK = 0x18c3, LABEL = 0xd6ba,
       AMBER_AT = 60,           /* Pixels from the quiet end: about -15 dB. */
       PIXELS_PER_OCTAVE = 11, FLOOR_BIT = 8, FULL_SCALE_BIT = 16,
       RELEASE = 2,             /* Pixels a bar falls per frame. */
       HOLD_FRAMES = 30 };

_Static_assert((FULL_SCALE_BIT - FLOOR_BIT) * PIXELS_PER_OCTAVE == AUDIO_METER_BAR_WIDTH,
               "the scale spans the bar");
_Static_assert(LABEL_X + GLYPH_WIDTH + 4 == AUDIO_METER_WIDTH, "labels fit the box");

/* 5 x 7 glyphs L and R, one row per byte, most significant of five bits left. */
static const unsigned char glyphs[2][GLYPH_HEIGHT] = {
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f},
    {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11},
};

/* Latest levels, left in bits 15..8 and right in bits 7..0. One writer (the
 * audio task) and one reader (the display task), both on this tile. */
static volatile unsigned published;

void audio_meter_publish(unsigned left, unsigned right)
{
    published = (left << 8) | right;
}

unsigned audio_meter_level(float peak)
{
    if (!(peak > 0)) return 0;
    if (peak >= 1) return AUDIO_METER_BAR_WIDTH;
    unsigned value = (unsigned)(peak * (1u << FULL_SCALE_BIT));
    if (value < (1u << FLOOR_BIT)) return 0;
    /* Whole octaves from the top set bit; a straight line within the octave. */
    unsigned bit = FULL_SCALE_BIT - 1;
    while (!(value >> bit)) --bit;
    return (bit - FLOOR_BIT) * PIXELS_PER_OCTAVE +
           (((value - (1u << bit)) * PIXELS_PER_OCTAVE) >> bit);
}

static void fill(unsigned char *frame, unsigned x, unsigned y,
                 unsigned width, unsigned height, unsigned color)
{
    for (unsigned row = 0; row < height; ++row) {
        unsigned char *pixel = frame +
            ((AUDIO_METER_Y + y + row) * PIPELINE_LCD_WIDTH + AUDIO_METER_X + x) * 2;
        for (unsigned column = 0; column < width; ++column, pixel += 2) {
            /* The LCD consumes big-endian RGB565, not native-endian words. */
            pixel[0] = (unsigned char)(color >> 8);
            pixel[1] = (unsigned char)color;
        }
    }
}

/* Fixed ramp by distance from the quiet, right-hand end: the FPS gauge's leaf
 * green, amber and brick red. A longer bar never recolours what it shows. */
static unsigned gradient_color(unsigned position)
{
    unsigned first = position < AMBER_AT;
    unsigned offset = first ? position : position - AMBER_AT;
    unsigned span = first ? AMBER_AT : BAR_WIDTH - 1 - AMBER_AT;
    unsigned red_a = first ? 10 : 26, red_b = first ? 26 : 25;
    unsigned green_a = first ? 43 : 39, green_b = first ? 39 : 19;
    unsigned blue_a = first ? 10 : 7, blue_b = 7;
    unsigned red = (red_a * (span - offset) + red_b * offset + span / 2) / span;
    unsigned green = (green_a * (span - offset) + green_b * offset + span / 2) / span;
    unsigned blue = (blue_a * (span - offset) + blue_b * offset + span / 2) / span;
    return (red << 11) | (green << 5) | blue;
}

static void draw(unsigned char *frame, const audio_meter_t *state)
{
    fill(frame, 0, 0, AUDIO_METER_WIDTH, AUDIO_METER_HEIGHT, BACKGROUND);
    fill(frame, 1, 0, AUDIO_METER_WIDTH - 2, 1, BORDER);
    fill(frame, 1, AUDIO_METER_HEIGHT - 1, AUDIO_METER_WIDTH - 2, 1, BORDER);
    fill(frame, 0, 1, 1, AUDIO_METER_HEIGHT - 2, BORDER);
    fill(frame, AUDIO_METER_WIDTH - 1, 1, 1, AUDIO_METER_HEIGHT - 2, BORDER);
    for (unsigned channel = 0; channel < 2; ++channel) {
        unsigned row = ROW_Y + channel * ROW_PITCH, bar_y = row + 1;
        for (unsigned y = 0; y < GLYPH_HEIGHT; ++y)
            for (unsigned x = 0; x < GLYPH_WIDTH; ++x)
                if ((glyphs[channel][y] >> (GLYPH_WIDTH - 1 - x)) & 1)
                    fill(frame, LABEL_X + x, row + y, 1, 1, LABEL);
        fill(frame, BAR_X - 1, bar_y - 1, BAR_WIDTH + 2, BAR_HEIGHT + 2, BORDER);
        fill(frame, BAR_X, bar_y, BAR_WIDTH, BAR_HEIGHT, TRACK);
        /* Position 0 is the right-hand end; the bar fills towards the left. */
        for (unsigned position = 0; position < state->shown[channel]; ++position)
            fill(frame, BAR_X + BAR_WIDTH - 1 - position, bar_y, 1, BAR_HEIGHT,
                 gradient_color(position));
        if (state->hold[channel])
            fill(frame, BAR_X + BAR_WIDTH - state->hold[channel], bar_y, 1, BAR_HEIGHT, 0xffff);
    }
}

void audio_meter_frame(audio_meter_t *state, unsigned char *frame)
{
    unsigned levels = published;
    for (unsigned channel = 0; channel < 2; ++channel) {
        unsigned level = (levels >> (channel ? 0 : 8)) & 0xff;
        if (level > BAR_WIDTH) level = BAR_WIDTH;
        unsigned fallen = state->shown[channel] > RELEASE ? state->shown[channel] - RELEASE : 0;
        state->shown[channel] = level > fallen ? level : fallen;
        if (state->shown[channel] >= state->hold[channel]) {
            state->hold[channel] = state->shown[channel];
            state->hold_age[channel] = 0;
        } else if (++state->hold_age[channel] > HOLD_FRAMES) {
            --state->hold[channel];
        }
    }
    draw(frame, state);
}
#endif

#include "decode_lead.h"

#if DECODE_LEAD_ENABLED
enum { VALID = 0x80000000u, TIME_MASK = 0x7fffffffu,
       GLYPH_WIDTH = 5, GLYPH_HEIGHT = 7, CELL = 6, TEXT_LENGTH = 12,
       SPACE = 10, D, E, C, PLUS, MINUS, GREATER, M, S,
       TEXT_X = (DECODE_LEAD_WIDTH - TEXT_LENGTH * CELL) / 2,
       BAR_X = 5, BAR_Y = 15, BAR_HALF = 45, CENTRE = BAR_X + BAR_HALF,
       BACKGROUND = 0x0841, BORDER = 0x4208, TRACK = 0x18c3, LABEL = 0xd6ba,
       GREEN = (10u << 11) | (43u << 5) | 10u,
       AMBER = (26u << 11) | (39u << 5) | 7u,
       RED = (25u << 11) | (19u << 5) | 7u };

/* Same single-word, tile-local publication as the stereo peak meter. No
 * multiword snapshot, retry loop, channel traffic or waiting in either task. */
static volatile unsigned published;

static const unsigned char glyphs[][GLYPH_HEIGHT] = {
    {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}, {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e},
    {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f}, {0x1f, 0x02, 0x04, 0x04, 0x01, 0x11, 0x0e},
    {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}, {0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e},
    {0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e}, {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e}, {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e},
    {0, 0, 0, 0, 0, 0, 0},                       /* space */
    {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e}, /* D */
    {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}, /* E */
    {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e}, /* C */
    {0, 0x04, 0x04, 0x1f, 0x04, 0x04, 0},       /* + */
    {0, 0, 0, 0x1f, 0, 0, 0},                   /* - */
    {0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10}, /* > */
    {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11}, /* M */
    {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}, /* S */
};

void decode_lead_advance(decode_lead_clock_t *state, unsigned units, unsigned rate)
{
    if (state->rate != rate) state->remainder = 0;
    state->rate = rate;
    unsigned numerator = units * 1000 + state->remainder;
    state->ms += numerator / rate;
    state->remainder = numerator % rate;
}

void decode_lead_publish(unsigned ms, unsigned valid)
{
    published = valid ? VALID | (ms & TIME_MASK) : 0;
}

unsigned decode_lead_read(unsigned *ms)
{
    unsigned snapshot = published;
    *ms = snapshot & TIME_MASK;
    return (snapshot & VALID) != 0;
}

int decode_lead_difference(unsigned audio_ms, unsigned video_ms)
{
    unsigned delta = (audio_ms - video_ms) & TIME_MASK;
    return delta < 0x40000000u ? (int)delta : -(int)(0x80000000u - delta);
}

static void fill(unsigned char *pixels, unsigned x, unsigned y,
                 unsigned width, unsigned height, unsigned color)
{
    for (unsigned row = 0; row < height; ++row) {
        unsigned char *pixel = pixels + ((y + row) * DECODE_LEAD_WIDTH + x) * 2;
        for (unsigned column = 0; column < width; ++column, pixel += 2) {
            pixel[0] = (unsigned char)(color >> 8);
            pixel[1] = (unsigned char)color;
        }
    }
}

void decode_lead_draw(unsigned char *pixels, unsigned video_ms)
{
    unsigned audio_ms;
    unsigned valid = decode_lead_read(&audio_ms);
    int delta = decode_lead_difference(audio_ms, video_ms);
    unsigned magnitude = delta < 0 ? (unsigned)-delta : (unsigned)delta;
    unsigned number = magnitude > 9999 ? 9999 : magnitude;
    unsigned char text[TEXT_LENGTH] = {
        D, E, C, SPACE, delta < 0 ? MINUS : PLUS, magnitude > 9999 ? GREATER : SPACE,
        number >= 1000 ? number / 1000 : SPACE,
        number >= 100 ? number / 100 % 10 : SPACE,
        number >= 10 ? number / 10 % 10 : SPACE, number % 10, M, S };
    if (!valid) {
        text[4] = text[5] = SPACE;
        text[6] = text[7] = text[8] = text[9] = MINUS;
    }

    fill(pixels, 0, 0, DECODE_LEAD_WIDTH, DECODE_LEAD_HEIGHT, BACKGROUND);
    fill(pixels, 1, 0, DECODE_LEAD_WIDTH - 2, 1, BORDER);
    fill(pixels, 1, DECODE_LEAD_HEIGHT - 1, DECODE_LEAD_WIDTH - 2, 1, BORDER);
    fill(pixels, 0, 1, 1, DECODE_LEAD_HEIGHT - 2, BORDER);
    fill(pixels, DECODE_LEAD_WIDTH - 1, 1, 1, DECODE_LEAD_HEIGHT - 2, BORDER);
    for (unsigned character = 0; character < TEXT_LENGTH; ++character)
        for (unsigned y = 0; y < GLYPH_HEIGHT; ++y)
            for (unsigned x = 0; x < GLYPH_WIDTH; ++x)
                if ((glyphs[text[character]][y] >> (GLYPH_WIDTH - 1 - x)) & 1)
                    fill(pixels, TEXT_X + character * CELL + x, 2 + y, 1, 1, LABEL);

    fill(pixels, BAR_X - 1, BAR_Y - 1, BAR_HALF * 2 + 3, 4, BORDER);
    fill(pixels, BAR_X, BAR_Y, BAR_HALF * 2 + 1, 2, TRACK);
    fill(pixels, CENTRE, BAR_Y - 2, 1, 6, LABEL);
    if (valid) {
        unsigned capped = magnitude > DECODE_LEAD_RANGE_MS ? DECODE_LEAD_RANGE_MS : magnitude;
        unsigned offset = capped * BAR_HALF / DECODE_LEAD_RANGE_MS;
        unsigned position = delta < 0 ? CENTRE - offset : CENTRE + offset;
        unsigned color = magnitude <= 40 ? GREEN : magnitude <= 100 ? AMBER : RED;
        fill(pixels, delta < 0 ? position : CENTRE, BAR_Y, offset + 1, 2, color);
        fill(pixels, CENTRE, BAR_Y - 2, 1, 6, LABEL);
        fill(pixels, position, BAR_Y - 2, 1, 6, 0xffff);
    }
}
#endif

#include "decode_lead.h"

#if DECODE_LEAD_ENABLED
#include <stdio.h>
enum { VALID = 0x80000000u, TIME_MASK = 0x7fffffffu,
       GLYPH_WIDTH = 5, GLYPH_HEIGHT = 7, CELL = 6, TEXT_LENGTH = 12,
        SPACE = 10, D, E, C, PLUS, MINUS, GREATER, M, S, U, H, COLON,
       TEXT_X = (DECODE_LEAD_WIDTH - TEXT_LENGTH * CELL) / 2,
       BAR_X = 5, BAR_HALF = 45, CENTRE = BAR_X + BAR_HALF,
       /* With the footer, the bar moves up three rows to make room for it. */
       BAR_Y = APP_XUH_MSC_PLAYER_AUDIO_PACED ? 12 : 15, FOOTER_Y = 17,
       BACKGROUND = 0x0841, BORDER = 0x4208, TRACK = 0x18c3, LABEL = 0xd6ba,
       GREEN = (10u << 11) | (43u << 5) | 10u,
       AMBER = (26u << 11) | (39u << 5) | 7u,
       RED = (25u << 11) | (19u << 5) | 7u };

/* Same single-word, tile-local publication as the stereo peak meter. No
 * multiword snapshot, retry loop, channel traffic or waiting in either task. */
static volatile unsigned published;
static volatile unsigned published_underflows, published_held_ms;

/* LCD task only: the leads drawn since the last report. */
static struct { long long sum; int least, greatest; unsigned count, green, amber, red; } summary;
static struct { int value; unsigned age, valid; } peak;

static unsigned band_color(unsigned magnitude)
{
    return magnitude <= DECODE_LEAD_AMBER_MS ? GREEN : magnitude <= DECODE_LEAD_RED_MS ? AMBER : RED;
}

static unsigned bar_position(int delta)
{
    unsigned magnitude = delta < 0 ? (unsigned)-delta : (unsigned)delta;
    unsigned capped = magnitude > DECODE_LEAD_RANGE_MS ? DECODE_LEAD_RANGE_MS : magnitude;
    unsigned offset = capped * BAR_HALF / DECODE_LEAD_RANGE_MS;
    return delta < 0 ? CENTRE - offset : CENTRE + offset;
}

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
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}, /* U */
    {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}, /* H */
    {0, 0x04, 0x04, 0, 0x04, 0x04, 0},         /* : */
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

void decode_lead_underflows_publish(unsigned count, unsigned held_ms)
{
    published_held_ms = held_ms;
    published_underflows = count;
}

void decode_lead_underflows_read(unsigned *count, unsigned *held_ms)
{
    *count = published_underflows;
    *held_ms = published_held_ms;
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
    } else {
        if (!summary.count || delta < summary.least) summary.least = delta;
        if (!summary.count || delta > summary.greatest) summary.greatest = delta;
        summary.sum += delta;
        ++summary.count;
        if (magnitude <= DECODE_LEAD_AMBER_MS) ++summary.green;
        else if (magnitude <= DECODE_LEAD_RED_MS) ++summary.amber;
        else ++summary.red;
        unsigned held = peak.value < 0 ? (unsigned)-peak.value : (unsigned)peak.value;
        if (!peak.valid || magnitude >= held || ++peak.age >= DECODE_LEAD_PEAK_PICTURES) {
            peak.value = delta;
            peak.age = 0;
            peak.valid = 1;
        }
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
    /* Marks under the bar where it turns amber and red, either side. */
    for (unsigned side = 0; side < 2; ++side) {
        int sign = side ? -1 : 1;
        fill(pixels, bar_position(sign * (DECODE_LEAD_AMBER_MS + 1)), BAR_Y + 3, 1, 2, AMBER);
        fill(pixels, bar_position(sign * (DECODE_LEAD_RED_MS + 1)), BAR_Y + 3, 1, 2, RED);
    }
    if (valid) {
        unsigned position = bar_position(delta);
        unsigned offset = delta < 0 ? CENTRE - position : position - CENTRE;
        fill(pixels, delta < 0 ? position : CENTRE, BAR_Y, offset + 1, 2, band_color(magnitude));
        fill(pixels, CENTRE, BAR_Y - 2, 1, 6, LABEL);
        /* The largest recent reading, in its band's colour, under the live tick. */
        unsigned held = peak.value < 0 ? (unsigned)-peak.value : (unsigned)peak.value;
        fill(pixels, bar_position(peak.value), BAR_Y - 2, 1, 6, band_color(held));
        fill(pixels, position, BAR_Y - 2, 1, 6, 0xffff);
    }
#if APP_XUH_MSC_PLAYER_AUDIO_PACED
    /* Fifteen cells fit inside the 100-pixel rectangle. Overflow is explicit,
     * while terminal reports retain the full values: U:003 H:00120MS. */
    unsigned underflows, held_ms;
    decode_lead_underflows_read(&underflows, &held_ms);
    unsigned count = underflows > 999 ? 999 : underflows;
    unsigned held = held_ms > 99999 ? 99999 : held_ms;
    unsigned char footer[] = {U, underflows > 999 ? GREATER : COLON,
        count / 100, count / 10 % 10, count % 10, SPACE,
        H, held_ms > 99999 ? GREATER : COLON,
        held / 10000, held / 1000 % 10, held / 100 % 10, held / 10 % 10, held % 10, M, S};
    for (unsigned character = 0; character < sizeof(footer); ++character)
        for (unsigned y = 0; y < GLYPH_HEIGHT; ++y)
            for (unsigned x = 0; x < GLYPH_WIDTH; ++x)
                if ((glyphs[footer[character]][y] >> (GLYPH_WIDTH - 1 - x)) & 1)
                    fill(pixels, 5 + character * CELL + x, FOOTER_Y + y, 1, 1,
                         underflows ? AMBER : LABEL);
#endif
}

void decode_lead_report(void)
{
    if (summary.count)
        printf("Audio decode lead: mean=%d min=%d max=%d ms over %u pictures; "
               "within %u ms=%u, within %u ms=%u, beyond=%u\n",
               (int)(summary.sum / (long long)summary.count), summary.least, summary.greatest,
               summary.count, DECODE_LEAD_AMBER_MS, summary.green,
               DECODE_LEAD_RED_MS, summary.amber, summary.red);
    summary.sum = 0;
    summary.count = summary.green = summary.amber = summary.red = 0;
    peak.valid = 0;
}
#endif

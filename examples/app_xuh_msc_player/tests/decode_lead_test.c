#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "decode_lead.h"

#if DECODE_LEAD_ENABLED
static unsigned char storage[DECODE_LEAD_WIDTH * DECODE_LEAD_HEIGHT * 2 + 32];
#define PIXELS (storage + 16)
enum { TRACK = 0x18c3, WHITE = 0xffff, LABEL = 0xd6ba,
       CENTRE = 50, BAR_LEFT = 5, BAR_RIGHT = 95, TEXT_X = 14,
       BAR_Y = APP_XUH_MSC_PLAYER_AUDIO_PACED ? 12 : 15, FOOTER_Y = 17 };

static unsigned pixel(unsigned x, unsigned y)
{
    const unsigned char *p = PIXELS + 2 * (y * DECODE_LEAD_WIDTH + x);
    return ((unsigned)p[0] << 8) | p[1];
}

static void guards(void)
{
    for (unsigned i = 0; i < 16; ++i) {
        assert(storage[i] == 0x55);
        assert(storage[sizeof(storage) - 1 - i] == 0x55);
    }
}

static void check_clocks(void)
{
    decode_lead_clock_t audio = {0}, video = {0};
    for (unsigned i = 1; i <= 100000; ++i) {
        decode_lead_advance(&audio, 1152, 44100);
        decode_lead_advance(&video, 1001, 30000);
        assert(audio.ms == (unsigned)((unsigned long long)i * 1152 * 1000 / 44100));
        assert(video.ms == (unsigned)((unsigned long long)i * 1001 * 1000 / 30000));
    }
    /* Rate changes retain whole ms but discard old sub-ms residue. */
    unsigned before = audio.ms;
    decode_lead_advance(&audio, 1152, 48000);
    assert(audio.ms == before + 24 && audio.remainder == 0);
    unsigned rates[][2] = {{24000, 1001}, {24, 1}, {25, 1}, {30, 1}, {50, 1}, {60, 1}};
    for (unsigned r = 0; r < sizeof(rates) / sizeof(rates[0]); ++r) {
        memset(&video, 0, sizeof(video));
        for (unsigned i = 0; i < 60000; ++i)
            decode_lead_advance(&video, rates[r][1], rates[r][0]);
        assert(video.ms == (unsigned)(60000ull * rates[r][1] * 1000 / rates[r][0]));
    }
    video.ms = 0xfffffff0u;
    video.remainder = 0;
    decode_lead_advance(&video, 1, 25);
    assert(video.ms == 24); /* Unsigned clock rollover. */
}

static void check_publication(void)
{
    unsigned ms;
    decode_lead_publish(0, 0);
    assert(!decode_lead_read(&ms));
    unsigned count, held_ms;
    decode_lead_underflows_publish(3, 120);
    decode_lead_underflows_read(&count, &held_ms);
    assert(count == 3 && held_ms == 120);
    decode_lead_underflows_publish(0, 0);
    decode_lead_underflows_read(&count, &held_ms);
    assert(count == 0 && held_ms == 0);
    decode_lead_publish(0, 1);
    assert(decode_lead_read(&ms) && ms == 0); /* Valid zero is not unavailable. */
    decode_lead_publish(72, 1);
    assert(decode_lead_read(&ms) && ms == 72);
    assert(decode_lead_difference(ms, 40) == 32);
    assert(decode_lead_difference(ms, 120) == -48);
    assert(decode_lead_difference(ms, 72) == 0);
    decode_lead_publish(0x80000018u, 1);
    assert(decode_lead_read(&ms) && ms == 24);
    assert(decode_lead_difference(ms, 0x7ffffff0u) == 40);
    assert(decode_lead_difference(0x7ffffff0u, ms) == -40);
    assert(decode_lead_difference(0xfffffff0u, 24) == -40);
    decode_lead_publish(999, 0); /* Restart/error invalidates stale progress. */
    assert(!decode_lead_read(&ms));
}

static void check_drawing(void)
{
    memset(storage, 0x55, sizeof(storage));
    decode_lead_publish(0, 0);
    decode_lead_draw(PIXELS, 40);
    assert(pixel(CENTRE, BAR_Y) == LABEL);
    assert(pixel(BAR_LEFT, BAR_Y) == TRACK && pixel(BAR_RIGHT, BAR_Y) == TRACK);
    guards();

    decode_lead_publish(DECODE_LEAD_RANGE_MS, 1);
    decode_lead_draw(PIXELS, DECODE_LEAD_RANGE_MS);
    assert(pixel(CENTRE, BAR_Y) == WHITE);
    /* Plus glyph's vertical stroke; absent in the minus glyph. */
    assert(pixel(TEXT_X + 4 * 6 + 2, 3) == LABEL);
    decode_lead_draw(PIXELS, 2 * DECODE_LEAD_RANGE_MS);
    assert(pixel(BAR_LEFT, BAR_Y) == WHITE && pixel(BAR_RIGHT, BAR_Y) == TRACK);
    assert(pixel(TEXT_X + 4 * 6 + 2, 3) != LABEL);
    decode_lead_draw(PIXELS, 0);
    assert(pixel(BAR_RIGHT, BAR_Y) == WHITE && pixel(BAR_LEFT, BAR_Y) == TRACK);
    decode_lead_draw(PIXELS, DECODE_LEAD_RANGE_MS / 2);
    assert(pixel(CENTRE + 22, BAR_Y) == WHITE); /* Positive is to the right. */
    decode_lead_draw(PIXELS, DECODE_LEAD_RANGE_MS + DECODE_LEAD_RANGE_MS / 2);
    assert(pixel(CENTRE - 22, BAR_Y) == WHITE);
    unsigned magnitudes[] = {DECODE_LEAD_AMBER_MS, DECODE_LEAD_AMBER_MS + 1,
                             DECODE_LEAD_RED_MS, DECODE_LEAD_RED_MS + 1};
    unsigned colors[] = {(10u << 11) | (43u << 5) | 10u,
                         (26u << 11) | (39u << 5) | 7u,
                         (26u << 11) | (39u << 5) | 7u,
                         (25u << 11) | (19u << 5) | 7u};
    for (unsigned i = 0; i < 4; ++i) {
        decode_lead_draw(PIXELS, DECODE_LEAD_RANGE_MS - magnitudes[i]);
        unsigned offset = magnitudes[i] * (BAR_RIGHT - CENTRE) / DECODE_LEAD_RANGE_MS;
        assert(pixel(CENTRE + offset, BAR_Y) == WHITE);
        /* At small offsets the centre and position ticks cover all the fill. */
        if (offset > 1) assert(pixel(CENTRE + 1, BAR_Y) == colors[i]);
    }
    for (unsigned i = 0; i < 2; ++i) {
        decode_lead_publish(i ? 20000 : 0, 1);
        decode_lead_draw(PIXELS, i ? 0 : 20000);
        assert(pixel(i ? BAR_RIGHT : BAR_LEFT, BAR_Y) == WHITE);
        /* The explicit > magnitude marker distinguishes overflow from 9999. */
        assert(pixel(TEXT_X + 5 * 6, 2) == LABEL);
        guards();
    }
    assert(DECODE_LEAD_X > 107 && DECODE_LEAD_X + DECODE_LEAD_WIDTH <= 212);
    assert(DECODE_LEAD_Y == 4);
#if APP_XUH_MSC_PLAYER_AUDIO_PACED
    assert(DECODE_LEAD_HEIGHT == 26);
    /* The gauge must end above a 180-row picture centred on 240 rows. */
    assert(DECODE_LEAD_Y + DECODE_LEAD_HEIGHT <= (240 - 180) / 2);
    decode_lead_underflows_publish(3, 120);
    decode_lead_draw(PIXELS, 0);
    /* U and H have outer strokes; normal colon has a central stroke. */
    assert(pixel(5, FOOTER_Y) != 0x0841);
    assert(pixel(5 + 6 * 6, FOOTER_Y) != 0x0841);
    decode_lead_underflows_publish(1000, 100000);
    decode_lead_draw(PIXELS, 0);
    assert(pixel(5 + 6, FOOTER_Y) == (unsigned)((26u << 11) | (39u << 5) | 7u));
    assert(pixel(5 + 7 * 6, FOOTER_Y) == (unsigned)((26u << 11) | (39u << 5) | 7u));
#else
    assert(DECODE_LEAD_HEIGHT == 22);
#endif
    /* Peak hold: a tick in its band's colour stays at the largest reading for
     * DECODE_LEAD_PEAK_PICTURES pictures, then follows the live reading. */
    decode_lead_report();
    unsigned large = DECODE_LEAD_RED_MS + 10;
    unsigned at = CENTRE + large * (BAR_RIGHT - CENTRE) / DECODE_LEAD_RANGE_MS;
    decode_lead_publish(large, 1);
    decode_lead_draw(PIXELS, 0);
    assert(pixel(at, BAR_Y - 2) == WHITE);
    decode_lead_publish(0, 1);
    for (unsigned i = 1; i < DECODE_LEAD_PEAK_PICTURES; ++i) {
        decode_lead_draw(PIXELS, 0);
        assert(pixel(at, BAR_Y - 2) == (unsigned)((25u << 11) | (19u << 5) | 7u));
    }
    decode_lead_draw(PIXELS, 0);
    assert(pixel(at, BAR_Y - 2) != (unsigned)((25u << 11) | (19u << 5) | 7u));
    guards();
}
#endif

int main(void)
{
#if DECODE_LEAD_ENABLED
    check_clocks();
    check_publication();
    check_drawing();
    puts("PASS: decode lead clocks, fractional rates, signs, rollover, validity and drawing bounds");
#else
    puts("PASS: decode lead disabled without runtime dependencies");
#endif
    return 0;
}

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "audio_meter.h"

static unsigned char frame[PIPELINE_LCD_WIDTH * PIPELINE_LCD_HEIGHT * 2];

enum { TRACK = 0x18c3, GREEN = (10u << 11) | (43u << 5) | 10u,
       /* One pixel short of the brick-red end, which the peak tick covers. */
       RED = (25u << 11) | (20u << 5) | 7u, WHITE = 0xffff,
       BAR_RIGHT = AUDIO_METER_X + 4 + AUDIO_METER_BAR_WIDTH - 1,  /* Quiet end. */
       BAR_LEFT = AUDIO_METER_X + 4,                               /* Loud end. */
       LEFT_Y = AUDIO_METER_Y + 5, RIGHT_Y = AUDIO_METER_Y + 15 };

static unsigned pixel(unsigned x, unsigned y)
{
    const unsigned char *p = frame + 2 * (y * PIPELINE_LCD_WIDTH + x);
    return ((unsigned)p[0] << 8) | p[1];
}

static void check_levels(void)
{
    assert(audio_meter_level(0.0f) == 0);
    assert(audio_meter_level(-0.5f) == 0);
    assert(audio_meter_level(1.0f) == AUDIO_METER_BAR_WIDTH);
    assert(audio_meter_level(4.0f) == AUDIO_METER_BAR_WIDTH); /* Clamped. */
    assert(audio_meter_level(0.5f) == AUDIO_METER_BAR_WIDTH - 11);     /* -6 dB */
    assert(audio_meter_level(0.25f) == AUDIO_METER_BAR_WIDTH - 22);    /* -12 dB */
    assert(audio_meter_level(1.0f / 256) == 0);                        /* -48 dB */
    assert(audio_meter_level(1.0f / 128) == 11);
    assert(audio_meter_level(0.001f) == 0);
    unsigned previous = 0;
    for (unsigned i = 0; i <= 4096; ++i) {  /* Never decreases, never overruns. */
        unsigned level = audio_meter_level((float)i / 4096);
        assert(level >= previous && level <= AUDIO_METER_BAR_WIDTH);
        previous = level;
    }
}

static void check_drawing(void)
{
    audio_meter_t state = {0};
    memset(frame, 0x55, sizeof(frame));
    audio_meter_publish(0, 0);
    audio_meter_frame(&state, frame);
    assert(pixel(BAR_RIGHT, LEFT_Y) == TRACK && pixel(BAR_RIGHT, RIGHT_Y) == TRACK);
    for (unsigned y = 0; y < PIPELINE_LCD_HEIGHT; ++y)       /* Only its own rectangle. */
        for (unsigned x = 0; x < PIPELINE_LCD_WIDTH; ++x)
            if (x < AUDIO_METER_X || x >= AUDIO_METER_X + AUDIO_METER_WIDTH ||
                y < AUDIO_METER_Y || y >= AUDIO_METER_Y + AUDIO_METER_HEIGHT)
                assert(pixel(x, y) == 0x5555);
    assert(AUDIO_METER_X + AUDIO_METER_WIDTH == PIPELINE_LCD_WIDTH - 4);
    assert(AUDIO_METER_X >= PIPELINE_LCD_WIDTH / 2);         /* Right half only. */

    /* Left at 20 pixels, right silent: the bar grows leftwards from the right. */
    audio_meter_publish(20, 0);
    audio_meter_frame(&state, frame);
    assert(pixel(BAR_RIGHT, LEFT_Y) == GREEN);
    assert(pixel(BAR_RIGHT - 18, LEFT_Y) != TRACK);
    assert(pixel(BAR_RIGHT - 19, LEFT_Y) == WHITE);          /* Peak-hold tick. */
    assert(pixel(BAR_RIGHT - 20, LEFT_Y) == TRACK);
    assert(pixel(BAR_RIGHT, RIGHT_Y) == TRACK);              /* Channels independent. */

    /* Full scale: red at the left end, green still at the right. */
    audio_meter_publish(AUDIO_METER_BAR_WIDTH, AUDIO_METER_BAR_WIDTH);
    audio_meter_frame(&state, frame);
    assert(pixel(BAR_RIGHT, RIGHT_Y) == GREEN);
    assert(pixel(BAR_LEFT + 1, RIGHT_Y) == RED);
    assert(pixel(BAR_LEFT, RIGHT_Y) == WHITE);
    assert(pixel(BAR_LEFT - 1, RIGHT_Y) == 0x4208);          /* Never overruns. */

    /* Silence: two pixels a frame, with the tick held and then falling. */
    audio_meter_publish(0, 0);
    audio_meter_frame(&state, frame);
    assert(state.shown[0] == AUDIO_METER_BAR_WIDTH - 2 && state.hold[0] == AUDIO_METER_BAR_WIDTH);
    for (unsigned i = 0; i < 29; ++i) audio_meter_frame(&state, frame);
    assert(state.hold[0] == AUDIO_METER_BAR_WIDTH);
    audio_meter_frame(&state, frame);
    assert(state.hold[0] == AUDIO_METER_BAR_WIDTH - 1);
    for (unsigned i = 0; i < 200; ++i) audio_meter_frame(&state, frame);
    assert(!state.shown[0] && !state.hold[0] && !state.shown[1] && !state.hold[1]);
    assert(pixel(BAR_LEFT, LEFT_Y) == TRACK && pixel(BAR_RIGHT, LEFT_Y) == TRACK);

    /* An out-of-range published level cannot draw outside the bar. */
    audio_meter_publish(255, 255);
    audio_meter_frame(&state, frame);
    assert(state.shown[0] == AUDIO_METER_BAR_WIDTH && pixel(BAR_LEFT - 1, LEFT_Y) == 0x4208);
}

int main(void)
{
    check_levels();
    check_drawing();
    puts("PASS: audio meter scale, right-to-left bars, colours, release and peak hold");
    return 0;
}

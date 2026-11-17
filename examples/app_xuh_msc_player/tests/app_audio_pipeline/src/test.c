#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcore/hwtimer.h>
#include "audio_pipeline.h"

static unsigned char input[4096];

static void play(chanend channel, unsigned bytes, unsigned fragment, unsigned decode,
                 unsigned stats[AUDIO_STATS_WORDS])
{
    assert(audio_pipeline_start(channel, decode) == 0);
    unsigned clock;
    assert(!audio_pipeline_clock(&clock));
    /* Zero-byte sends must not produce an empty DATA packet. */
    assert(audio_pipeline_send(channel, input, 0) == 0);
    for (unsigned offset = 0; offset < bytes;) {
        unsigned count = bytes - offset;
        if (count > fragment) count = fragment;
        /* Malformed streams latch an error but still acknowledge every send. */
        assert(audio_pipeline_send(channel, input + offset, count) <= 0);
        offset += count;
        if (decode && input[0] == 0xff && offset >= 576)
            assert(audio_pipeline_clock(&clock));
    }
    audio_pipeline_stop(channel, stats);
    assert(stats[AUDIO_BYTES] == bytes);
}

static void starvation(chanend channel, unsigned stats[AUDIO_STATS_WORDS])
{
    hwtimer_t timer = hwtimer_alloc();
    assert(audio_pipeline_start(channel, 1) == 0);
    assert(audio_pipeline_send(channel, input, 576) == 0);
    unsigned before, after;
    assert(audio_pipeline_clock(&before));
    /* More than two block durations, then complete and partially late frames.
     * The sender is deliberately idle; no storage or codec fault is involved. */
    hwtimer_delay(timer, 6000000u);
    int result = audio_pipeline_send(channel, input + 576, 1152);
    if (APP_XUH_MSC_PLAYER_PACING_STRICT) {
        assert(result < 0);
    } else {
        assert(result == 0 && audio_pipeline_clock(&after));
        /* The epoch must not move forward by the 60-ms input gap. Allow the
         * short round-trip sampling error of clock publication across tiles. */
        assert((int)(after - before) < 1000000 && (int)(before - after) < 1000000);
    }
    audio_pipeline_stop(channel, stats);
    assert(stats[AUDIO_UNDERFLOWS] == 1 && stats[AUDIO_HELD_SAMPLES] >= 1152);
    assert(stats[AUDIO_HELD_SAMPLES] == stats[AUDIO_DROPPED_SAMPLES]);
    assert(stats[AUDIO_ERRORS] == 0);
    if (!APP_XUH_MSC_PLAYER_PACING_STRICT) assert(stats[AUDIO_BLOCKS] == 3);
    /* A complete final frame followed by no more input is EOF, not a gap.
     * This also verifies a clean new play after strict failure. */
    assert(audio_pipeline_start(channel, 1) == 0);
    assert(audio_pipeline_send(channel, input, 1728) == 0);
    hwtimer_delay(timer, 10000000u);
    audio_pipeline_stop(channel, stats);
    assert(stats[AUDIO_BLOCKS] == 3 && !stats[AUDIO_ERRORS]);
    assert(!stats[AUDIO_UNDERFLOWS] && !stats[AUDIO_HELD_SAMPLES]);
    hwtimer_free(timer);
}

void test_audio_pipeline(chanend channel)
{
    FILE *file = fopen("short.mp2", "rb");
    assert(file);
    unsigned bytes = (unsigned)fread(input, 1, sizeof(input), file);
    assert(!ferror(file) && feof(file) && fclose(file) == 0);
    /* Three stereo MP2 frames at 48 kHz and 192 kbit/s. */
    assert(bytes == 1728);
    unsigned stats[AUDIO_STATS_WORDS], checksum = 0, peak = 0;
    unsigned fragments[] = {4096, 127, 1};
    for (unsigned i = 0; i < 3; ++i) {
        play(channel, bytes, fragments[i], 1, stats);
        printf("audio case %u: bytes=%u blocks=%u samples=%u rate=%u errors=%u arena=%u checksum=%08x\n",
               i, stats[AUDIO_BYTES], stats[AUDIO_BLOCKS], stats[AUDIO_SAMPLES],
               stats[AUDIO_RATE], stats[AUDIO_ERRORS], stats[AUDIO_PEAK], stats[AUDIO_CHECKSUM]);
        fflush(stdout);
        assert(stats[AUDIO_BLOCKS] == 3 && stats[AUDIO_SAMPLES] == 3456);
        assert(stats[AUDIO_RATE] == 48000 && stats[AUDIO_ERRORS] == 0);
        assert(!stats[AUDIO_UNDERFLOWS] && !stats[AUDIO_HELD_SAMPLES]);
        assert(stats[AUDIO_PEAK] > 0);
        if (!i) {
            checksum = stats[AUDIO_CHECKSUM];
            peak = stats[AUDIO_PEAK];
        } else {
            assert(stats[AUDIO_CHECKSUM] == checksum && stats[AUDIO_PEAK] == peak);
        }
    }
    play(channel, bytes, 4096, 0, stats);
    for (unsigned i = AUDIO_BLOCKS; i < AUDIO_STATS_WORDS; ++i) assert(stats[i] == 0);
    play(channel, 0, 1, 1, stats);
    assert(stats[AUDIO_ERRORS] && !stats[AUDIO_BLOCKS]);
    play(channel, 123, 7, 1, stats);
    assert(stats[AUDIO_ERRORS] && !stats[AUDIO_BLOCKS]);
    unsigned char first = input[0];
    input[0] = 0;
    play(channel, bytes, 127, 1, stats);
    assert(stats[AUDIO_ERRORS] && !stats[AUDIO_BLOCKS]);
    input[0] = first;
    play(channel, bytes, 7, 1, stats);
    assert(stats[AUDIO_BLOCKS] == 3 && stats[AUDIO_ERRORS] == 0);
    assert(stats[AUDIO_CHECKSUM] == checksum && stats[AUDIO_PEAK] == peak);
    starvation(channel, stats);
    puts("PASS: audio pipeline 10 cases; chunking, bypass/errors, continuous clock, underflow/recovery and EOF");
    exit(0);
}

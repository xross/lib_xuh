#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio_pipeline.h"

static unsigned char input[4096];

static void play(chanend channel, unsigned bytes, unsigned fragment, unsigned decode,
                 unsigned stats[AUDIO_STATS_WORDS])
{
    assert(audio_pipeline_start(channel, decode) == 0);
    /* Zero-byte sends must not produce an empty DATA packet. */
    assert(audio_pipeline_send(channel, input, 0) == 0);
    for (unsigned offset = 0; offset < bytes;) {
        unsigned count = bytes - offset;
        if (count > fragment) count = fragment;
        assert(audio_pipeline_send(channel, input + offset, count) == 0);
        offset += count;
    }
    audio_pipeline_stop(channel, stats);
    assert(stats[AUDIO_BYTES] == bytes);
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
    puts("PASS: audio pipeline 8 cases; chunking, decode/bypass, stop/restart and error recovery");
    exit(0);
}

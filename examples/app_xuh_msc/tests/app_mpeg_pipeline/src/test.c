#include <stdio.h>
#include <stdlib.h>
#include "mpeg_pipeline.h"

#define CHECK(condition) do { if (!(condition)) { \
    printf("FAIL: %s:%u: %s\n", __FILE__, __LINE__, #condition); exit(1); } } while (0)

/* Substitute only storage and LCD hardware. Exercise the actual decoder,
 * channels, tile-local buffers, colour conversion and repeated-play protocol. */
void test_loader(chanend c_load)
{
    setbuf(stdout, NULL);
    for (unsigned play = 1; play <= 7; ++play) {
        unsigned error_case = play >= 4 && play <= 6;
        FILE *file = error_case ? NULL : fopen("VIDEO.M1V", "rb");
        CHECK(error_case || file);
        unsigned begin[3] = {play, play == 4, 0};
        pipeline_send(c_load, begin, 3);
        unsigned request[2], stats[MPEG_STATS_WORDS];
        for (;;) {
            pipeline_receive(c_load, request, 2);
            if (request[0] == MPEG_PLAY_COMPLETE) break;
            CHECK(request[0] == MPEG_REQUEST_INPUT && request[1] && request[1] <= MPEG_LOAD_BYTES);
            unsigned bytes = error_case ? 0 : fread(mpeg_pipeline_input_buffer(), 1, request[1], file);
            unsigned response[3] = {bytes, bytes == 0, play == 5};
            CHECK(!file || !ferror(file));
            pipeline_send(c_load, response, 3);
        }
        pipeline_receive(c_load, stats, MPEG_STATS_WORDS);
        if (file) CHECK(!fclose(file));
        CHECK(!stats[MPEG_ARENA_LIVE]);
        if (error_case) {
            CHECK(!stats[PIPELINE_FRAMES] && stats[PIPELINE_ERRORS]);
        } else {
            CHECK(stats[PIPELINE_FRAMES] == MPEG_PIPELINE_TEST_FRAMES &&
                  stats[MPEG_DECODED_FRAMES] == MPEG_PIPELINE_TEST_FRAMES);
            CHECK(!stats[PIPELINE_ERRORS]);
        }
        printf("PASS: pipeline play=%u frames=%u errors=%u live=%u\n", play,
               stats[PIPELINE_FRAMES], stats[PIPELINE_ERRORS], stats[MPEG_ARENA_LIVE]);
    }
    puts("PASS: MPEG pipeline repeat, EOF drain, open/read/empty errors and recovery");
    exit(0);
}

void test_renderer(chanend c_render)
{
    unsigned expected = 0;
    unsigned expected_hash[MPEG_PIPELINE_TEST_FRAMES];
    FILE *hash_file = fopen("HASHES.TXT", "r");
    CHECK(hash_file);
    for (unsigned i = 0; i < MPEG_PIPELINE_TEST_FRAMES; ++i)
        CHECK(fscanf(hash_file, "%x", &expected_hash[i]) == 1);
    CHECK(!fclose(hash_file));
    for (;;) {
        unsigned ready[2];
        pipeline_receive(c_render, ready, 2);
        if (ready[0] == PIPELINE_STOP) { expected = 0; continue; }
        CHECK(ready[0] == (expected & 1) && ready[1] == expected);
        CHECK(expected < MPEG_PIPELINE_TEST_FRAMES);
        CHECK(!mpeg_pipeline_check_lcd(ready[0]));
        const unsigned char *buffer = mpeg_pipeline_lcd_buffer(ready[0]);
        unsigned hash = 2166136261u;
        for (unsigned i = 0; i < PIPELINE_LCD_WIDTH * PIPELINE_LCD_HEIGHT * 2; ++i)
            hash = (hash ^ buffer[i]) * 16777619u;
        CHECK(!mpeg_pipeline_check_lcd(ready[0]));
        CHECK(hash == expected_hash[expected]);
        printf("RGB565 frame=%u hash=%08x\n", expected, hash);
        /* Use zero simulated SPI time; this is not a throughput measurement. */
        unsigned ack[3] = {expected++, 0, 0};
        pipeline_send(c_render, ack, 3);
    }
}

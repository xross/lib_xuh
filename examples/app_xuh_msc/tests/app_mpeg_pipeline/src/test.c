#include <stdio.h>
#include <stdlib.h>
#include <xcore/hwtimer.h>
#include "mpeg_pipeline.h"
#include "video_loader.h"

#define CHECK(condition) do { if (!(condition)) { \
    printf("FAIL: %s:%u: %s\n", __FILE__, __LINE__, #condition); exit(1); } } while (0)

/* Substitute only storage and LCD hardware. Exercise the actual shared loader,
 * decoder, channels, tile-local buffers, colour conversion and repeated plays. */
enum { CASE_GOOD, CASE_OPEN_FAILS, CASE_READ_FAILS, CASE_EMPTY };
static FILE *file;
static unsigned test_case;

VIDEO_SOURCE_OPEN static int source_open(void *context)
{
    (void)context;
    if (test_case == CASE_OPEN_FAILS) return 1;
    file = fopen("VIDEO.M1V", "rb");
    return file ? 0 : 1;
}

VIDEO_SOURCE_READ static int source_read(void *context, unsigned char *destination, unsigned capacity, unsigned *bytes)
{
    (void)context;
    *bytes = 0;
    if (test_case == CASE_READ_FAILS) return -1;
    if (test_case == CASE_EMPTY) return 0;
    *bytes = fread(destination, 1, capacity, file);
    if (ferror(file)) return -1;
    return *bytes ? 1 : 0;
}

VIDEO_SOURCE_CLOSE static int source_close(void *context)
{
    (void)context;
    int rc = fclose(file);
    file = NULL;
    return rc;
}

void test_loader(chanend c_load)
{
    static const video_source_t source = {
        "VIDEO.M1V", NULL, source_open, source_read, source_close,
        mpeg_pipeline_input_buffer, MPEG_LOAD_BYTES };
    static video_play_t play;
    setbuf(stdout, NULL);
    for (unsigned number = 1; number <= 7; ++number) {
        test_case = number == 4 ? CASE_OPEN_FAILS : number == 5 ? CASE_READ_FAILS :
                    number == 6 ? CASE_EMPTY : CASE_GOOD;
        video_loader_play(c_load, &source, &play);
        const unsigned *stats = play.stats;
        CHECK(!stats[MPEG_ARENA_LIVE]);
        if (test_case != CASE_GOOD) {
            CHECK(!stats[PIPELINE_FRAMES] && stats[PIPELINE_ERRORS]);
            CHECK(!stats[PIPELINE_RECEIVE_TICKS] && !stats[PIPELINE_CONVERT_TICKS]);
            CHECK(!stats[MPEG_SEND_US] && !stats[MPEG_ACK_WAIT_US] &&
                  !stats[PIPELINE_RENDER_WAIT_TICKS]);
            CHECK(test_case == CASE_EMPTY || play.failed);
        } else {
            CHECK(!play.failed && play.units);
            CHECK(stats[PIPELINE_FRAMES] == MPEG_PIPELINE_TEST_FRAMES &&
                  stats[MPEG_DECODED_FRAMES] == MPEG_PIPELINE_TEST_FRAMES);
            CHECK(!stats[PIPELINE_ERRORS]);
            CHECK(stats[PIPELINE_RECEIVE_TICKS] && stats[PIPELINE_CONVERT_TICKS]);
            CHECK(stats[MPEG_SEND_US] && stats[MPEG_ACK_WAIT_US]);
            CHECK(stats[MPEG_SEND_US] + stats[MPEG_ACK_WAIT_US] <= stats[PIPELINE_TRANSFER_TICKS]);
            if (MPEG_PIPELINE_TEST_FRAMES > 1) CHECK(stats[PIPELINE_RENDER_WAIT_TICKS]);
            if (number == 1) video_loader_report(number, &play);
        }
        printf("PASS: pipeline play=%u frames=%u errors=%u live=%u\n", number,
               stats[PIPELINE_FRAMES], stats[PIPELINE_ERRORS], stats[MPEG_ARENA_LIVE]);
    }
    puts("PASS: MPEG pipeline repeat, EOF drain, open/read/empty errors and recovery");
    exit(0);
}

void test_renderer(chanend c_render)
{
    hwtimer_t timer = hwtimer_alloc();
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
        /* Deliberately slower than reception/conversion/decode: early YUV
         * acknowledgement must not allow reuse of the RGB slot being read. */
        hwtimer_delay(timer, 10000000);
        const unsigned char *buffer = mpeg_pipeline_lcd_buffer(ready[0]);
        unsigned hash = 2166136261u;
        for (unsigned i = 0; i < PIPELINE_LCD_WIDTH * PIPELINE_LCD_HEIGHT * 2; ++i)
            hash = (hash ^ buffer[i]) * 16777619u;
        CHECK(!mpeg_pipeline_check_lcd(ready[0]));
        CHECK(hash == expected_hash[expected]);
        printf("RGB565 frame=%u hash=%08x\n", expected, hash);
        /* Delay is a backpressure fixture, not an SPI throughput measurement. */
        unsigned ack[3] = {expected++, 0, 0};
        pipeline_send(c_render, ack, 3);
    }
}

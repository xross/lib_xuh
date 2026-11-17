#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <xcore/hwtimer.h>
#include "audio_pipeline.h"
#include "dac_fixture.h"
#include "jpeg_pipeline.h"
#include "mpeg_audio.h"

static unsigned char input[1728];
static unsigned char reference_memory[MPEG_AUDIO_MEMORY_SIZE] __attribute__((aligned(8)));
static unsigned reference_hash;

/* Decode independently on tile 0, without a queue, to check every output pair
 * and its order, not just the player's last-sample-per-block checksum. */
static void reference(void)
{
    FILE *file = fopen("short.mp2", "rb");
    assert(file);
    assert(fread(input, 1, sizeof(input), file) == sizeof(input));
    assert(fgetc(file) == EOF && !ferror(file) && fclose(file) == 0);
    assert(mpeg_audio_open(reference_memory, sizeof(reference_memory)) == MPEG_NEED_INPUT);
    assert(mpeg_audio_feed(input, sizeof(input)) == MPEG_NEED_INPUT);
    for (unsigned i = 0; i < 3; ++i) {
        mpeg_audio_frame_t frame;
        assert(mpeg_audio_next(&frame) == MPEG_FRAME && frame.sample_rate == AUDIO_DAC_RATE);
        for (unsigned j = 0; j < MPEG_AUDIO_FRAME_SAMPLES; ++j)
            reference_hash = test_pcm_hash(reference_hash, frame.samples + 2 * j);
    }
    mpeg_audio_frame_t frame;
    assert(mpeg_audio_next(&frame) == MPEG_NEED_INPUT);
    assert(mpeg_audio_finish() == MPEG_END);
    mpeg_audio_close();
    assert(reference_hash);
}

static void observe(chanend consumer, unsigned operation, unsigned limit,
                    unsigned observation[FIXTURE_WORDS])
{
    unsigned request[2] = {operation, limit};
    pipeline_send(consumer, request, 2);
    pipeline_receive(consumer, observation, FIXTURE_WORDS);
}

static void start(chanend audio, chanend consumer, unsigned decode)
{
    unsigned clock, snapshot[FIXTURE_WORDS];
    assert(audio_pipeline_start(audio, decode) == 0);
    assert(!audio_pipeline_clock(&clock));
    observe(consumer, FIXTURE_ARM, FIXTURE_FRAMES, snapshot);
    assert(!snapshot[DAC_STARTED] && !snapshot[DAC_PLAYED]);
    assert(!snapshot[DAC_UNDERFLOWS] && !snapshot[DAC_MISSING]);
    assert(!snapshot[FIXTURE_AUDIO_VALID]);
    assert(audio_pipeline_send(audio, input, 0) == 0);
}

static void complete(chanend audio, chanend consumer, unsigned fragment, hwtimer_t timer,
                     unsigned idle_before_stop)
{
    start(audio, consumer, 1);
    unsigned snapshot[FIXTURE_WORDS], stats[AUDIO_STATS_WORDS];
    hwtimer_delay(timer, 100000u);  /* Callback is alive, but startup is silent. */
    observe(consumer, FIXTURE_SNAPSHOT, 0, snapshot);
    assert(snapshot[FIXTURE_CALLBACKS] && !snapshot[FIXTURE_NONZERO]);
    assert(!snapshot[DAC_STARTED] && !snapshot[DAC_PLAYED]);
    audio_pipeline_offset(audio, 17);
    for (unsigned offset = 0; offset < sizeof(input);) {
        unsigned count = sizeof(input) - offset;
        if (count > fragment) count = fragment;
        assert(audio_pipeline_send(audio, input + offset, count) == 0);
        offset += count;
    }
    if (idle_before_stop) hwtimer_delay(timer, 10000000u);
    audio_pipeline_stop(audio, stats);
    assert(stats[AUDIO_BYTES] == sizeof(input) && stats[AUDIO_BLOCKS] == 3);
    assert(stats[AUDIO_SAMPLES] == FIXTURE_FRAMES && stats[AUDIO_RATE] == AUDIO_DAC_RATE);
    assert(!stats[AUDIO_ERRORS] && !stats[AUDIO_UNDERFLOWS] && !stats[AUDIO_HELD_SAMPLES]);
    assert(!stats[AUDIO_DROPPED_SAMPLES] && stats[AUDIO_PEAK]);
    observe(consumer, FIXTURE_SNAPSHOT, 0, snapshot);
    /* STOP must wait for actual callback consumption of the last queued PCM. */
    assert(snapshot[DAC_PLAYED] >= FIXTURE_FRAMES);
    assert(snapshot[FIXTURE_CAPTURED] == FIXTURE_FRAMES);
    assert(snapshot[FIXTURE_HASH] == reference_hash && snapshot[FIXTURE_NONZERO]);
    assert(snapshot[FIXTURE_AUDIO_VALID] && snapshot[FIXTURE_AUDIO_MS] >= 17 + 72);
    unsigned nonzero = snapshot[FIXTURE_NONZERO];
    hwtimer_delay(timer, 100000u);
    observe(consumer, FIXTURE_SNAPSHOT, 0, snapshot);
    assert(snapshot[FIXTURE_NONZERO] == nonzero);  /* Silent after EOF. */
}

static void error_case(chanend audio, chanend consumer, unsigned bytes, unsigned decode)
{
    start(audio, consumer, decode);
    /* Failed input must still be acknowledged so STOP and the next START work. */
    if (bytes) assert(audio_pipeline_send(audio, input, bytes) <= 0);
    unsigned stats[AUDIO_STATS_WORDS], snapshot[FIXTURE_WORDS];
    audio_pipeline_stop(audio, stats);
    assert(stats[AUDIO_BYTES] == bytes && !stats[AUDIO_BLOCKS]);
    if (decode) assert(stats[AUDIO_ERRORS]);
    else for (unsigned i = AUDIO_BLOCKS; i < AUDIO_STATS_WORDS; ++i) assert(!stats[i]);
    observe(consumer, FIXTURE_SNAPSHOT, 0, snapshot);
    assert(!snapshot[FIXTURE_NONZERO] && !snapshot[DAC_STARTED]);
}

static void starvation(chanend audio, chanend consumer, hwtimer_t timer)
{
    start(audio, consumer, 1);
    assert(audio_pipeline_send(audio, input, 576) == 0);
    hwtimer_delay(timer, 6000000u);
    unsigned snapshot[FIXTURE_WORDS], stats[AUDIO_STATS_WORDS];
    observe(consumer, FIXTURE_SNAPSHOT, 0, snapshot);
    assert(snapshot[DAC_STARTED] && snapshot[DAC_PLAYED] > 1152);
    assert(!snapshot[DAC_UNDERFLOWS]);  /* A provisional EOF tail is not a gap. */
    unsigned played = snapshot[DAC_PLAYED], nonzero = snapshot[FIXTURE_NONZERO];
    hwtimer_delay(timer, 100000u);
    observe(consumer, FIXTURE_SNAPSHOT, 0, snapshot);
    assert(snapshot[DAC_PLAYED] > played && snapshot[FIXTURE_NONZERO] == nonzero);
    int result = audio_pipeline_send(audio, input + 576, 1152);
    assert(result <= 0);
    /* Give the callback time to confirm the gap, then require a fresh ACK to
     * report strict failure (the preceding ACK can race confirmation). */
    hwtimer_delay(timer, APP_XUH_MSC_PLAYER_PACING_STRICT ? 1000000u : 100000u);
    result = audio_pipeline_send(audio, input, 576);
    unsigned clock;
    if (APP_XUH_MSC_PLAYER_PACING_STRICT) {
        assert(result < 0 && !audio_pipeline_clock(&clock));
        observe(consumer, FIXTURE_SNAPSHOT, 0, snapshot);
        assert(!snapshot[DAC_STARTED]);  /* Error reset reached the callback. */
    } else {
        assert(result == 0 && audio_pipeline_clock(&clock));
    }
    audio_pipeline_stop(audio, stats);
    assert(!stats[AUDIO_ERRORS] && stats[AUDIO_UNDERFLOWS] == 1);
    assert(stats[AUDIO_FIRST_UNDERFLOW] == 1152 && stats[AUDIO_HELD_SAMPLES] >= 1152);
    assert(stats[AUDIO_HELD_SAMPLES] == stats[AUDIO_DROPPED_SAMPLES]);
    if (!APP_XUH_MSC_PLAYER_PACING_STRICT) assert(stats[AUDIO_BLOCKS] == 4);
}

/* Same provisional MP2 stack bound as the production task: PL_MPEG's unused
 * indirect callbacks prevent complete automatic analysis. */
#pragma stackfunction 1024
void test_dac_pipeline(chanend audio, chanend consumer)
{
    hwtimer_t timer = hwtimer_alloc();
    assert(timer);
    reference();
    complete(audio, consumer, 1728, timer, 0);
    puts("DAC case 1: startup, PCM order and EOF drain"); fflush(stdout);
    complete(audio, consumer, 127, timer, 1);
    puts("DAC case 2: fragmented replay and delayed STOP without false underflow"); fflush(stdout);
    error_case(audio, consumer, sizeof(input), 0);
    error_case(audio, consumer, 0, 1);
    error_case(audio, consumer, 123, 1);
    unsigned char first = input[0];
    input[0] = 0;
    error_case(audio, consumer, sizeof(input), 1);
    input[0] = first;
    puts("DAC cases 3-6: bypass, empty, truncated and malformed input"); fflush(stdout);
    starvation(audio, consumer, timer);
    puts("DAC case 7: silence, continuous clock and underflow/strict failure"); fflush(stdout);
    complete(audio, consumer, 1728, timer, 0);
    puts("DAC case 8: clean replay after underflow/strict failure"); fflush(stdout);
    puts("PASS: DAC audio pipeline 8 cases; real queue/mailbox, timed callback, no I2S pins");
    exit(0);
}

void test_dac_watchdog(void)
{
    hwtimer_t timer = hwtimer_alloc();
    assert(timer);
    hwtimer_delay(timer, 300000000u);
    fputs("FAIL: DAC audio pipeline exceeded 3 target seconds\n", stderr);
    exit(1);
}

#include "audio_pipeline.h"
#if APP_XUH_MSC_PLAYER_MPEG_AUDIO
#include <string.h>
#include <xcore/hwtimer.h>
#include <xcore/select.h>
#include "jpeg_pipeline.h"
#include "mpeg_audio.h"
#include "audio_meter.h"
#include "decode_lead.h"
#include "audio_playback.h"
#include "audio_dac_queue.h"

/* The MP2 decoder is lib_mpeg's; this task owns its memory. */
static unsigned char audio_memory[MPEG_AUDIO_MEMORY_SIZE] __attribute__((aligned(8)));

/* A held start goes ahead this long after its go: the time the picture that
 * prompted the go takes to appear. */
enum { START_LATENCY_TICKS = APP_XUH_MSC_PLAYER_DISPLAY_LATENCY_MS * (AUDIO_REFERENCE_HZ / 1000) };

/* Sender's tile: the audio clock as of the newest acknowledgement. Between a
 * go and the first acknowledgement that confirms the start, the clock is the
 * one the go asks for, so that the sender need not wait to learn of it. */
static unsigned clock_offset, clock_valid, go_expected, go_time;

int audio_pipeline_clock(unsigned *offset)
{
    if (!clock_valid && go_expected) {
        *offset = go_time + START_LATENCY_TICKS;
        return 1;
    }
    *offset = clock_offset;
    return (int)clock_valid;
}

int audio_pipeline_start(chanend channel, unsigned decode)
{
    unsigned header[2] = {AUDIO_START, decode}, result;
    clock_valid = go_expected = 0;
    pipeline_send(channel, header, 2);
    pipeline_receive(channel, &result, 1);
    return (int)result;
}

void audio_pipeline_offset(chanend channel, int milliseconds)
{
    unsigned message[2] = {AUDIO_OFFSET, (unsigned)milliseconds};
    pipeline_send(channel, message, 2);
}

void audio_pipeline_go(chanend channel)
{
    unsigned message[2] = {AUDIO_GO, 0};
    pipeline_send(channel, message, 2);
    go_time = get_reference_time();
    go_expected = 1;
}

/* Audio task: media time of the first audio sample, from AUDIO_OFFSET. */
static int media_start_ms;

/* Audio task, paced: compressed audio on its way to the decoder. With
 * APP_XUH_MSC_PLAYER_AUDIO_WAIT_BYTES it waits in a buffer in front of the
 * decoder's own input; without, it goes straight to the decoder. */
#if APP_XUH_MSC_PLAYER_AUDIO_WAIT_BYTES
static struct {
    unsigned char bytes[APP_XUH_MSC_PLAYER_AUDIO_WAIT_BYTES];
    unsigned head, count;
} waiting;

static void waiting_reset(void) { waiting.head = waiting.count = 0; }
static unsigned waiting_count(void) { return waiting.count; }

/* Whether another chunk can be taken from the sender. */
static unsigned waiting_room(void)
{
    return sizeof(waiting.bytes) - waiting.count >= AUDIO_CHUNK_BYTES;
}

/* Take a chunk. Returns the bytes that reached the decoder, or -1 on a
 * decoder error. */
static int waiting_put(const unsigned char *data, unsigned bytes)
{
    unsigned tail = (waiting.head + waiting.count) % sizeof(waiting.bytes);
    unsigned first = sizeof(waiting.bytes) - tail;
    if (first > bytes) first = bytes;
    memcpy(waiting.bytes + tail, data, first);
    memcpy(waiting.bytes, data + first, bytes - first);
    waiting.count += bytes;
    return 0;
}

/* Give the decoder as much as it has room for. Returns the bytes moved, or -1
 * on a decoder error. */
static int waiting_feed(void)
{
    int moved = 0;
    while (waiting.count) {
        unsigned count = (unsigned)mpeg_audio_input_space();
        if (count > waiting.count) count = waiting.count;
        if (count > sizeof(waiting.bytes) - waiting.head) count = sizeof(waiting.bytes) - waiting.head;
        if (!count) break;
        if (mpeg_audio_feed(waiting.bytes + waiting.head, count) < 0) return -1;
        waiting.head = (waiting.head + count) % sizeof(waiting.bytes);
        waiting.count -= count;
        moved += (int)count;
    }
    return moved;
}
#else
static void waiting_reset(void) {}
static unsigned waiting_count(void) { return 0; }
static unsigned waiting_room(void) { return mpeg_audio_input_space() >= AUDIO_CHUNK_BYTES; }
static int waiting_put(const unsigned char *data, unsigned bytes)
{
    return mpeg_audio_feed(data, bytes) < 0 ? -1 : (int)bytes;
}
static int waiting_feed(void) { return 0; }
#endif

/* Audio task: a start held for AUDIO_GO. The picture that prompted the go takes
 * the display latency to appear, so the output starts that long after it. */
static struct { unsigned held, go, go_time; } start_gate;

/* Whether a held start may now go ahead. Nothing is played while it is held,
 * so the sender would be kept waiting once there is no room for its audio:
 * start then, without the go, and at the end of the input. */
static unsigned start_due(unsigned now, unsigned stopping)
{
    if (stopping || !waiting_room()) return 1;
    return start_gate.go && (int)(now - (start_gate.go_time + START_LATENCY_TICKS)) >= 0;
}

/* The task's next wake-up, brought forward to a held start that has its go. */
static unsigned start_wake(unsigned wake)
{
    unsigned due = start_gate.go_time + START_LATENCY_TICKS;
    return start_gate.held && start_gate.go && (int)(due - wake) < 0 ? due : wake;
}

int audio_pipeline_send(chanend channel, const unsigned char *data, unsigned bytes)
{
    asm volatile("" ::: "memory");
    int failed = 0;
    while (bytes) {
        unsigned count = bytes > AUDIO_CHUNK_BYTES ? AUDIO_CHUNK_BYTES : bytes;
        chanend_out_word(channel, AUDIO_DATA);
        chanend_out_word(channel, count);
        for (unsigned offset = 0; offset < count; offset += 4) {
            unsigned word = 0, size = count - offset < 4 ? count - offset : 4;
            memcpy(&word, data + offset, size);
            chanend_out_word(channel, word);
        }
        chanend_out_control_token(channel, XS1_CT_END);
        /* [AUDIO_DATA, elapsed output ticks, clock valid, failed] */
        unsigned consumed[4];
        pipeline_receive(channel, consumed, 4);
        if (consumed[0] != AUDIO_DATA) __builtin_trap();
        clock_offset = get_reference_time() - consumed[1];
        clock_valid = consumed[2];
        /* The start is confirmed, or will not happen. */
        if (consumed[2] || consumed[3]) go_expected = 0;
        failed |= consumed[3];
        data += count;
        bytes -= count;
    }
    return failed ? -1 : 0;
}

void audio_pipeline_stop(chanend channel, unsigned stats[AUDIO_STATS_WORDS])
{
    unsigned header[2] = {AUDIO_STOP, 0};
    pipeline_send(channel, header, 2);
    pipeline_receive(channel, stats, AUDIO_STATS_WORDS);
}

#if AUDIO_METER_ENABLED
/* Peak magnitude of each channel of one decoded block, for the on-screen meter. */
static void publish_levels(const mpeg_audio_sample_t *samples)
{
#if MPEG_AUDIO_FORMAT == MPEG_AUDIO_FORMAT_S32
    unsigned peak[2] = {0, 0};
    for (unsigned i = 0; i < 2 * MPEG_AUDIO_FRAME_SAMPLES; i += 2) {
        /* Unsigned subtraction also handles INT32_MIN without signed overflow. */
        unsigned left = samples[i] < 0 ? 0u - (unsigned)samples[i] : (unsigned)samples[i];
        unsigned right = samples[i + 1] < 0 ? 0u - (unsigned)samples[i + 1] : (unsigned)samples[i + 1];
        if (left > peak[0]) peak[0] = left;
        if (right > peak[1]) peak[1] = right;
    }
    audio_meter_publish(audio_meter_level_s32(peak[0]), audio_meter_level_s32(peak[1]));
#else
    float peak[2] = {0, 0};
    for (unsigned i = 0; i < 2 * MPEG_AUDIO_FRAME_SAMPLES; i += 2) {
        float left = samples[i] < 0 ? -samples[i] : samples[i];
        float right = samples[i + 1] < 0 ? -samples[i + 1] : samples[i + 1];
        if (left > peak[0]) peak[0] = left;
        if (right > peak[1]) peak[1] = right;
    }
    audio_meter_publish(audio_meter_level(peak[0]), audio_meter_level(peak[1]));
#endif
}
#endif

/* One decoded block: statistics, checksum and level meter. When not paced, the
 * gauge's audio position is the audio decoded: pass media_clock. When paced it
 * is the audio played, published from the output clock instead: pass NULL. */
static void output_block(const mpeg_audio_frame_t *frame, unsigned stats[AUDIO_STATS_WORDS],
                         decode_lead_clock_t *media_clock)
{
    ++stats[AUDIO_BLOCKS];
    stats[AUDIO_SAMPLES] += MPEG_AUDIO_FRAME_SAMPLES;
    stats[AUDIO_RATE] = frame->sample_rate;
#if DECODE_LEAD_ENABLED
    if (media_clock) {
        decode_lead_advance(media_clock, MPEG_AUDIO_FRAME_SAMPLES, frame->sample_rate);
        decode_lead_publish(media_clock->ms, 1);
    }
#else
    (void)media_clock;
#endif
    /* Full PCM comparison belongs to the codec reference test, not the
     * player's timing path. */
    unsigned bits;
    memcpy(&bits, frame->samples + 2 * MPEG_AUDIO_FRAME_SAMPLES - 1, sizeof(bits));
    stats[AUDIO_CHECKSUM] = (stats[AUDIO_CHECKSUM] * 33) ^ bits;
#if AUDIO_METER_ENABLED
    publish_levels(frame->samples);
#endif
}

/* Decode one block, timing only the decoder. */
static mpeg_result_t decode_block(mpeg_audio_frame_t *frame, unsigned stats[AUDIO_STATS_WORDS])
{
    unsigned start = get_reference_time();
    mpeg_result_t next = mpeg_audio_next(frame);
    stats[AUDIO_DECODE_US] += (get_reference_time() - start) / 100;
    return next;
}

#if !APP_XUH_MSC_PLAYER_AUDIO_DAC
/* Shadow output of the decode/discard stand-in, not physical I2S. At a block's
 * end retain its last emitted pair until another usable block completes.
 * Volatile keeps this modelled output observable, including while starved. */
static volatile mpeg_audio_sample_t held_output[2];
#endif

static unsigned strict_failed(const unsigned stats[AUDIO_STATS_WORDS])
{
    return APP_XUH_MSC_PLAYER_PACING_STRICT && stats[AUDIO_UNDERFLOWS];
}

/* The channel protocol and decoder lifecycle shared by both forms of the task. */

/* Start of a play: take START, open the decoder, clear the displays and reply.
 * Returns whether this play decodes. */
static unsigned begin_play(chanend channel, unsigned stats[AUDIO_STATS_WORDS])
{
    unsigned header[2];
    pipeline_receive(channel, header, 2);
    if (header[0] != AUDIO_START) __builtin_trap();
    media_start_ms = 0;
    unsigned decode = header[1] & 1;
    start_gate.held = decode && (header[1] & AUDIO_WAIT_FOR_GO);
    start_gate.go = 0;
    waiting_reset();
#if APP_XUH_MSC_PLAYER_AUDIO_DAC
    audio_dac_reset(start_gate.held);
#endif
    unsigned result = decode &&
        mpeg_audio_open(audio_memory, sizeof(audio_memory)) != MPEG_NEED_INPUT;
#if DECODE_LEAD_ENABLED
    decode_lead_publish(0, 0);
    decode_lead_underflows_publish(0, 0);
#endif
#if AUDIO_METER_ENABLED
    audio_meter_publish(0, 0);
#endif
    pipeline_send(channel, &result, 1);
    if (result) ++stats[AUDIO_ERRORS];
    return decode;
}

/* One message from the sender, once the channel is ready. Returns its
 * operation: AUDIO_DATA with *bytes of payload, AUDIO_STOP, or AUDIO_OFFSET
 * or AUDIO_GO, which are handled here. */
static unsigned receive_message(chanend channel, unsigned payload[AUDIO_CHUNK_BYTES / 4],
                                unsigned *bytes)
{
    unsigned operation = chanend_in_word(channel);
    unsigned value = chanend_in_word(channel);
    if (operation == AUDIO_DATA) {
        if (!value || value > AUDIO_CHUNK_BYTES) __builtin_trap();
        for (unsigned i = 0; i < (value + 3) / 4; ++i) payload[i] = chanend_in_word(channel);
    } else if (operation == AUDIO_OFFSET) {
        media_start_ms = (int)value;
    } else if (operation == AUDIO_GO) {
        start_gate.go = 1;
        start_gate.go_time = get_reference_time();
    } else if (operation != AUDIO_STOP) {
        __builtin_trap();
    }
    chanend_check_control_token(channel, XS1_CT_END);
    asm volatile("" ::: "memory");
    *bytes = value;
    return operation;
}

/* End of a play: close the decoder and return the statistics. A strict abort
 * may leave the input incomplete; that is not reported as a decoder error. */
static void end_play(chanend channel, unsigned decode, unsigned stats[AUDIO_STATS_WORDS])
{
    if (decode) {
        if (!strict_failed(stats) &&
            (mpeg_audio_finish() != MPEG_END || !stats[AUDIO_BLOCKS])) ++stats[AUDIO_ERRORS];
        stats[AUDIO_PEAK] = (unsigned)mpeg_audio_memory_stats().peak;
        mpeg_audio_close();
    }
#if DECODE_LEAD_ENABLED
    /* Otherwise the last audio position stays available while video drains. */
    if (stats[AUDIO_ERRORS]) decode_lead_publish(0, 0);
#endif
#if AUDIO_METER_ENABLED
    audio_meter_publish(0, 0);
#endif
    pipeline_send(channel, stats, AUDIO_STATS_WORDS);
}

#if APP_XUH_MSC_PLAYER_AUDIO_DAC
#if MPEG_AUDIO_FORMAT != MPEG_AUDIO_FORMAT_S32 || !APP_XUH_MSC_PLAYER_AUDIO_PACED
#error Physical I2S output requires paced S32 decoding
#endif

enum { DAC_POLL_TICKS = AUDIO_REFERENCE_HZ / 200 };

static void dac_progress(unsigned status[DAC_STATUS_WORDS], unsigned stats[AUDIO_STATS_WORDS])
{
    audio_dac_status(status);
    stats[AUDIO_UNDERFLOWS] = status[DAC_UNDERFLOWS];
    /* The DAC plays silence for missing PCM, and the late PCM is dropped. */
    stats[AUDIO_HELD_SAMPLES] = stats[AUDIO_DROPPED_SAMPLES] = status[DAC_MISSING];
    stats[AUDIO_FIRST_UNDERFLOW] = status[DAC_FIRST_UNDERFLOW];
#if DECODE_LEAD_ENABLED
    decode_lead_publish((unsigned)((unsigned long long)status[DAC_PLAYED] * 1000 /
                                  AUDIO_DAC_RATE) + (unsigned)media_start_ms, status[DAC_STARTED]);
    decode_lead_underflows_publish(status[DAC_UNDERFLOWS],
        (unsigned)((unsigned long long)status[DAC_MISSING] * 1000 / AUDIO_DAC_RATE));
#endif
}

/* Output to the DAC. The I2S callback plays the PCM queue at the sample rate;
 * this task keeps it fed. One decoded block is held, in the decoder's own
 * memory, until the queue has taken all of it. Compressed audio waits on its
 * way to the decoder, and a chunk is taken from the sender only when there is
 * room. The audio clock reported to the sender is the count of frames the
 * DAC has played. The stack bound is provisional, as for the stand-in. */
#pragma stackfunction 1024
void mp2_audio_task(chanend channel)
{
    unsigned payload[AUDIO_CHUNK_BYTES / 4];
    hwtimer_t timer = hwtimer_alloc();
    if (!timer) __builtin_trap();
    for (;;) {
        unsigned stats[AUDIO_STATS_WORDS] = {0};
        unsigned status[DAC_STATUS_WORDS] = {0};
        const mpeg_audio_sample_t *block = NULL;
        unsigned remaining = 0, need_input = 1, stopping = 0, acknowledge = 0, silenced = 0;
        unsigned decode = begin_play(channel, stats);
        for (;;) {
            unsigned failed = stats[AUDIO_ERRORS] || strict_failed(stats);
            unsigned active = decode && !failed;
            if (active) {
                int fed = waiting_feed();
                if (fed > 0) need_input = 0;
                /* Or there is input that yields no block and no room for more. */
                if (fed < 0 || (!fed && need_input && !remaining &&
                                (waiting_count() || !waiting_room()))) {
                    ++stats[AUDIO_ERRORS];
                    continue;
                }
                if (start_gate.held && start_due(get_reference_time(), stopping)) {
                    audio_dac_release();
                    start_gate.held = 0;
                }
                if (!remaining && !need_input) {
                    mpeg_audio_frame_t frame;
                    mpeg_result_t next = decode_block(&frame, stats);
                    if (next == MPEG_FRAME) {
                        output_block(&frame, stats, NULL);
                        if (frame.sample_rate != AUDIO_DAC_RATE) {
                            ++stats[AUDIO_ERRORS];  /* No rate conversion. */
                            continue;
                        }
                        block = frame.samples;
                        remaining = MPEG_AUDIO_FRAME_SAMPLES;
                    } else {
                        need_input = 1;
                        if (next < 0) { ++stats[AUDIO_ERRORS]; continue; }
                    }
                }
                if (remaining) {
                    unsigned taken = audio_dac_write(block, remaining);
                    block += 2 * taken;
                    remaining -= taken;
                }
                dac_progress(status, stats);
                if (strict_failed(stats)) continue;
                /* Have the next block decoded before the queue needs it. */
                if (!remaining && !need_input) continue;
            } else if (failed && !silenced) {
                audio_dac_reset(0);
                remaining = 0;
                silenced = 1;
#if DECODE_LEAD_ENABLED
                decode_lead_publish(0, 0);
#endif
            }
            if (acknowledge) {
                unsigned live = active && status[DAC_STARTED];
                unsigned consumed[4] = {AUDIO_DATA,
                    (unsigned)((unsigned long long)status[DAC_PLAYED] * AUDIO_REFERENCE_HZ /
                               AUDIO_DAC_RATE), live, failed};
                pipeline_send(channel, consumed, 4);
                acknowledge = 0;
            }
            if (stopping && (!active || (need_input && !remaining && !waiting_count()))) break;
            unsigned accept = !active || waiting_room();
            unsigned timed = active && (remaining || status[DAC_STARTED] || start_gate.held);
            if (timed) hwtimer_set_trigger_time(timer,
                start_wake(get_reference_time() + DAC_POLL_TICKS));
            SELECT_RES_ORDERED(CASE_GUARD_THEN(timer, timed, on_poll),
                               CASE_GUARD_THEN(channel, accept && !stopping, on_message))
            {
            on_message: {
                unsigned bytes, operation = receive_message(channel, payload, &bytes);
                if (operation == AUDIO_STOP) stopping = 1;
                if (operation != AUDIO_DATA) break;
                stats[AUDIO_BYTES] += bytes;
                if (active) {
                    int put = waiting_put((const unsigned char *)payload, bytes);
                    if (put < 0) ++stats[AUDIO_ERRORS];
                    else if (put) need_input = 0;
                }
                acknowledge = 1;
                break;
            }
            on_poll: {
                (void)hwtimer_get_time(timer);      /* Clears the event. */
                break;
            }
            }
        }
        hwtimer_clear_trigger_time(timer);
        if (!silenced) {
            audio_dac_finish();
            dac_progress(status, stats);
        }
        end_play(channel, decode, stats);
    }
}
#else
/* PL_MPEG's unused indirect callbacks prevent complete automatic stack sizing.
 * This provisional bound includes the 1-KiB payload and synthesis scratch.
 *
 * Decode one block ahead of its output deadline. The decoder owns its PCM; the
 * stand-in needs only the last pair, so there is no additional PCM buffer.
 * Starvation does not reset the epoch. Decode late frames to preserve synthesis
 * state, omit their obsolete prefix, and hold the last emitted pair for gaps.
 * A gap is confirmed when later media arrives: an unconfirmed tail at EOF is
 * not an underflow. STOP drains the final buffered audio at the normal rate. */
#pragma stackfunction 1024
void mp2_audio_task(chanend channel)
{
    unsigned payload[AUDIO_CHUNK_BYTES / 4];
    hwtimer_t timer = hwtimer_alloc();
    for (;;) {
        unsigned stats[AUDIO_STATS_WORDS] = {0};
        decode_lead_clock_t media_clock = {0};
        audio_playback_t playback = {0};
        unsigned playing = 0, due = 0, pending = 0, pending_due = 0;
        unsigned epoch = 0, need_input = 1, stopping = 0, acknowledge = 0;
        mpeg_audio_sample_t tail[2] = {0}, pending_tail[2] = {0};
        held_output[0] = held_output[1] = 0;
        unsigned decode = begin_play(channel, stats);
        unsigned paced = APP_XUH_MSC_PLAYER_AUDIO_PACED && decode;
        for (;;) {
            unsigned active = paced && !stats[AUDIO_ERRORS] && !strict_failed(stats);
            if (active) {
                unsigned now = get_reference_time();
                audio_playback_advance(&playback, now);
#if DECODE_LEAD_ENABLED
                /* The gauge compares the picture shown with the audio played.
                 * Blocks are decoded ahead of their output, so the decoded
                 * position would read up to two blocks early. */
                if (playback.started)
                    decode_lead_publish((unsigned)((unsigned long long)playback.played * 1000 /
                                                   playback.rate) + (unsigned)media_start_ms, 1);
#endif
                if (playing && (int)(now - due) >= 0) {
                    held_output[0] = tail[0];
                    held_output[1] = tail[1];
                    playing = 0;
                }
                int fed = waiting_feed();
                if (fed > 0) need_input = 0;
                /* Or the decoder is full of input that yields no block. */
                if (fed < 0 || (!fed && need_input && !pending && !playing &&
                                (waiting_count() || !waiting_room()))) {
                    ++stats[AUDIO_ERRORS];
                    continue;
                }
                if (start_gate.held && start_due(now, stopping)) start_gate.held = 0;
                /* The clock starts with the first decoded block. */
                if (!pending && !need_input && !start_gate.held) {
                    mpeg_audio_frame_t frame;
                    mpeg_result_t next = decode_block(&frame, stats);
                    if (next == MPEG_FRAME) {
                        output_block(&frame, stats, NULL);
                        /* lib_mpeg requires one format throughout a stream. */
                        if (playback.started && playback.rate != frame.sample_rate) {
                            ++stats[AUDIO_ERRORS];
                            continue;
                        }
                        now = get_reference_time();
                        if (!playback.started) epoch = now;
                        unsigned skip = audio_playback_frame(&playback, now,
                                            MPEG_AUDIO_FRAME_SAMPLES, frame.sample_rate);
                        if (!stats[AUDIO_UNDERFLOWS] && playback.underflows)
                            stats[AUDIO_FIRST_UNDERFLOW] = playback.decoded - MPEG_AUDIO_FRAME_SAMPLES;
                        stats[AUDIO_UNDERFLOWS] = playback.underflows;
                        stats[AUDIO_HELD_SAMPLES] = playback.held;
                        stats[AUDIO_DROPPED_SAMPLES] = playback.dropped;
#if DECODE_LEAD_ENABLED
                        decode_lead_underflows_publish(playback.underflows,
                            (unsigned)((unsigned long long)playback.held * 1000 / playback.rate));
#endif
                        if (skip < MPEG_AUDIO_FRAME_SAMPLES) {
                            pending_tail[0] = frame.samples[2 * MPEG_AUDIO_FRAME_SAMPLES - 2];
                            pending_tail[1] = frame.samples[2 * MPEG_AUDIO_FRAME_SAMPLES - 1];
                            pending_due = audio_playback_due(&playback);
                            pending = 1;
                        }
                        /* Re-evaluate errors/strict failure and try to catch up
                         * one decoded frame per iteration, without a busy wait. */
                        if (strict_failed(stats) || !pending) continue;
                    } else {
                        need_input = 1;
                        if (next < 0) ++stats[AUDIO_ERRORS];
                    }
                }
                if (!playing && pending) {
                    tail[0] = pending_tail[0];
                    tail[1] = pending_tail[1];
                    due = pending_due;
                    playing = 1;
                    pending = 0;
                    /* Prepare the following block before this one's deadline. */
                    continue;
                }
            }
            active = paced && !stats[AUDIO_ERRORS] && !strict_failed(stats);
            if (acknowledge) {
                /* Reply after bounded decode work so the first complete frame
                 * publishes the startup clock without requiring another send.
                 * Error paths reply too, allowing the loader to close. */
                unsigned failed = stats[AUDIO_ERRORS] || strict_failed(stats);
                unsigned live = paced && playback.started && !failed;
                unsigned consumed[4] = {AUDIO_DATA,
                    live ? get_reference_time() - epoch : 0, live, failed};
                pipeline_send(channel, consumed, 4);
                acknowledge = 0;
            }
            if (stopping && (!active || (need_input && !pending && !playing && !waiting_count()))) break;
            unsigned accept = !active || waiting_room();
            unsigned timed = active && (playback.started || start_gate.held);
            if (timed) hwtimer_set_trigger_time(timer,
                playing ? due : start_wake(get_reference_time() + AUDIO_REFERENCE_HZ / 100));
            /* Service a ready deadline before another compressed input chunk. */
            SELECT_RES_ORDERED(CASE_GUARD_THEN(timer, timed, on_due),
                               CASE_GUARD_THEN(channel, accept && !stopping, on_message))
            {
            on_message: {
                unsigned bytes, operation = receive_message(channel, payload, &bytes);
                if (operation == AUDIO_STOP) stopping = 1;
                if (operation != AUDIO_DATA) break;
                stats[AUDIO_BYTES] += bytes;
                if (decode && !stats[AUDIO_ERRORS] && !strict_failed(stats)) {
                    if (paced) {
                        int put = waiting_put((const unsigned char *)payload, bytes);
                        if (put < 0) ++stats[AUDIO_ERRORS];
                        else if (put) need_input = 0;
                    } else if (mpeg_audio_feed((const unsigned char *)payload, bytes) < 0) {
                        ++stats[AUDIO_ERRORS];
                    } else {
                        /* Decode and discard everything that has arrived. */
                        mpeg_audio_frame_t frame;
                        mpeg_result_t next;
                        while ((next = decode_block(&frame, stats)) == MPEG_FRAME)
                            output_block(&frame, stats, &media_clock);
                        if (next < 0) ++stats[AUDIO_ERRORS];
                    }
                }
#if DECODE_LEAD_ENABLED
                if (stats[AUDIO_ERRORS]) decode_lead_publish(0, 0);
#endif
                acknowledge = 1;
                break;
            }
            on_due: {
                (void)hwtimer_get_time(timer);      /* Clears the event. */
                break;
            }
            }
        }
        hwtimer_clear_trigger_time(timer);
        end_play(channel, decode, stats);
    }
}
#endif
#endif

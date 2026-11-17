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

/* The MP2 decoder is lib_mpeg's; this task owns its memory. */
static unsigned char audio_memory[MPEG_AUDIO_MEMORY_SIZE] __attribute__((aligned(8)));

/* Sender's tile: the audio clock as of the newest acknowledgement. */
static unsigned clock_offset, clock_valid;

int audio_pipeline_clock(unsigned *offset)
{
    *offset = clock_offset;
    return (int)clock_valid;
}

int audio_pipeline_start(chanend channel, unsigned decode)
{
    unsigned header[2] = {AUDIO_START, decode}, result;
    clock_valid = 0;
    pipeline_send(channel, header, 2);
    pipeline_receive(channel, &result, 1);
    return (int)result;
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

/* Shadow output of the decode/discard stand-in, not physical I2S. At a block's
 * end retain its last emitted pair until another usable block completes.
 * Volatile keeps this modelled output observable, including while starved. */
static volatile mpeg_audio_sample_t held_output[2];

static unsigned strict_failed(const unsigned stats[AUDIO_STATS_WORDS])
{
    return APP_XUH_MSC_PLAYER_PACING_STRICT && stats[AUDIO_UNDERFLOWS];
}

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
        unsigned header[2], stats[AUDIO_STATS_WORDS] = {0};
        decode_lead_clock_t media_clock = {0};
        audio_playback_t playback = {0};
        unsigned playing = 0, due = 0, pending = 0, pending_due = 0;
        unsigned epoch = 0, need_input = 1, stopping = 0, acknowledge = 0;
        mpeg_audio_sample_t tail[2] = {0}, pending_tail[2] = {0};
        held_output[0] = held_output[1] = 0;
        pipeline_receive(channel, header, 2);
        if (header[0] != AUDIO_START) __builtin_trap();
        unsigned decode = header[1], result = decode &&
            mpeg_audio_open(audio_memory, sizeof(audio_memory)) != MPEG_NEED_INPUT;
        unsigned paced = APP_XUH_MSC_PLAYER_AUDIO_PACED && decode;
#if DECODE_LEAD_ENABLED
        decode_lead_publish(0, 0);
        decode_lead_underflows_publish(0, 0);
#endif
        pipeline_send(channel, &result, 1);
        if (result) ++stats[AUDIO_ERRORS];
#if AUDIO_METER_ENABLED
        audio_meter_publish(0, 0);
#endif
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
                                                   playback.rate), 1);
#endif
                if (playing && (int)(now - due) >= 0) {
                    held_output[0] = tail[0];
                    held_output[1] = tail[1];
                    playing = 0;
                }
                if (!pending && !need_input) {
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
            if (stopping && (!active || (need_input && !pending && !playing))) break;
            unsigned accept = !active || mpeg_audio_input_space() >= AUDIO_CHUNK_BYTES;
            /* A full buffer that yields no block would never drain. */
            if (!accept && !playing && !pending) { ++stats[AUDIO_ERRORS]; continue; }
            unsigned timed = active && playback.started;
            if (timed) hwtimer_set_trigger_time(timer,
                playing ? due : get_reference_time() + AUDIO_REFERENCE_HZ / 100);
            /* Service a ready deadline before another compressed input chunk. */
            SELECT_RES_ORDERED(CASE_GUARD_THEN(timer, timed, on_due),
                               CASE_GUARD_THEN(channel, accept && !stopping, on_message))
            {
            on_message: {
                unsigned operation = chanend_in_word(channel);
                unsigned bytes = chanend_in_word(channel);
                if (operation == AUDIO_STOP) {
                    chanend_check_control_token(channel, XS1_CT_END);
                    stopping = 1;
                    break;
                }
                if (operation != AUDIO_DATA || !bytes || bytes > AUDIO_CHUNK_BYTES) __builtin_trap();
                for (unsigned i = 0; i < (bytes + 3) / 4; ++i)
                    payload[i] = chanend_in_word(channel);
                chanend_check_control_token(channel, XS1_CT_END);
                asm volatile("" ::: "memory");
                stats[AUDIO_BYTES] += bytes;
                if (decode && !stats[AUDIO_ERRORS] && !strict_failed(stats)) {
                    if (mpeg_audio_feed((const unsigned char *)payload, bytes) < 0) {
                        ++stats[AUDIO_ERRORS];
                    } else if (!paced) {
                        /* Decode and discard everything that has arrived. */
                        mpeg_audio_frame_t frame;
                        mpeg_result_t next;
                        while ((next = decode_block(&frame, stats)) == MPEG_FRAME)
                            output_block(&frame, stats, &media_clock);
                        if (next < 0) ++stats[AUDIO_ERRORS];
                    } else {
                        need_input = 0;
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
        if (decode) {
            /* Strict abort may intentionally leave an incomplete input stream;
             * do not misreport its truncation as a second decoder error. */
            if (!strict_failed(stats) &&
                (mpeg_audio_finish() != MPEG_END || !stats[AUDIO_BLOCKS])) ++stats[AUDIO_ERRORS];
            stats[AUDIO_PEAK] = (unsigned)mpeg_audio_memory_stats().peak;
            mpeg_audio_close();
        }
#if DECODE_LEAD_ENABLED
        /* Keep the final audio position available while buffered video drains. */
        if (stats[AUDIO_ERRORS]) decode_lead_publish(0, 0);
#endif
#if AUDIO_METER_ENABLED
        audio_meter_publish(0, 0);
#endif
        pipeline_send(channel, stats, AUDIO_STATS_WORDS);
    }
}
#endif

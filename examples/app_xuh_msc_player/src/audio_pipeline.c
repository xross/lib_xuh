#include "audio_pipeline.h"
#if APP_XUH_MSC_PLAYER_MPEG_AUDIO
#include <string.h>
#include <xcore/hwtimer.h>
#include "jpeg_pipeline.h"
#include "mpeg_audio.h"
#include "audio_meter.h"
#include "decode_lead.h"

/* The MP2 decoder is lib_mpeg's; this task owns its memory. */
static unsigned char audio_memory[MPEG_AUDIO_MEMORY_SIZE] __attribute__((aligned(8)));

int audio_pipeline_start(chanend channel, unsigned decode)
{
    unsigned header[2] = {AUDIO_START, decode}, result;
    pipeline_send(channel, header, 2);
    pipeline_receive(channel, &result, 1);
    return (int)result;
}

int audio_pipeline_send(chanend channel, const unsigned char *data, unsigned bytes)
{
    asm volatile("" ::: "memory");
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
        unsigned consumed;
        pipeline_receive(channel, &consumed, 1);
        if (consumed != AUDIO_DATA) __builtin_trap();
        data += count;
        bytes -= count;
    }
    return 0;
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

/* PL_MPEG exposes generic load/seek callbacks even though this bounded memory
 * adapter never installs them. Bound the otherwise unresolved call graph;
 * includes the 1-KiB payload and the synthesis transform's scalar scratch. */
#pragma stackfunction 1024
void mp2_audio_task(chanend channel)
{
    unsigned payload[AUDIO_CHUNK_BYTES / 4];
    for (;;) {
        unsigned header[2], stats[AUDIO_STATS_WORDS] = {0};
#if DECODE_LEAD_ENABLED
        decode_lead_clock_t media_clock = {0};
#endif
        pipeline_receive(channel, header, 2);
        if (header[0] != AUDIO_START) __builtin_trap();
        unsigned decode = header[1], result = decode &&
            mpeg_audio_open(audio_memory, sizeof(audio_memory)) != MPEG_NEED_INPUT;
#if DECODE_LEAD_ENABLED
        decode_lead_publish(0, 0);
#endif
        pipeline_send(channel, &result, 1);
        if (result) ++stats[AUDIO_ERRORS];
#if AUDIO_METER_ENABLED
        audio_meter_publish(0, 0);
#endif
        for (;;) {
            unsigned operation = chanend_in_word(channel);
            unsigned bytes = chanend_in_word(channel);
            if (operation == AUDIO_STOP) {
                chanend_check_control_token(channel, XS1_CT_END);
                break;
            }
            if (operation != AUDIO_DATA || !bytes || bytes > AUDIO_CHUNK_BYTES) __builtin_trap();
            for (unsigned i = 0; i < (bytes + 3) / 4; ++i)
                payload[i] = chanend_in_word(channel);
            chanend_check_control_token(channel, XS1_CT_END);
            asm volatile("" ::: "memory");
            stats[AUDIO_BYTES] += bytes;
            if (decode && !stats[AUDIO_ERRORS]) {
                if (mpeg_audio_feed((const unsigned char *)payload, bytes) < 0) {
                    ++stats[AUDIO_ERRORS];
                } else {
                    mpeg_audio_frame_t frame;
                    unsigned start = get_reference_time();
                    mpeg_result_t next;
                    while ((next = mpeg_audio_next(&frame)) == MPEG_FRAME) {
                        ++stats[AUDIO_BLOCKS];
                        stats[AUDIO_SAMPLES] += MPEG_AUDIO_FRAME_SAMPLES;
                        stats[AUDIO_RATE] = frame.sample_rate;
#if DECODE_LEAD_ENABLED
                        decode_lead_advance(&media_clock, MPEG_AUDIO_FRAME_SAMPLES, frame.sample_rate);
                        decode_lead_publish(media_clock.ms, 1);
#endif
                        /* Full PCM comparison belongs to the codec reference
                         * test, not the player's timing path. */
                        unsigned bits;
                        memcpy(&bits, frame.samples + 2 * MPEG_AUDIO_FRAME_SAMPLES - 1, sizeof(bits));
                        stats[AUDIO_CHECKSUM] = (stats[AUDIO_CHECKSUM] * 33) ^ bits;
#if AUDIO_METER_ENABLED
                        publish_levels(frame.samples);
#endif
                    }
                    stats[AUDIO_DECODE_US] += (get_reference_time() - start) / 100;
                    if (next < 0) ++stats[AUDIO_ERRORS];
                }
            }
#if DECODE_LEAD_ENABLED
            if (stats[AUDIO_ERRORS]) decode_lead_publish(0, 0);
#endif
            /* END releases the route; separate messages can arrive out of
             * order without acknowledgement. Cover bypass/error paths too,
             * so the producer can still finish and send STOP. */
            unsigned consumed = AUDIO_DATA;
            pipeline_send(channel, &consumed, 1);
        }
        if (decode) {
            if (mpeg_audio_finish() != MPEG_END || !stats[AUDIO_BLOCKS]) ++stats[AUDIO_ERRORS];
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

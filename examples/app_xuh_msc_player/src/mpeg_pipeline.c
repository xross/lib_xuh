#include "mpeg_pipeline.h"
#include "video_loader.h"
#include "activity_led.h"

#if MSC_MPEG_PIPELINE
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <xcore/hwtimer.h>
#include <xcore/select.h>
#include "mpeg.h"
#include "mpeg_rgb565.h"
#include "fps_overlay.h"
#include "audio_meter.h"
#include "audio_pipeline.h"
#include "decode_lead.h"
#include "frame_timing.h"

#if APP_XUH_MSC_PLAYER_FRAME_TIMING
static void report_frame_timing(const char *name, const frame_timing_t *timing)
{
    printf("MPEG timing %s: count=%u min=%u p50_le=%u p95_le=%u p99_le=%u max=%u over_40ms=%u\n",
           name, timing->count, timing->min_us,
           frame_timing_percentile(timing, 50), frame_timing_percentile(timing, 95),
           frame_timing_percentile(timing, 99), timing->max_us, timing->over_40ms);
}
#endif

/* Pacing against the audio clock (APP_XUH_MSC_PLAYER_AUDIO_PACED, with audio).
 *
 * The loader publishes the audio output clock as an offset from this tile's
 * reference timer, in the video's time: zero is the first picture. Once audio
 * starts, its clock continues through underflows instead of restarting.
 *
 * The decoder decodes each picture, then holds it until the audio played
 * reaches it. While it waits it keeps taking video, so that the loader, which also delivers the audio,
 * can read on: a program stream carries video well before the audio for the same
 * moment, and the decoder's input buffer is where that video waits. If full,
 * wait for the picture's deadline; do not release it early. Audio underflows
 * are counted by the audio task, and strict builds fail on actual underflow. */
#ifndef APP_XUH_MSC_PLAYER_AUDIO_PACED
#error audio_pipeline.h defines APP_XUH_MSC_PLAYER_AUDIO_PACED
#endif
#if APP_XUH_MSC_PLAYER_MPEG_AUDIO && APP_XUH_MSC_PLAYER_AUDIO_DECODE && APP_XUH_MSC_PLAYER_AUDIO_PACED
#define MPEG_PACED 1
#else
#define MPEG_PACED 0
#endif
enum { TICKS_PER_MS = 100000,
       /* From a decoded picture being released to the end of its write to the display. */
       DISPLAY_LATENCY_TICKS = APP_XUH_MSC_PLAYER_DISPLAY_LATENCY_MS * TICKS_PER_MS,
       PACE_POLL_TICKS = 2 * TICKS_PER_MS };

/* One loader writer and one decoder reader on the same tile. Clock validity
 * is separate from the offset: full clips wrap the reference-timer clock. */
static volatile unsigned audio_clock_offset, audio_clock_valid;

void mpeg_pipeline_audio_clock(unsigned offset, unsigned valid)
{
    audio_clock_offset = offset;
    audio_clock_valid = valid;
}

/* Tile 1: two filesystem input chunks (one being read ahead while the other is
 * fed), plus the memory passed to lib_mpeg.
 * Tile 0: two RGB565 framebuffers and one two-row YUV strip. No RGB888 frame. */
static unsigned char input[2][MPEG_LOAD_BYTES] __attribute__((aligned(4)));
static unsigned char lcd_frames[2][PIPELINE_LCD_WIDTH * PIPELINE_LCD_HEIGHT * 2] __attribute__((aligned(4)));
static unsigned strip[PIPELINE_LCD_WIDTH * 3 / 4];
static video_window_t lcd_windows[2];
#if DECODE_LEAD_ENABLED
static unsigned lcd_video_ms[2];
unsigned mpeg_pipeline_lcd_video_ms(unsigned slot) { return lcd_video_ms[slot]; }
#endif
/* Decoder memory, owned by tile 1. Only paced playback opts into a larger
 * application arena; video-only/unpaced builds retain the library default. */
static unsigned char video_memory[MPEG_PACED ? APP_XUH_MSC_PLAYER_MPEG_ARENA_BYTES :
                                             MPEG_VIDEO_MEMORY_SIZE] __attribute__((aligned(8)));
typedef unsigned __attribute__((may_alias)) plane_word_t;
#if PIPELINE_VERIFY
static unsigned lcd_hash[2];
static unsigned hash_bytes(const unsigned char *data, unsigned count)
{
    unsigned hash = 2166136261u;
    for (unsigned i = 0; i < count; ++i) hash = (hash ^ data[i]) * 16777619u;
    return hash;
}
#endif

VIDEO_SOURCE_BUFFER unsigned char *mpeg_pipeline_input_buffer(unsigned slot) { return input[slot]; }
unsigned char *mpeg_pipeline_lcd_buffer(unsigned slot) { return lcd_frames[slot]; }
video_window_t mpeg_pipeline_lcd_window(unsigned slot) { return lcd_windows[slot]; }
unsigned mpeg_pipeline_check_lcd(unsigned slot)
{
#if PIPELINE_VERIFY
    return hash_bytes(lcd_frames[slot], sizeof(lcd_frames[slot])) != lcd_hash[slot];
#else
    (void)slot;
    return 0;
#endif
}

static void send_row(chanend c_image, const unsigned char *row, unsigned bytes)
{
    /* Decoder planes are word aligned in practice. Load words directly: a
     * memcpy from an unknown alignment costs a library call per word on xcore. */
    if (!((uintptr_t)row & 3)) {
        const plane_word_t *words = (const plane_word_t *)row;
        for (unsigned index = 0; index < bytes / 4; ++index)
            chanend_out_word(c_image, words[index]);
        return;
    }
    for (unsigned offset = 0; offset < bytes; offset += 4) {
        unsigned word;
        memcpy(&word, row + offset, sizeof(word));
        chanend_out_word(c_image, word);
    }
}

/* Send decoded planar YUV420 from tile 1 to the tile-0 receiver.
 * Each payload word carries four 8-bit Y, Cb or Cr samples, not RGB pixels.
 * Tile 0 converts each two-row strip to RGB565. Wait for its acknowledgement
 * before decoding can reuse the source planes. This handoff can block on
 * reception/conversion and on the receiver waiting for the LCD renderer. */
static void send_frame(chanend c_image, const mpeg_frame_t *frame,
                       unsigned *send_us, unsigned *ack_wait_us)
{
    asm volatile("" ::: "memory");
    unsigned start = get_reference_time();
    chanend_out_word(c_image, frame->number);
    chanend_out_word(c_image, frame->width);
    chanend_out_word(c_image, frame->height);
    /* Preserve the exact rational for media position, not rounded FPS tenths. */
    chanend_out_word(c_image, frame->frame_rate_num);
    chanend_out_word(c_image, frame->frame_rate_den);
    /* Transfer two Y rows plus their Cb/Cr row. Decoder-owned planes remain
     * valid until tile 0 acknowledges staging; only then may decode continue. */
    for (unsigned y = 0; y < frame->height; y += 2) {
        send_row(c_image, frame->plane[0] + y * frame->stride[0], frame->width);
        send_row(c_image, frame->plane[0] + (y + 1) * frame->stride[0], frame->width);
        send_row(c_image, frame->plane[1] + (y / 2) * frame->stride[1], frame->width / 2);
        send_row(c_image, frame->plane[2] + (y / 2) * frame->stride[2], frame->width / 2);
    }
    chanend_out_control_token(c_image, XS1_CT_END);
    /* This includes channel backpressure while tile 0 receives/converts.
     * Separate it from the final acknowledgement, which may wait for LCD. */
    unsigned sent = get_reference_time();
    unsigned ack;
    pipeline_receive(c_image, &ack, 1);
    unsigned acknowledged = get_reference_time();
    *send_us += (sent - start) / 100;
    *ack_wait_us += (acknowledged - sent) / 100;
    if (ack != frame->number) __builtin_trap();
}

/* The decoder's side of the loader protocol. */
typedef struct {
    /* The chunk currently owned by the decoder and how much is still unfed. */
    unsigned held, held_sequence, remaining;
    const unsigned char *chunk;
    unsigned expected, protocol_errors, stopped, wait_us, feed_us;
    unsigned input_high_water;
    /* Pictures that started more than 10 ms after they were due, and the worst. */
    unsigned late_pictures, latest_ticks;
    hwtimer_t timer;
} input_t;

/* Tell the loader that the held chunk has been fed in full. This does not wait
 * for the loader. */
static void release_input(chanend c_load, input_t *in)
{
    if (in->held) {
        unsigned ack = VIDEO_LOADER_ACK(in->held_sequence, 0);
        pipeline_send(c_load, &ack, 1);
        in->held = 0;
    }
}

/* Feed the decoder: the rest of the held chunk, or else the next chunk from the
 * loader, waiting for it. At the loader's stop, signals the end of input.
 * Returns MPEG_NEED_INPUT, or a negative result on failure. */
static mpeg_result_t take_input(chanend c_load, input_t *in)
{
    unsigned start;
    if (!in->remaining) {
        /* Input is requested only after end of input if the decoder
         * misbehaves; it should drain to MPEG_END. */
        if (in->stopped) return MPEG_ERROR;
        unsigned ready[3];
        start = get_reference_time();
        /* The one-word acknowledgement fits without waiting for the loader;
         * release ownership before receiving its next chunk. */
        release_input(c_load, in);
        pipeline_receive(c_load, ready, 3);
        in->wait_us += (get_reference_time() - start) / 100;
        if (ready[0] == PIPELINE_STOP) {
            in->stopped = 1;
            if (ready[1]) return MPEG_ERROR;
            return mpeg_feed(NULL, 0, 1);
        }
        in->held = 1;
        in->held_sequence = ready[1];
        if (ready[0] != (in->expected & 1) || ready[1] != in->expected ||
            !ready[2] || ready[2] > MPEG_LOAD_BYTES) {
            ++in->protocol_errors;
            return MPEG_ERROR;
        }
        ++in->expected;
        in->chunk = input[ready[0]];
        in->remaining = ready[2];
    }
    /* A chunk larger than the free input space is fed in parts. */
    unsigned space = mpeg_input_space();
    if (!space) return MPEG_CAPACITY;
    unsigned count = in->remaining < space ? in->remaining : space;
    start = get_reference_time();
    mpeg_result_t result = mpeg_feed(in->chunk, count, 0);
    unsigned used = mpeg_input_capacity() - mpeg_input_space();
    if (used > in->input_high_water) in->input_high_water = used;
    in->feed_us += (get_reference_time() - start) / 100;
    in->chunk += count;
    in->remaining -= count;
    return result;
}

/* Wait until the decoded picture, the one after `pictures` already shown, is
 * due on the audio clock, taking video meanwhile. Returns at once when there is no audio clock
 * or the frame rate is not yet known. Returns MPEG_NEED_INPUT, or a negative
 * result on failure. */
static mpeg_result_t hold_picture(chanend c_load, input_t *in, unsigned pictures,
                                  unsigned rate_num, unsigned rate_den)
{
    if (!rate_num) return MPEG_NEED_INPUT;
    /* All in ticks modulo 2^32; differences are good to about 21 seconds. */
    unsigned picture = (unsigned)((unsigned long long)pictures * 100000000ull * rate_den / rate_num);
    unsigned first = 1;
    for (;;) {
        if (!audio_clock_valid) return MPEG_NEED_INPUT;
        unsigned played = get_reference_time() - audio_clock_offset;
        int early = (int)(picture - DISPLAY_LATENCY_TICKS - played);
        if (early <= 0) {
            if (first && -early > 10 * TICKS_PER_MS) {
                ++in->late_pictures;
                if ((unsigned)-early > in->latest_ticks) in->latest_ticks = (unsigned)-early;
            }
            return MPEG_NEED_INPUT;
        }
        first = 0;
        /* A partially fed chunk is already ours: feeding it cannot wait on the
         * loader. Do not strand its tail while there is input space. */
        if (in->remaining && mpeg_input_space()) {
            mpeg_result_t result = take_input(c_load, in);
            if (result < 0) return result;
            continue;
        }
        unsigned take = !in->stopped && !in->remaining && mpeg_input_space() >= MPEG_LOAD_BYTES;
        if (take) {
            /* Ask for the next chunk, then wait for it or for the picture to
             * fall due, whichever is first: the loader may be some time, held
             * up delivering audio, and the picture must not wait for it. */
            release_input(c_load, in);
        }
        unsigned wait = (unsigned)early < PACE_POLL_TICKS ? (unsigned)early : PACE_POLL_TICKS;
        hwtimer_set_trigger_time(in->timer, get_reference_time() + wait);
        mpeg_result_t result = MPEG_NEED_INPUT;
        SELECT_RES(CASE_GUARD_THEN(c_load, take, on_chunk), CASE_THEN(in->timer, on_time))
        {
        on_chunk:
            result = take_input(c_load, in);
            break;
        on_time:
            (void)hwtimer_get_time(in->timer);
            break;
        }
        hwtimer_clear_trigger_time(in->timer);
        if (result < 0) return result;
    }
}

/* As in the standalone benchmark: upstream indirect callback branches prevent
 * complete automatic stack sizing. Provisional 4096-word (16 KiB) reservation. */
#pragma stackfunction 4096
void mpeg_pipeline_decode(chanend c_load, chanend c_image)
{
    hwtimer_t pace_timer = hwtimer_alloc();
    for (;;) {
        unsigned decoded = 0, decode_us = 0, transfer_us = 0;
        unsigned send_us = 0, ack_wait_us = 0;
        input_t in = {0};
        in.timer = pace_timer;
        unsigned rate_num = 0, rate_den = 1;
        unsigned stats[MPEG_STATS_WORDS] = {0};
        mpeg_result_t result = mpeg_open(video_memory, sizeof(video_memory));
        int opened = result >= 0;
#if APP_XUH_MSC_PLAYER_FRAME_TIMING
        frame_timing_t decode_timing = {.count = 0}, service_timing = {.count = 0};
        unsigned frame_decode_ticks = 0, previous_ack = 0;
#endif
        while (result >= 0) {
            mpeg_frame_t frame;
            unsigned start = get_reference_time();
            activity_led_decode(ACTIVITY_LED_BUSY);
            result = mpeg_next(&frame);
#if APP_XUH_MSC_PLAYER_FRAME_TIMING
            unsigned decode_ticks = get_reference_time() - start;
            decode_us += decode_ticks / 100;
            /* A frame may require several next/feed cycles. Sum decode work,
             * not just the final next() call, excluding input/feed/handoff. */
            frame_decode_ticks += decode_ticks;
#else
            decode_us += (get_reference_time() - start) / 100;
#endif
            activity_led_decode(result < 0 ? ACTIVITY_LED_ERROR : 0);
            if (result == MPEG_FRAME) {
                /* Reject unsupported geometry before publishing any payload. */
                if (!video_window_valid(frame.width, frame.height) ||
                    frame.number != decoded) { result = MPEG_UNSUPPORTED; break; }
                if (!decoded) {
                    video_window_t window = video_window_center(frame.width, frame.height);
                    printf("MPEG picture: %ux%u centred at (%u,%u), source rate=%u/%u FPS\n",
                           frame.width, frame.height, window.x, window.y,
                           frame.frame_rate_num, frame.frame_rate_den);
                }
                rate_num = frame.frame_rate_num;
                rate_den = frame.frame_rate_den;
                /* The picture is decoded; keep it until it is due. Its planes
                 * stay valid until the next decode. Holding here, not before
                 * decoding, keeps the time a picture takes to decode out of
                 * when it is shown. */
                if (MPEG_PACED) {
                    mpeg_result_t held = hold_picture(c_load, &in, decoded, rate_num, rate_den);
                    if (held < 0) { result = held; break; }
                }
                activity_led_decode(ACTIVITY_LED_FRAME);
                start = get_reference_time();
                send_frame(c_image, &frame, &send_us, &ack_wait_us);
                transfer_us += (get_reference_time() - start) / 100;
#if APP_XUH_MSC_PLAYER_FRAME_TIMING
                unsigned acknowledged = get_reference_time();
                frame_timing_add(&decode_timing, frame_decode_ticks / 100);
                frame_decode_ticks = 0;
                /* Service period includes input/feed/decode/handoff and LCD
                 * backpressure. Exclude startup; this is not an LCD timestamp. */
                if (decoded) frame_timing_add(&service_timing, (acknowledged - previous_ack) / 100);
                previous_ack = acknowledged;
#endif
                ++decoded;
            }
            if (result == MPEG_NEED_INPUT) {
                result = take_input(c_load, &in);
            } else if (result == MPEG_END) {
                break;
            }
        }
        /* After a failure, release any held chunk and refuse further ones until
         * the loader's stop arrives, so both sides finish the play in step. */
        if (in.held) {
            unsigned ack = VIDEO_LOADER_ACK(in.held_sequence, 1);
            pipeline_send(c_load, &ack, 1);
        }
        while (!in.stopped) {
            unsigned ready[3];
            pipeline_receive(c_load, ready, 3);
            if (ready[0] == PIPELINE_STOP) {
                in.stopped = 1;
            } else {
                unsigned ack = VIDEO_LOADER_ACK(ready[1], 1);
                pipeline_send(c_load, &ack, 1);
            }
        }
        unsigned stop[5] = {PIPELINE_STOP, 0, 0, 0, 0};
        pipeline_send(c_image, stop, 5);
        pipeline_receive(c_image, stats, PIPELINE_STATS_WORDS);
        if (opened) {
            printf("MPEG input: capacity=%u high_water=%u bytes; arena=%u bytes\n",
                   (unsigned)mpeg_input_capacity(), in.input_high_water, (unsigned)sizeof(video_memory));
            mpeg_memory_stats_t memory = mpeg_memory_stats();
            stats[MPEG_ARENA_PEAK] = memory.peak;
            if (!memory.guards_ok) ++stats[PIPELINE_ERRORS];
            mpeg_close();
            stats[MPEG_ARENA_LIVE] = mpeg_memory_stats().live;
        }
        if (result != MPEG_END || !decoded || stats[MPEG_ARENA_LIVE] || stats[PIPELINE_FRAMES] != decoded)
            ++stats[PIPELINE_ERRORS];
        if (MPEG_PACED && decoded)
            printf("Video paced to audio: %u started over 10 ms late, worst %u ms\n",
                   in.late_pictures, in.latest_ticks / TICKS_PER_MS);
        stats[PIPELINE_ERRORS] += in.protocol_errors;
        if (stats[PIPELINE_ERRORS]) activity_led_decode(ACTIVITY_LED_ERROR);
        stats[PIPELINE_DECODE_TICKS] = decode_us;
        stats[PIPELINE_TRANSFER_TICKS] = transfer_us;
        stats[MPEG_FEED_US] = in.feed_us;
        stats[MPEG_INPUT_WAIT_US] = in.wait_us;
        stats[MPEG_SEND_US] = send_us;
        stats[MPEG_ACK_WAIT_US] = ack_wait_us;
        stats[MPEG_DECODED_FRAMES] = decoded;
        stats[MPEG_DECODE_STATUS] = (unsigned)result;
#if APP_XUH_MSC_PLAYER_FRAME_TIMING
        /* All rendering has drained. Never print per-frame or inside decode. */
        report_frame_timing("decode", &decode_timing);
        report_frame_timing("service", &service_timing);
#endif
        pipeline_send(c_load, stats, MPEG_STATS_WORDS);
    }
}

static void record_render(unsigned stats[], unsigned ack[], unsigned expected)
{
    if (ack[0] != expected) ++stats[PIPELINE_ERRORS];
    stats[PIPELINE_ERRORS] += ack[2];
    stats[PIPELINE_RENDER_TICKS] += ack[1] / 100;
    ++stats[PIPELINE_FRAMES];
    unsigned now = get_reference_time();
    if (stats[PIPELINE_FRAMES] == 1) stats[PIPELINE_FIRST_COMPLETE] = now;
    stats[PIPELINE_LAST_COMPLETE] = now;
}

static void receive_play(chanend c_image, chanend c_render)
{
    unsigned expected = 0, pending = 0, ack[3], stats[PIPELINE_STATS_WORDS] = {0};
#if DECODE_LEAD_ENABLED
    decode_lead_clock_t video_clock = {0};
#endif
#if APP_XUH_MSC_PLAYER_FPS_OVERLAY
    fps_overlay_t fps = {0};    /* Per play: the first window restarts with it. */
#endif
#if AUDIO_METER_ENABLED
    audio_meter_t meter = {{0}};
#endif
    for (;;) {
        unsigned sequence = chanend_in_word(c_image);
        unsigned width = chanend_in_word(c_image);
        unsigned height = chanend_in_word(c_image);
        unsigned rate_num = chanend_in_word(c_image);
        unsigned rate_den = chanend_in_word(c_image);
        if (sequence == PIPELINE_STOP) {
            chanend_check_control_token(c_image, XS1_CT_END);
            break;
        }
        if (!video_window_valid(width, height) || !rate_num || rate_num > 60000 ||
            !rate_den || rate_den > 1001 || rate_num > 60 * rate_den) __builtin_trap();
        if (sequence != expected) ++stats[PIPELINE_ERRORS];
        ++expected;
        unsigned slot = sequence & 1;
#if DECODE_LEAD_ENABLED
        decode_lead_advance(&video_clock, rate_den, rate_num);
        lcd_video_ms[slot] = video_clock.ms;
#endif
        video_window_t window = video_window_center(width, height);
        /* A slot is owned by this receiver until published to the renderer.
         * Clear stale borders only when its geometry changes, not per frame. */
        if (lcd_windows[slot].width != width || lcd_windows[slot].height != height)
            memset(lcd_frames[slot], 0, sizeof(lcd_frames[slot]));
        lcd_windows[slot] = window;
        unsigned receive_ticks = 0, convert_ticks = 0;
        for (unsigned y = 0; y < height; y += 2) {
            unsigned start = get_reference_time();
            for (unsigned index = 0; index < width * 3 / 4; ++index)
                strip[index] = chanend_in_word(c_image);
            /* Keep strip writes and conversion on their respective sides of
             * this timing boundary. Reception includes blocked channel time. */
            asm volatile("" ::: "memory");
            unsigned received = get_reference_time();
            asm volatile("" ::: "memory");
            /* The strip holds Y row, Y row, Cb row, Cr row without padding. */
            const unsigned char *rows = (const unsigned char *)strip;
            unsigned char *output = lcd_frames[slot] + video_window_rgb_offset(window, y);
            mpeg_yuv420_rows_to_rgb565(rows, rows + width, rows + 2 * width,
                                        rows + 2 * width + width / 2,
                                        output, output + PIPELINE_LCD_WIDTH * 2, width);
            asm volatile("" ::: "memory");
            unsigned converted = get_reference_time();
            receive_ticks += received - start;
            convert_ticks += converted - received;
        }
        chanend_check_control_token(c_image, XS1_CT_END);
        /* Convert units once per frame, not per strip, to avoid accumulating
         * fractional-microsecond truncation over 120 strips. Header/END,
         * overlay work and renderer/ack waits are outside these two totals. */
        stats[PIPELINE_RECEIVE_TICKS] += receive_ticks / 100;
        stats[PIPELINE_CONVERT_TICKS] += convert_ticks / 100;
#if APP_XUH_MSC_PLAYER_FPS_OVERLAY
        fps.target_tenths = (rate_num * 10 + rate_den / 2) / rate_den;
        fps_overlay_frame(&fps, get_reference_time(), lcd_frames[slot]);
#endif
#if AUDIO_METER_ENABLED
        audio_meter_frame(&meter, lcd_frames[slot]);
#endif
#if PIPELINE_VERIFY
        lcd_hash[slot] = hash_bytes(lcd_frames[slot], sizeof(lcd_frames[slot]));
#endif
        /* YUV is now fully consumed. Release tile 1 before waiting for LCD;
         * the next send will backpressure until this RGB slot is published.
         * Sequence zero retains its first-display-complete handshake below. */
        if (sequence) pipeline_send(c_image, &sequence, 1);
        /* The alternate framebuffer was rendered while this one was filled.
         * Receive completion before publishing the next renderer request. */
        if (pending) {
            unsigned start = get_reference_time();
            pipeline_receive(c_render, ack, 3);
            stats[PIPELINE_RENDER_WAIT_TICKS] += (get_reference_time() - start) / 100;
            record_render(stats, ack, sequence - 1);
        }
        unsigned ready[2] = {slot, sequence};
        pipeline_send(c_render, ready, 2);
        pending = 1;
        if (!sequence) {
            pipeline_receive(c_render, ack, 3);
            record_render(stats, ack, sequence);
            pending = 0;
            pipeline_send(c_image, &sequence, 1);
        }
    }
    if (pending) {
        pipeline_receive(c_render, ack, 3);
        record_render(stats, ack, expected - 1);
    }
    unsigned stop[2] = {PIPELINE_STOP, 0};
    pipeline_send(c_render, stop, 2);
    pipeline_send(c_image, stats, PIPELINE_STATS_WORDS);
}

void mpeg_pipeline_receive(chanend c_image, chanend c_render)
{
    for (;;) receive_play(c_image, c_render);
}
#endif

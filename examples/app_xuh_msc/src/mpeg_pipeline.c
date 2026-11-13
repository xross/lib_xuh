#include "mpeg_pipeline.h"
#include "video_loader.h"

#if MSC_MPEG_PIPELINE
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <xcore/hwtimer.h>
#include "mpeg.h"
#include "mpeg_rgb565.h"
#include "fps_overlay.h"

/* Tile 1: two filesystem input chunks (one being read ahead while the other is
 * fed), plus lib_mpeg's static arena.
 * Tile 0: two RGB565 framebuffers and one two-row YUV strip. No RGB888 frame. */
static unsigned char input[2][MPEG_LOAD_BYTES] __attribute__((aligned(4)));
static unsigned char lcd_frames[2][PIPELINE_LCD_WIDTH * PIPELINE_LCD_HEIGHT * 2] __attribute__((aligned(4)));
static unsigned strip[PIPELINE_LCD_WIDTH * 3 / 4];
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
static void send_frame(chanend c_image, const mpeg_frame_t *frame)
{
    asm volatile("" ::: "memory");
    chanend_out_word(c_image, frame->number);
    chanend_out_word(c_image, frame->width);
    chanend_out_word(c_image, frame->height);
    /* Transfer two Y rows plus their Cb/Cr row. Decoder-owned planes remain
     * valid until tile 0 acknowledges staging; only then may decode continue. */
    for (unsigned y = 0; y < frame->height; y += 2) {
        send_row(c_image, frame->plane[0] + y * frame->stride[0], frame->width);
        send_row(c_image, frame->plane[0] + (y + 1) * frame->stride[0], frame->width);
        send_row(c_image, frame->plane[1] + (y / 2) * frame->stride[1], frame->width / 2);
        send_row(c_image, frame->plane[2] + (y / 2) * frame->stride[2], frame->width / 2);
    }
    chanend_out_control_token(c_image, XS1_CT_END);
    unsigned ack;
    pipeline_receive(c_image, &ack, 1);
    if (ack != frame->number) __builtin_trap();
}

/* As in the standalone benchmark: upstream indirect callback branches prevent
 * complete automatic stack sizing. Provisional 4096-word (16 KiB) reservation. */
#pragma stackfunction 4096
void mpeg_pipeline_decode(chanend c_load, chanend c_image)
{
    for (;;) {
        unsigned decoded = 0, decode_us = 0, feed_us = 0, transfer_us = 0, wait_us = 0;
        unsigned expected = 0, protocol_errors = 0, stopped = 0;
        /* The chunk currently owned by the decoder and how much is still unfed. */
        unsigned held = 0, held_sequence = 0, remaining = 0;
        const unsigned char *chunk = NULL;
        unsigned stats[MPEG_STATS_WORDS] = {0};
        mpeg_result_t result = mpeg_open();
        int opened = result >= 0;
        while (result >= 0) {
            mpeg_frame_t frame;
            unsigned start = get_reference_time();
            result = mpeg_next(&frame);
            decode_us += (get_reference_time() - start) / 100;
            if (result == MPEG_FRAME) {
                /* Fixed full-screen protocol; reject before publishing a frame. */
                if (frame.width != PIPELINE_LCD_WIDTH || frame.height != PIPELINE_LCD_HEIGHT ||
                    frame.number != decoded) { result = MPEG_UNSUPPORTED; break; }
                start = get_reference_time();
                send_frame(c_image, &frame);
                transfer_us += (get_reference_time() - start) / 100;
                ++decoded;
            } else if (result == MPEG_NEED_INPUT) {
                if (!remaining) {
                    /* Input is requested only after end of input if the
                     * decoder misbehaves; it should drain to MPEG_END. */
                    if (stopped) { result = MPEG_ERROR; break; }
                    unsigned ready[3];
                    start = get_reference_time();
                    /* Release the fully fed chunk only now. The loader reads
                     * ahead before it collects this ack, so sending it any
                     * earlier would block the decoder behind that read. */
                    if (held) {
                        unsigned ack[2] = {held_sequence, 0};
                        pipeline_send(c_load, ack, 2);
                        held = 0;
                    }
                    pipeline_receive(c_load, ready, 3);
                    wait_us += (get_reference_time() - start) / 100;
                    if (ready[0] == PIPELINE_STOP) {
                        stopped = 1;
                        if (ready[1]) { result = MPEG_ERROR; break; }
                        result = mpeg_feed(NULL, 0, 1);
                        continue;
                    }
                    held = 1;
                    held_sequence = ready[1];
                    if (ready[0] != (expected & 1) || ready[1] != expected ||
                        !ready[2] || ready[2] > MPEG_LOAD_BYTES) {
                        ++protocol_errors;
                        result = MPEG_ERROR;
                        break;
                    }
                    ++expected;
                    chunk = input[ready[0]];
                    remaining = ready[2];
                }
                /* A chunk larger than the free input space is fed in parts. */
                unsigned space = mpeg_input_space();
                if (!space) { result = MPEG_CAPACITY; break; }
                unsigned count = remaining < space ? remaining : space;
                start = get_reference_time();
                result = mpeg_feed(chunk, count, 0);
                feed_us += (get_reference_time() - start) / 100;
                chunk += count;
                remaining -= count;
            } else if (result == MPEG_END) {
                break;
            }
        }
        /* After a failure, release any held chunk and refuse further ones until
         * the loader's stop arrives, so both sides finish the play in step. */
        if (held) {
            unsigned ack[2] = {held_sequence, 1};
            pipeline_send(c_load, ack, 2);
        }
        while (!stopped) {
            unsigned ready[3];
            pipeline_receive(c_load, ready, 3);
            if (ready[0] == PIPELINE_STOP) {
                stopped = 1;
            } else {
                unsigned ack[2] = {ready[1], 1};
                pipeline_send(c_load, ack, 2);
            }
        }
        unsigned stop[3] = {PIPELINE_STOP, 0, 0};
        pipeline_send(c_image, stop, 3);
        pipeline_receive(c_image, stats, PIPELINE_STATS_WORDS);
        if (opened) {
            mpeg_memory_stats_t memory = mpeg_memory_stats();
            stats[MPEG_ARENA_PEAK] = memory.peak;
            if (!memory.guards_ok) ++stats[PIPELINE_ERRORS];
            mpeg_close();
            stats[MPEG_ARENA_LIVE] = mpeg_memory_stats().live;
        }
        if (result != MPEG_END || !decoded || stats[MPEG_ARENA_LIVE] || stats[PIPELINE_FRAMES] != decoded)
            ++stats[PIPELINE_ERRORS];
        stats[PIPELINE_ERRORS] += protocol_errors;
        stats[PIPELINE_DECODE_TICKS] = decode_us;
        stats[PIPELINE_TRANSFER_TICKS] = transfer_us;
        stats[MPEG_FEED_US] = feed_us;
        stats[MPEG_INPUT_WAIT_US] = wait_us;
        stats[MPEG_DECODED_FRAMES] = decoded;
        stats[MPEG_DECODE_STATUS] = (unsigned)result;
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
#if APP_XUH_MSC_FPS_OVERLAY
    fps_overlay_t fps = {0};    /* Per play: the first window restarts with it. */
#endif
    for (;;) {
        unsigned sequence = chanend_in_word(c_image);
        unsigned width = chanend_in_word(c_image);
        unsigned height = chanend_in_word(c_image);
        if (sequence == PIPELINE_STOP) {
            chanend_check_control_token(c_image, XS1_CT_END);
            break;
        }
        if (width != PIPELINE_LCD_WIDTH || height != PIPELINE_LCD_HEIGHT) __builtin_trap();
        if (sequence != expected) ++stats[PIPELINE_ERRORS];
        ++expected;
        unsigned slot = sequence & 1;
        unsigned receive_ticks = 0, convert_ticks = 0;
        for (unsigned y = 0; y < height; y += 2) {
            unsigned start = get_reference_time();
            for (unsigned index = 0; index < sizeof(strip) / sizeof(strip[0]); ++index)
                strip[index] = chanend_in_word(c_image);
            /* Keep strip writes and conversion on their respective sides of
             * this timing boundary. Reception includes blocked channel time. */
            asm volatile("" ::: "memory");
            unsigned received = get_reference_time();
            asm volatile("" ::: "memory");
            /* The strip holds Y row, Y row, Cb row, Cr row without padding. */
            const unsigned char *rows = (const unsigned char *)strip;
            unsigned char *output = lcd_frames[slot] + y * width * 2;
            mpeg_yuv420_rows_to_rgb565(rows, rows + width, rows + 2 * width,
                                        rows + 2 * width + width / 2,
                                        output, output + width * 2, width);
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
#if APP_XUH_MSC_FPS_OVERLAY
        fps_overlay_frame(&fps, get_reference_time(), lcd_frames[slot]);
#endif
#if PIPELINE_VERIFY
        lcd_hash[slot] = hash_bytes(lcd_frames[slot], sizeof(lcd_frames[slot]));
#endif
        /* The alternate framebuffer was rendered while this one was filled.
         * Receive completion before publishing the next renderer request. */
        if (pending) {
            pipeline_receive(c_render, ack, 3);
            record_render(stats, ack, sequence - 1);
        }
        unsigned ready[2] = {slot, sequence};
        pipeline_send(c_render, ready, 2);
        pending = 1;
        if (!sequence) {
            pipeline_receive(c_render, ack, 3);
            record_render(stats, ack, sequence);
            pending = 0;
        }
        pipeline_send(c_image, &sequence, 1);
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

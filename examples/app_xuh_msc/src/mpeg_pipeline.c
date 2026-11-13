#include "mpeg_pipeline.h"

#if MSC_MPEG_PIPELINE
#include <stdio.h>
#include <string.h>
#include <xcore/hwtimer.h>
#include "mpeg.h"
#include "yuv_rgb565.h"

/* Tile 1: one reusable filesystem input chunk, plus lib_mpeg's static arena.
 * Tile 0: two RGB565 framebuffers and one two-row YUV strip. No RGB888 frame. */
static unsigned char input[MPEG_LOAD_BYTES] __attribute__((aligned(4)));
static unsigned char lcd_frames[2][PIPELINE_LCD_WIDTH * PIPELINE_LCD_HEIGHT * 2];
static unsigned char strip[PIPELINE_LCD_WIDTH * 3] __attribute__((aligned(4)));
#if PIPELINE_VERIFY
static unsigned lcd_hash[2];
static unsigned hash_bytes(const unsigned char *data, unsigned count)
{
    unsigned hash = 2166136261u;
    for (unsigned i = 0; i < count; ++i) hash = (hash ^ data[i]) * 16777619u;
    return hash;
}
#endif

unsigned char *mpeg_pipeline_input_buffer(void) { return input; }
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
    for (unsigned offset = 0; offset < bytes; offset += 4) {
        unsigned word;
        memcpy(&word, row + offset, sizeof(word));
        chanend_out_word(c_image, word);
    }
}

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
        unsigned begin[3], decoded = 0, decode_us = 0, feed_us = 0, transfer_us = 0;
        unsigned stats[MPEG_STATS_WORDS] = {0};
        pipeline_receive(c_load, begin, 3);
        mpeg_result_t result = MPEG_ERROR;
        int opened = !begin[1];
        if (opened) result = mpeg_open();
        while (opened && result >= 0) {
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
                unsigned space = mpeg_input_space();
                if (!space) { result = MPEG_CAPACITY; break; }
                unsigned request[2] = {MPEG_REQUEST_INPUT, space < MPEG_LOAD_BYTES ? space : MPEG_LOAD_BYTES};
                unsigned response[3];
                pipeline_send(c_load, request, 2);
                pipeline_receive(c_load, response, 3);
                if (response[2] || response[0] > request[1]) { result = MPEG_ERROR; break; }
                start = get_reference_time();
                result = mpeg_feed(input, response[0], response[1]);
                feed_us += (get_reference_time() - start) / 100;
            } else if (result == MPEG_END) {
                break;
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
        stats[PIPELINE_DECODE_TICKS] = decode_us;
        stats[PIPELINE_TRANSFER_TICKS] = transfer_us;
        stats[MPEG_FEED_US] = feed_us;
        stats[MPEG_DECODED_FRAMES] = decoded;
        stats[MPEG_DECODE_STATUS] = (unsigned)result;
        unsigned complete[2] = {MPEG_PLAY_COMPLETE, decoded};
        pipeline_send(c_load, complete, 2);
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
        unsigned start = get_reference_time();
        for (unsigned y = 0; y < height; y += 2) {
            for (unsigned offset = 0; offset < sizeof(strip); offset += 4) {
                unsigned word = chanend_in_word(c_image);
                memcpy(strip + offset, &word, sizeof(word));
            }
            yuv420_strip_to_rgb565(strip, lcd_frames[slot] + y * width * 2, width);
        }
        chanend_check_control_token(c_image, XS1_CT_END);
        stats[PIPELINE_CONVERT_TICKS] += (get_reference_time() - start) / 100;
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

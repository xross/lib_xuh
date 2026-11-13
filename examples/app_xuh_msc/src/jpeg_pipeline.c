#include "jpeg_pipeline.h"
#include "video_loader.h"

#if MSC_JPEG_PIPELINE
#include <stdio.h>
#include <string.h>
#include <xcore/hwtimer.h>
#include "jpeg.h"

/* Tile 1: double-buffered compressed input, single full-screen RGB output.
 * Only the loader writes input, only the decoder writes RGB.
 * Slot ownership is transferred by the ready/free channel messages. */
static unsigned char jpeg_input[2][PIPELINE_INPUT_BYTES] __attribute__((aligned(4)));
static unsigned char jpeg_rgb[PIPELINE_RGB_BYTES] __attribute__((aligned(4)));
static unsigned char jpeg_work[JPEG_DECODE_WORK_SIZE] __attribute__((aligned(4)));

/* Tile 0: the receiver fills one slot while the LCD task renders the other. */
static unsigned char lcd_frames[2][PIPELINE_LCD_HEIGHT * PIPELINE_LCD_WIDTH * 2];

#if PIPELINE_VERIFY
static unsigned rgb_hash[2], lcd_hash[2];

static unsigned frame_hash(const unsigned char *data, unsigned size)
{
    unsigned hash = 2166136261u;
    for (unsigned i = 0; i < size; ++i) hash = (hash ^ data[i]) * 16777619u;
    return hash;
}
#endif

unsigned jpeg_pipeline_check_lcd(unsigned slot)
{
#if PIPELINE_VERIFY
    return frame_hash(lcd_frames[slot], sizeof(lcd_frames[slot])) != lcd_hash[slot];
#else
    return 0;
#endif
}

VIDEO_SOURCE_BUFFER unsigned char *jpeg_pipeline_input_buffer(unsigned slot)
{
    return jpeg_input[slot];
}

unsigned char *jpeg_pipeline_lcd_buffer(unsigned slot)
{
    return lcd_frames[slot];
}

static void decode_play(chanend c_load, chanend c_output)
{
    unsigned expected = 0, pending = 0, decode_ticks = 0, errors = 0;
    unsigned ready[3], free_slot[1];
    while (1) {
        pipeline_receive(c_load, ready, 3);
        if (ready[0] == PIPELINE_STOP) break;
        unsigned slot = ready[0], sequence = ready[1], size = ready[2];
        if (sequence != expected || slot != (sequence & 1) || size > PIPELINE_INPUT_BYTES)
            ++errors;
        ++expected;

        /* There is one full-screen RGB buffer: wait for the transmitter to
         * release it before decoding. Compressed input remains double buffered. */
        if (pending) {
            pipeline_receive(c_output, free_slot, 1);
            if (free_slot[0] != sequence - 1) ++errors;
        }
        pending = 0;
        unsigned start = get_reference_time();
        jpeg_decode_info_t info = {0};
        memset(jpeg_rgb, 0, sizeof(jpeg_rgb));
        memset(jpeg_work, 0, sizeof(jpeg_work));
        jpeg_decode_result_t result = jpeg_decode(
            jpeg_input[slot], size, jpeg_rgb, sizeof(jpeg_rgb),
            jpeg_work, sizeof(jpeg_work), JPEG_DECODE_SCALE, &info);
        decode_ticks += (get_reference_time() - start) / 100;
        if (result == JPEG_DECODE_OK && info.channels == 3) {
#if PIPELINE_VERIFY
            unsigned hash = frame_hash(jpeg_rgb, PIPELINE_RGB_BYTES);
            rgb_hash[slot] = hash;
#endif
            unsigned output[5] = {slot, sequence, info.output_width,
                                  info.output_height, info.channels};
            pipeline_send(c_output, output, 5);
            pending = 1;
        } else {
            printf("JPEG pipeline: frame %u decode failed, result=%d\n", sequence, result);
            ++errors;
        }
        /* Input may now be overwritten; all decoding of it is finished. */
        unsigned ack[2] = {sequence, pending ? 0 : 1};
        pipeline_send(c_load, ack, 2);
    }

    if (pending) {
        pipeline_receive(c_output, free_slot, 1);
        if (free_slot[0] != expected - 1) ++errors;
    }
    unsigned stop[5] = {PIPELINE_STOP, 0, 0, 0, 0};
    pipeline_send(c_output, stop, 5);
    unsigned stats[PIPELINE_LOADER_STATS_WORDS] = {0};
    pipeline_receive(c_output, stats, PIPELINE_STATS_WORDS);
    stats[PIPELINE_DECODE_TICKS] = decode_ticks;
    stats[PIPELINE_ERRORS] += errors;
    pipeline_send(c_load, stats, PIPELINE_LOADER_STATS_WORDS);
}

static void transmit_play(chanend c_decode, chanend c_image)
{
    unsigned ready[5], ack[1], expected = 0, ticks = 0, first_complete = 0, errors = 0;
    while (1) {
        pipeline_receive(c_decode, ready, 5);
        if (ready[0] == PIPELINE_STOP) break;
        unsigned slot = ready[0], sequence = ready[1];
        unsigned width = ready[2], height = ready[3], channels = ready[4];
        if (sequence != expected || slot != (sequence & 1)) ++errors;
        ++expected;
        unsigned crop_width = width < PIPELINE_LCD_WIDTH ? width : PIPELINE_LCD_WIDTH;
        unsigned crop_height = height < PIPELINE_LCD_HEIGHT ? height : PIPELINE_LCD_HEIGHT;
        unsigned crop_x = (width - crop_width) / 2;
        unsigned crop_y = (height - crop_height) / 2;
#if PIPELINE_VERIFY
        if (frame_hash(jpeg_rgb, PIPELINE_RGB_BYTES) != rgb_hash[slot]) ++errors;
#endif

        unsigned start = get_reference_time();
        chanend_out_word(c_image, sequence);
        chanend_out_word(c_image, crop_width);
        chanend_out_word(c_image, crop_height);
        for (unsigned y = 0; y < crop_height; ++y) {
            const unsigned char *row = jpeg_rgb +
                ((crop_y + y) * width + crop_x) * channels;
            for (unsigned x = 0; x < crop_width; ++x) {
                chanend_out_byte(c_image, row[x * channels]);
                chanend_out_byte(c_image, row[x * channels + 1]);
                chanend_out_byte(c_image, row[x * channels + 2]);
            }
        }
        chanend_out_control_token(c_image, XS1_CT_END);
#if PIPELINE_VERIFY
        if (frame_hash(jpeg_rgb, PIPELINE_RGB_BYTES) != rgb_hash[slot]) ++errors;
#endif
        pipeline_receive(c_image, ack, 1);
        if (ack[0] != sequence) ++errors;
        if (sequence == 0) first_complete = get_reference_time();
        ticks += (get_reference_time() - start) / 100;
        /* Receiver no longer reads RGB; decoder can reclaim this slot. */
        pipeline_send(c_decode, ack, 1);
    }
    unsigned stop[3] = {PIPELINE_STOP, 0, 0};
    pipeline_send(c_image, stop, 3);
    unsigned stats[PIPELINE_STATS_WORDS];
    pipeline_receive(c_image, stats, PIPELINE_STATS_WORDS);
    stats[PIPELINE_FIRST_COMPLETE] = first_complete;
    stats[PIPELINE_LAST_COMPLETE] = get_reference_time();
    stats[PIPELINE_TRANSFER_TICKS] = ticks;
    stats[PIPELINE_ERRORS] += errors;
    pipeline_send(c_decode, stats, PIPELINE_STATS_WORDS);
}

static void receive_play(chanend c_image, chanend c_render)
{
    unsigned expected = 0, pending = 0, stats[PIPELINE_STATS_WORDS] = {0};
    unsigned ack[3];
    while (1) {
        unsigned sequence = chanend_in_word(c_image);
        unsigned width = chanend_in_word(c_image);
        unsigned height = chanend_in_word(c_image);
        if (sequence == PIPELINE_STOP) {
            chanend_check_control_token(c_image, XS1_CT_END);
            break;
        }
        if (sequence != expected) ++stats[PIPELINE_ERRORS];
        ++expected;
        unsigned slot = sequence & 1;
        unsigned x_offset = (PIPELINE_LCD_WIDTH - width) / 2;
        unsigned y_offset = (PIPELINE_LCD_HEIGHT - height) / 2;
        unsigned start = get_reference_time();
        memset(lcd_frames[slot], 0, sizeof(lcd_frames[slot]));
        for (unsigned y = 0; y < height; ++y) {
            for (unsigned x = 0; x < width; ++x) {
                unsigned red = chanend_in_byte(c_image);
                unsigned green = chanend_in_byte(c_image);
                unsigned blue = chanend_in_byte(c_image);
                unsigned pixel = ((red & 0xf8) << 8) | ((green & 0xfc) << 3) | (blue >> 3);
                unsigned offset = ((y_offset + y) * PIPELINE_LCD_WIDTH + x_offset + x) * 2;
                lcd_frames[slot][offset] = pixel >> 8;
                lcd_frames[slot][offset + 1] = pixel;
            }
        }
        chanend_check_control_token(c_image, XS1_CT_END);
        stats[PIPELINE_CONVERT_TICKS] += (get_reference_time() - start) / 100;
#if PIPELINE_VERIFY
        unsigned hash = frame_hash(lcd_frames[slot], sizeof(lcd_frames[slot]));
        lcd_hash[slot] = hash;
#endif
        if (pending) {
            pipeline_receive(c_render, ack, 3);
            if (ack[0] != sequence - 1) ++stats[PIPELINE_ERRORS];
            stats[PIPELINE_ERRORS] += ack[2];
            stats[PIPELINE_RENDER_TICKS] += ack[1] / 100;
            ++stats[PIPELINE_FRAMES];
        }
        unsigned ready[2] = {slot, sequence};
        pipeline_send(c_render, ready, 2);
        pending = 1;
        /* First acknowledgement measures actual startup-to-display latency.
         * Later frames acknowledge staging so conversion and SPI overlap. */
        if (sequence == 0) {
            pipeline_receive(c_render, ack, 3);
            if (ack[0] != sequence) ++stats[PIPELINE_ERRORS];
            stats[PIPELINE_ERRORS] += ack[2];
            stats[PIPELINE_RENDER_TICKS] += ack[1] / 100;
            ++stats[PIPELINE_FRAMES];
            pending = 0;
        }
        pipeline_send(c_image, &sequence, 1);
    }
    if (pending) {
        pipeline_receive(c_render, ack, 3);
        if (ack[0] != expected - 1) ++stats[PIPELINE_ERRORS];
        stats[PIPELINE_ERRORS] += ack[2];
        stats[PIPELINE_RENDER_TICKS] += ack[1] / 100;
        ++stats[PIPELINE_FRAMES];
    }
    unsigned stop[2] = {PIPELINE_STOP, 0};
    pipeline_send(c_render, stop, 2);
    pipeline_send(c_image, stats, PIPELINE_STATS_WORDS);
}

/* A stop message ends one play, not the lifetime of the pipeline tasks. */
void jpeg_pipeline_decode(chanend c_load, chanend c_output)
{
    while (1) decode_play(c_load, c_output);
}

void jpeg_pipeline_transmit(chanend c_decode, chanend c_image)
{
    while (1) transmit_play(c_decode, c_image);
}

void jpeg_pipeline_receive(chanend c_image, chanend c_render)
{
    while (1) receive_play(c_image, c_render);
}
#endif

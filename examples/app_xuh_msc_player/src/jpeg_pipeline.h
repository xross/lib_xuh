#ifndef JPEG_PIPELINE_H_
#define JPEG_PIPELINE_H_

#include <xccompat.h>
#include "video_config.h"
#define PIPELINE_STOP (~0u)
#define PIPELINE_INPUT_BYTES (512 * 60)
#define JPEG_INPUT_WIDTH 320
#define JPEG_INPUT_HEIGHT 240
#define JPEG_DECODE_SCALE 0
#define JPEG_OUTPUT_WIDTH (JPEG_INPUT_WIDTH >> JPEG_DECODE_SCALE)
#define JPEG_OUTPUT_HEIGHT (JPEG_INPUT_HEIGHT >> JPEG_DECODE_SCALE)
#define PIPELINE_RGB_BYTES (JPEG_OUTPUT_WIDTH * JPEG_OUTPUT_HEIGHT * 3)

/* Development-only per-buffer integrity checks during transmit/render.
 * Disabled for timing runs; enable with -DPIPELINE_VERIFY=1. */
#ifndef PIPELINE_VERIFY
#define PIPELINE_VERIFY 0
#endif

enum {
    PIPELINE_FRAMES,
    PIPELINE_FIRST_COMPLETE,
    PIPELINE_LAST_COMPLETE,
    /* Stage totals below are microseconds, not raw timer ticks. */
    PIPELINE_TRANSFER_TICKS,
    PIPELINE_RECEIVE_TICKS, /* MPEG YUV strip reception, including channel waits. */
    /* MPEG: conversion only. JPEG: combined reception and conversion. */
    PIPELINE_CONVERT_TICKS,
    PIPELINE_RENDER_TICKS,
    /* MPEG receiver blocked on the previous LCD frame, excluding startup/drain. */
    PIPELINE_RENDER_WAIT_TICKS,
    PIPELINE_DECODE_TICKS,
    PIPELINE_ERRORS,
    PIPELINE_STATS_WORDS
};
/* Statistics returned to the loader: the words above plus decoder-specific
 * words (see mpeg_pipeline.h); a decoder without any sends zeros. */
#define PIPELINE_LOADER_STATS_WORDS (PIPELINE_STATS_WORDS + 8)

#if MSC_JPEG_PIPELINE
void jpeg_pipeline_decode(chanend c_load, chanend c_output);
void jpeg_pipeline_transmit(chanend c_decode, chanend c_image);
void jpeg_pipeline_receive(chanend c_image, chanend c_render);
unsigned jpeg_pipeline_check_lcd(unsigned slot);

#ifdef __XC__
unsigned char * unsafe jpeg_pipeline_lcd_buffer(unsigned slot);
#else
unsigned char *jpeg_pipeline_lcd_buffer(unsigned slot);
unsigned char *jpeg_pipeline_input_buffer(unsigned slot);
#endif
#endif

#if MSC_VIDEO_PIPELINE && !defined(__XC__)
#include <xs1.h>
#include <xcore/chanend.h>

/* END releases every route. Reverse messages explicitly return ownership.
 * A producer may fill the alternate slot while its consumer owns this slot,
 * but must receive that consumer's completion before publishing another slot.
 * Buffer pointers are used only by tasks on the same tile. */
static inline void pipeline_send(chanend c, const unsigned *words, unsigned count)
{
    /* Raw chanend instructions are volatile but have no memory clobber.
     * Publish all shared-buffer writes before transferring ownership. */
    asm volatile("" ::: "memory");
    for (unsigned i = 0; i < count; ++i) chanend_out_word(c, words[i]);
    chanend_out_control_token(c, XS1_CT_END);
    asm volatile("" ::: "memory");
}

static inline void pipeline_receive(chanend c, unsigned *words, unsigned count)
{
    asm volatile("" ::: "memory");
    for (unsigned i = 0; i < count; ++i) words[i] = chanend_in_word(c);
    chanend_check_control_token(c, XS1_CT_END);
    asm volatile("" ::: "memory");
}
#endif
#endif

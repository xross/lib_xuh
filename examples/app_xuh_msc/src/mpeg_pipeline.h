#ifndef MPEG_PIPELINE_H_
#define MPEG_PIPELINE_H_
#include "jpeg_pipeline.h"

enum { MPEG_LOAD_BYTES = 4096, MPEG_REQUEST_INPUT = 1, MPEG_PLAY_COMPLETE = 2,
       MPEG_DECODED_FRAMES = PIPELINE_STATS_WORDS, MPEG_FEED_US,
       MPEG_ARENA_PEAK, MPEG_ARENA_LIVE, MPEG_DECODE_STATUS, MPEG_STATS_WORDS };

/* Loader -> decoder: begin [play, open_rc, 0], then input responses
 * [bytes, end_of_input, read_rc]. Decoder -> loader: [INPUT, capacity] or
 * [COMPLETE, decoded_frames] followed by MPEG_STATS_WORDS statistics.
 * The shared input chunk is owned by the loader only between a request and
 * its response. No pointers cross tiles. Every message terminates with CT_END. */
#if MSC_MPEG_PIPELINE
void mpeg_pipeline_decode(chanend c_load, chanend c_image);
void mpeg_pipeline_receive(chanend c_image, chanend c_render);
unsigned mpeg_pipeline_check_lcd(unsigned slot);
#ifdef __XC__
unsigned char * unsafe mpeg_pipeline_lcd_buffer(unsigned slot);
#else
unsigned char *mpeg_pipeline_lcd_buffer(unsigned slot);
unsigned char *mpeg_pipeline_input_buffer(void);
#endif
#endif
#endif

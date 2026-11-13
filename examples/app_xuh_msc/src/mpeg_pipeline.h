#ifndef MPEG_PIPELINE_H_
#define MPEG_PIPELINE_H_
#include "jpeg_pipeline.h"

/* MPEG_INPUT_WAIT_US is the time the decoder spent blocked waiting for the
 * loader: with read-ahead it, not the loader's own read time, is what storage
 * costs the frame period. */
enum { MPEG_LOAD_BYTES = 4096,
       MPEG_DECODED_FRAMES = PIPELINE_STATS_WORDS, MPEG_FEED_US,
       MPEG_ARENA_PEAK, MPEG_ARENA_LIVE, MPEG_DECODE_STATUS, MPEG_INPUT_WAIT_US,
       MPEG_STATS_WORDS };
#ifndef __XC__
_Static_assert(MPEG_STATS_WORDS == PIPELINE_LOADER_STATS_WORDS, "loader statistics size");
#endif

/* The loader protocol is the shared one in video_loader.h; a unit is one chunk
 * of at most MPEG_LOAD_BYTES of the elementary stream. The decoder copies a
 * chunk into lib_mpeg's input buffer, possibly in several feeds, and
 * acknowledges it once fully fed. Two chunk slots let the loader read ahead.
 * No pointers cross tiles. Every message terminates with CT_END. */
#if MSC_MPEG_PIPELINE
void mpeg_pipeline_decode(chanend c_load, chanend c_image);
void mpeg_pipeline_receive(chanend c_image, chanend c_render);
unsigned mpeg_pipeline_check_lcd(unsigned slot);
#ifdef __XC__
unsigned char * unsafe mpeg_pipeline_lcd_buffer(unsigned slot);
#else
unsigned char *mpeg_pipeline_lcd_buffer(unsigned slot);
unsigned char *mpeg_pipeline_input_buffer(unsigned slot);
#endif
#endif
#endif

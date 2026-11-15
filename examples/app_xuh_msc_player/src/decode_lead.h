#ifndef DECODE_LEAD_H_
#define DECODE_LEAD_H_
#include "video_config.h"

#ifndef APP_XUH_MSC_PLAYER_DECODE_LEAD
#define APP_XUH_MSC_PLAYER_DECODE_LEAD 1
#endif
#ifndef APP_XUH_MSC_PLAYER_MPEG_AUDIO
#define APP_XUH_MSC_PLAYER_MPEG_AUDIO 0
#endif
#ifndef APP_XUH_MSC_PLAYER_AUDIO_DECODE
#define APP_XUH_MSC_PLAYER_AUDIO_DECODE 1
#endif
#define DECODE_LEAD_ENABLED (MSC_MPEG_PIPELINE && APP_XUH_MSC_PLAYER_MPEG_AUDIO && \
    APP_XUH_MSC_PLAYER_AUDIO_DECODE && APP_XUH_MSC_PLAYER_DECODE_LEAD)

/* Separate packed RGB565 rectangle: never modifies an in-flight video slot. */
enum { DECODE_LEAD_WIDTH = 100, DECODE_LEAD_HEIGHT = 22,
       DECODE_LEAD_X = (PIPELINE_LCD_WIDTH - DECODE_LEAD_WIDTH) / 2,
       DECODE_LEAD_Y = 4, DECODE_LEAD_RANGE_MS = 1000 };

typedef struct {
    unsigned ms, remainder, rate;
} decode_lead_clock_t;

#if DECODE_LEAD_ENABLED
#ifndef __XC__
/* Accumulate media duration without rounding every block/frame. units is a
 * sample count or frame-rate denominator; rate is samples/s or numerator.
 * Both must be nonzero and units * 1000 + rate - 1 must fit an unsigned word.
 * Reset the clock to zero for each play. A rate change drops sub-ms residue. */
void decode_lead_advance(decode_lead_clock_t *state, unsigned units, unsigned rate);

/* One audio-task writer, one LCD-task reader on the same tile. A single word
 * publishes validity and elapsed media ms together. STOP retains the last
 * position while video drains; START and decoder errors invalidate it. */
void decode_lead_publish(unsigned ms, unsigned valid);
unsigned decode_lead_read(unsigned *ms);

/* Signed difference modulo 2^31 ms; unambiguous within +/- 12.4 days. */
int decode_lead_difference(unsigned audio_ms, unsigned video_ms);
#endif

/* LCD task: draw after transferring the picture. video_ms is the END of the
 * displayed frame, matching the end of the last decoded audio block. No PTS
 * offsets, audio playback clock or panel-scan timing are measured. */
#ifdef __XC__
void decode_lead_draw(unsigned char * unsafe pixels, unsigned video_ms);
#else
void decode_lead_draw(unsigned char *pixels, unsigned video_ms);
#endif
#endif
#endif

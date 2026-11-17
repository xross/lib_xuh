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
#ifndef APP_XUH_MSC_PLAYER_AUDIO_PACED
#define APP_XUH_MSC_PLAYER_AUDIO_PACED 1
#endif
#define DECODE_LEAD_ENABLED (MSC_MPEG_PIPELINE && APP_XUH_MSC_PLAYER_MPEG_AUDIO && \
    APP_XUH_MSC_PLAYER_AUDIO_DECODE && APP_XUH_MSC_PLAYER_DECODE_LEAD)

/* Separate packed RGB565 rectangle: never modifies an in-flight video slot. */
/* Keep the original rectangle/transfer cost in maximum-speed builds. With the
 * underflow footer it ends at row 29, the last row above a 180-row picture
 * centred on the 240-row display. */
enum { DECODE_LEAD_WIDTH = 100,
       DECODE_LEAD_HEIGHT = APP_XUH_MSC_PLAYER_AUDIO_PACED ? 26 : 22,
       DECODE_LEAD_X = (PIPELINE_LCD_WIDTH - DECODE_LEAD_WIDTH) / 2,
       DECODE_LEAD_Y = 4,
       /* Paced, the gauge shows sync: the bar spans +/-60 ms, amber beyond
        * 15 ms and red beyond 45 ms either way. Unpaced, it shows decode lead,
        * which ranges much further. */
       DECODE_LEAD_RANGE_MS = APP_XUH_MSC_PLAYER_AUDIO_PACED ? 60 : 1000,
       DECODE_LEAD_AMBER_MS = APP_XUH_MSC_PLAYER_AUDIO_PACED ? 15 : 40,
       DECODE_LEAD_RED_MS = APP_XUH_MSC_PLAYER_AUDIO_PACED ? 45 : 100,
       /* A tick stays at the largest reading of this many pictures. */
       DECODE_LEAD_PEAK_PICTURES = 50 };

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

/* Audio task publishes confirmed underflow episodes and missing-audio ms
 * (DAC silence or stand-in hold; the historical parameter name is retained).
 * Two independent tile-local words: approximate display, never clock control. */
void decode_lead_underflows_publish(unsigned count, unsigned held_ms);
void decode_lead_underflows_read(unsigned *count, unsigned *held_ms);

/* Signed difference modulo 2^31 ms; unambiguous within +/- 12.4 days. */
int decode_lead_difference(unsigned audio_ms, unsigned video_ms);
#endif

/* LCD task: draw after transferring the picture. When paced, video_ms is the
 * START of the picture just written and the audio position is the audio played,
 * both on the stream's timeline: zero means the picture reached the display as
 * its audio played. Otherwise video_ms is the END of the picture and the audio
 * position the end of the last decoded block. Panel scan-out is not measured. */
#ifdef __XC__
void decode_lead_draw(unsigned char * unsafe pixels, unsigned video_ms);
#else
void decode_lead_draw(unsigned char *pixels, unsigned video_ms);
#endif

/* LCD task, at the end of a play: print the mean, least and greatest lead of the
 * pictures drawn while the audio position was valid, and start a new summary.
 * Prints nothing if there were none. Not called while pictures are rendered. */
void decode_lead_report(void);
#endif
#endif

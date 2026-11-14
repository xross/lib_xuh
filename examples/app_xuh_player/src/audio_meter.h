#ifndef AUDIO_METER_H_
#define AUDIO_METER_H_
#include "video_config.h"

/* On-screen stereo level meter for the decoded MP2 audio, in the top-right
 * corner: a mirror of the FPS gauge. The left and right bars grow from the
 * right-hand edge towards the centre of the screen on a 48-dB logarithmic
 * scale, green when quiet through amber to red at full scale, each with a
 * white peak-hold tick.
 *
 * The audio is decoded but not played and not synchronised with the video:
 * the meter shows the level of the audio most recently decoded.
 *
 * Build with -DAPP_XUH_PLAYER_AUDIO_METER=0 to leave frames untouched. */
#ifndef APP_XUH_PLAYER_AUDIO_METER
#define APP_XUH_PLAYER_AUDIO_METER 1
#endif
#ifndef APP_XUH_PLAYER_MPEG_AUDIO
#define APP_XUH_PLAYER_MPEG_AUDIO 0
#endif
#ifndef APP_XUH_PLAYER_AUDIO_DECODE
#define APP_XUH_PLAYER_AUDIO_DECODE 1
#endif
#define AUDIO_METER_ENABLED (MSC_MPEG_PIPELINE && APP_XUH_PLAYER_MPEG_AUDIO && \
    APP_XUH_PLAYER_AUDIO_DECODE && APP_XUH_PLAYER_AUDIO_METER)

/* Fixed physical screen rectangle, never relative to a video window. */
enum { AUDIO_METER_WIDTH = 104, AUDIO_METER_HEIGHT = 22,
       AUDIO_METER_X = PIPELINE_LCD_WIDTH - 4 - AUDIO_METER_WIDTH, AUDIO_METER_Y = 4,
       AUDIO_METER_BAR_WIDTH = 88 };   /* Bar length at full scale, in pixels. */

typedef struct {
    unsigned shown[2];      /* Displayed bar length, left then right. */
    unsigned hold[2];       /* Peak-hold tick position. */
    unsigned hold_age[2];   /* Frames since the tick last rose. */
} audio_meter_t;

#if AUDIO_METER_ENABLED
/* Bar length, 0..AUDIO_METER_BAR_WIDTH, for a peak sample magnitude where 1.0
 * is full scale: 11 pixels for each halving, so about -48 dB is empty. */
unsigned audio_meter_level(float peak);

/* Audio task: publish the levels of the block just decoded. The meter and the
 * audio task are on one tile; nothing waits. */
void audio_meter_publish(unsigned left, unsigned right);

/* Display task, once per staged frame: take the latest published levels, apply
 * a fast rise and slow fall, and draw into a PIPELINE_LCD_WIDTH-wide big-endian
 * RGB565 frame. Reset by zeroing the state. */
void audio_meter_frame(audio_meter_t *state, unsigned char *frame);
#endif
#endif

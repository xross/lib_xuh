#ifndef APP_AUDIO_DAC_QUEUE_H_
#define APP_AUDIO_DAC_QUEUE_H_
#include "audio_dac.h"
#if APP_XUH_MSC_PLAYER_AUDIO_DAC

/* The PCM queue between the MP2 task and the I2S callback. Both run on the
 * DAC's tile and share the queue's memory; the callback never waits. Positions
 * and counts are stereo frames. */
enum { DAC_PLAYED, DAC_STARTED, DAC_UNDERFLOWS, DAC_MISSING,
       DAC_FIRST_UNDERFLOW, DAC_STATUS_WORDS };

/* Empty the queue for a new play, or after an error: output is silence until
 * PCM is written again. Waits for the I2S callback to take the request. */
void audio_dac_reset(void);
/* Queue as much of the PCM as fits and return the frames taken. Frames whose
 * time has already passed are taken and dropped. Zero means the queue is full. */
unsigned audio_dac_write(const int32_t *samples, unsigned frames);
/* No more PCM in this play: wait until the queued PCM has been played. */
void audio_dac_finish(void);
void audio_dac_status(unsigned status[DAC_STATUS_WORDS]);
#endif
#endif

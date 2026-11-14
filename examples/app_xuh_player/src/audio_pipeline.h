#ifndef APP_AUDIO_PIPELINE_H_
#define APP_AUDIO_PIPELINE_H_

#ifndef APP_XUH_PLAYER_MPEG_AUDIO
#define APP_XUH_PLAYER_MPEG_AUDIO 0
#endif
#ifndef APP_XUH_PLAYER_AUDIO_DECODE
#define APP_XUH_PLAYER_AUDIO_DECODE 1
#endif

#if APP_XUH_PLAYER_MPEG_AUDIO
#include <xccompat.h>
enum { AUDIO_START = 1, AUDIO_DATA = 2, AUDIO_STOP = 3,
       AUDIO_CHUNK_BYTES = 1024 };
enum { AUDIO_BYTES, AUDIO_BLOCKS, AUDIO_SAMPLES, AUDIO_RATE, AUDIO_DECODE_US,
       AUDIO_ERRORS, AUDIO_PEAK, AUDIO_CHECKSUM, AUDIO_STATS_WORDS };
void mp2_audio_task(chanend channel);
#ifndef __XC__
int audio_pipeline_start(chanend channel, unsigned decode);
/* Each bounded chunk is acknowledged after consumption, including bypass and
 * latched-error paths. Decoder errors are reported by audio_pipeline_stop(). */
int audio_pipeline_send(chanend channel, const unsigned char *data, unsigned bytes);
void audio_pipeline_stop(chanend channel, unsigned stats[AUDIO_STATS_WORDS]);
#endif
#endif
#endif

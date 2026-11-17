#ifndef APP_AUDIO_PIPELINE_H_
#define APP_AUDIO_PIPELINE_H_

#ifndef APP_XUH_MSC_PLAYER_MPEG_AUDIO
#define APP_XUH_MSC_PLAYER_MPEG_AUDIO 0
#endif
#ifndef APP_XUH_MSC_PLAYER_AUDIO_DECODE
#define APP_XUH_MSC_PLAYER_AUDIO_DECODE 1
#endif

/* 1: the audio task consumes decoded audio at its sample rate, standing in for
 * an audio output, and the player runs in real time against it. 0: audio is
 * decoded and discarded as it arrives and the player runs at full speed. */
#ifndef APP_XUH_MSC_PLAYER_AUDIO_PACED
#define APP_XUH_MSC_PLAYER_AUDIO_PACED 1
#endif

/* Tests can fail on the first confirmed output underflow. Normal playback
 * continues on the same clock, holding the last pair and discarding late PCM. */
#ifndef APP_XUH_MSC_PLAYER_PACING_STRICT
#define APP_XUH_MSC_PLAYER_PACING_STRICT 0
#endif

#if APP_XUH_MSC_PLAYER_MPEG_AUDIO
#include <xccompat.h>
enum { AUDIO_START = 1, AUDIO_DATA = 2, AUDIO_STOP = 3,
       AUDIO_CHUNK_BYTES = 1024 };
enum { AUDIO_BYTES, AUDIO_BLOCKS, AUDIO_SAMPLES, AUDIO_RATE, AUDIO_DECODE_US,
       AUDIO_ERRORS, AUDIO_PEAK, AUDIO_CHECKSUM,
       AUDIO_UNDERFLOWS, AUDIO_HELD_SAMPLES, AUDIO_DROPPED_SAMPLES,
       AUDIO_FIRST_UNDERFLOW, AUDIO_STATS_WORDS };
void mp2_audio_task(chanend channel);
#ifndef __XC__
int audio_pipeline_start(chanend channel, unsigned decode);
/* Each bounded chunk is acknowledged once accepted, including bypass and
 * latched-error paths. When paced, compressed audio waits in the decoder's input
 * buffer and a chunk is accepted only when there is room, so this blocks while
 * that buffer is full. Returns -1 on a decoder error or strict underflow; all
 * paths acknowledge the chunk so the caller can still close the protocol. */
int audio_pipeline_send(chanend channel, const unsigned char *data, unsigned bytes);
/* The sender's view of the paced audio clock, from the newest acknowledgement:
 * the audio played so far, in reference-timer ticks, is get_reference_time()
 * less *offset on the sender's tile. Once started it remains valid through
 * underflows. Returns 0 when unpaced, before startup, or on failure. */
int audio_pipeline_clock(unsigned *offset);
/* End input and drain buffered audio at its sample rate before returning.
 * Underflow counters exclude startup and the tail beyond normal audio EOF. */
void audio_pipeline_stop(chanend channel, unsigned stats[AUDIO_STATS_WORDS]);
#endif
#endif
#endif

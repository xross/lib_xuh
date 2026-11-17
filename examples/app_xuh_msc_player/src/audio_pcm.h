#ifndef APP_AUDIO_PCM_H_
#define APP_AUDIO_PCM_H_
#include <stdint.h>

enum { AUDIO_PCM_CAPACITY = 1024, AUDIO_PCM_PREFILL = 512 };

/* Single producer/single consumer, tile-local XS3 ordered SRAM. Word-sized
 * volatile publications plus compiler barriers transfer slot ownership. This
 * is not a portable host-thread synchronisation primitive. Native tests drive
 * the two sides sequentially at adversarial publication boundaries instead.
 * All positions are stereo frames (samples/channel), modulo 2^32; no operation
 * may be separated by half the position range. */
typedef struct {
    int32_t samples[AUDIO_PCM_CAPACITY][2];
    volatile unsigned delivered, played, started, eof, drained;
    volatile unsigned missing, underflows, first_underflow;
    unsigned gap, gap_start, gap_confirmed;
} audio_pcm_t;

/* Reset only while both sides are quiescent (the hardware mailbox enforces it).
 * Slots need not be cleared: startup/delivered prevent stale reads on replay. */
void audio_pcm_reset(audio_pcm_t *q);
/* Producer: consume up to count consecutive media frames, returning how many.
 * Late prefixes are discarded, never replayed. Zero means backpressure. */
unsigned audio_pcm_push(audio_pcm_t *q, const int32_t *samples, unsigned count);
/* Producer: no more media; drain known media, but keep the clock advancing
 * through silence so that video can finish. Provisional tail gaps don't count. */
void audio_pcm_finish(audio_pcm_t *q);
/* Consumer: one bounded, nonblocking I2S callback. Silence during startup and
 * starvation. Only this function writes the output position and gap counters. */
void audio_pcm_pop(audio_pcm_t *q, int32_t samples[2]);
#endif

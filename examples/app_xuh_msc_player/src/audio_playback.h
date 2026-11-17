#ifndef APP_AUDIO_PLAYBACK_H_
#define APP_AUDIO_PLAYBACK_H_
#include <stdint.h>

/* Output-clock accounting, independent of MP2 parsing and hardware resources.
 * Call advance at least once per reference-timer wrap (the task uses 10 ms).
 * Positions/counts are samples per channel; modulo differences allow streams
 * shorter than half the unsigned sample range. No clock reset on starvation. */
typedef struct {
    unsigned started, rate, last_tick, played, remainder, decoded;
    unsigned underflows, held, dropped, gap;
} audio_playback_t;

enum { AUDIO_REFERENCE_HZ = 100000000u };

static inline void audio_playback_advance(audio_playback_t *p, unsigned now)
{
    if (!p->started) return;
    uint64_t samples = (uint64_t)(unsigned)(now - p->last_tick) * p->rate + p->remainder;
    p->played += (unsigned)(samples / AUDIO_REFERENCE_HZ);
    p->remainder = (unsigned)(samples % AUDIO_REFERENCE_HZ);
    p->last_tick = now;
}

/* A decoded frame becomes available at now. Return its obsolete prefix, which
 * must not be output. A completely late frame still has to be decoded for MP2
 * synthesis state. Consecutive completely late frames are one gap; a usable
 * suffix closes it. Counting only confirmed media excludes a provisional tail
 * beyond the last frame (normal EOF), without needing a second header parser. */
static inline unsigned audio_playback_frame(audio_playback_t *p, unsigned now,
                                            unsigned samples, unsigned rate)
{
    if (!p->started) {
        p->started = 1;
        p->rate = rate;
        p->last_tick = now;
    }
    audio_playback_advance(p, now);
    int late = (int)(p->played - p->decoded);
    unsigned skip = late > 0 ? (unsigned)late : 0;
    if (skip > samples) skip = samples;
    p->decoded += samples;
    if (skip) {
        if (!p->gap) ++p->underflows;
        p->gap = 1;
        p->held += skip;
        p->dropped += skip;
    }
    if (skip < samples) p->gap = 0;
    return skip;
}

/* End of the newest decoded frame on the continuing output clock. Rounding up
 * avoids consuming a fractional final sample early (notably at 44.1 kHz). */
static inline unsigned audio_playback_due(const audio_playback_t *p)
{
    int remaining = (int)(p->decoded - p->played);
    if (remaining <= 0) return p->last_tick;
    uint64_t ticks = (uint64_t)(unsigned)remaining * AUDIO_REFERENCE_HZ - p->remainder;
    return p->last_tick + (unsigned)((ticks + p->rate - 1) / p->rate);
}

#endif

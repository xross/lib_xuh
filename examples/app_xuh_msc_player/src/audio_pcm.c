#include "audio_pcm.h"

static inline void barrier(void) { __asm__ volatile("" ::: "memory"); }

void audio_pcm_reset(audio_pcm_t *q, unsigned hold)
{
    q->delivered = q->played = q->started = q->eof = q->drained = 0;
    q->hold = hold;
    q->missing = q->underflows = q->first_underflow = 0;
    q->gap = q->gap_start = q->gap_confirmed = 0;
    barrier();
}

unsigned audio_pcm_push(audio_pcm_t *q, const int32_t *samples, unsigned count)
{
    unsigned start = q->delivered, played = q->played;
    barrier(); /* Acquire the consumer's released slots. */
    int late = (int)(played - start);
    unsigned skip = late > 0 ? (unsigned)late : 0;
    if (skip > count) skip = count;
    unsigned ahead = (int)(start - played) > 0 ? start - played : 0;
    unsigned copy = count - skip;
    if (copy > AUDIO_PCM_CAPACITY - ahead) copy = AUDIO_PCM_CAPACITY - ahead;
    for (unsigned i = skip; i < skip + copy; ++i) {
        unsigned slot = (start + i) & (AUDIO_PCM_CAPACITY - 1);
        q->samples[slot][0] = samples[2 * i];
        q->samples[slot][1] = samples[2 * i + 1];
    }
    barrier(); /* Publish complete pairs only. */
    q->delivered = start + skip + copy;
    return skip + copy;
}

void audio_pcm_release(audio_pcm_t *q)
{
    barrier();
    q->hold = 0;
}

void audio_pcm_finish(audio_pcm_t *q)
{
    barrier();
    q->eof = 1;
}

void audio_pcm_pop(audio_pcm_t *q, int32_t samples[2])
{
    samples[0] = samples[1] = 0;
    unsigned eof = q->eof;
    barrier(); /* If EOF is visible, acquire its final delivered position too. */
    unsigned delivered = q->delivered;
    barrier(); /* Acquire published PCM before reading slots. */
    if (!q->started) {
        if (!delivered) {
            if (eof) q->drained = 1;
            return;
        }
        if ((delivered < AUDIO_PCM_PREFILL || q->hold) && !eof) return;
        q->started = 1;
    }
    unsigned played = q->played;
    /* Confirm only silence that actually replaced subsequently delivered
     * media. Fully late chunks extend one gap, not one episode per chunk. */
    if (q->gap) {
        int known = (int)(delivered - q->gap_start);
        unsigned confirmed = known > 0 ? (unsigned)known : 0;
        if (confirmed > played - q->gap_start) confirmed = played - q->gap_start;
        if (confirmed > q->gap_confirmed) {
            if (!q->gap_confirmed) {
                if (!q->underflows) q->first_underflow = q->gap_start;
                ++q->underflows;
            }
            q->missing += confirmed - q->gap_confirmed;
            q->gap_confirmed = confirmed;
        }
    }
    if ((int)(delivered - played) > 0) {
        unsigned slot = played & (AUDIO_PCM_CAPACITY - 1);
        samples[0] = q->samples[slot][0];
        samples[1] = q->samples[slot][1];
        q->gap = 0;
    } else if (!q->gap) {
        q->gap = 1;
        q->gap_start = played;
        q->gap_confirmed = 0;
    }
    barrier(); /* Release slots only after both sample reads. */
    q->played = played + 1;
    if (eof && (int)(q->played - delivered) >= 0) q->drained = 1;
}

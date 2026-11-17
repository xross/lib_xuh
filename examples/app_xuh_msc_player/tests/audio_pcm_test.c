#include <assert.h>
#include <stdio.h>
#include "audio_pcm.h"

static audio_pcm_t q;
static int32_t media[4 * AUDIO_PCM_CAPACITY][2];

static unsigned push(unsigned from, unsigned count)
{
    return audio_pcm_push(&q, media[from], count);
}

static int32_t pop(void)
{
    int32_t pair[2];
    audio_pcm_pop(&q, pair);
    assert(pair[1] == -pair[0]);
    return pair[0];
}

int main(void)
{
    for (unsigned i = 0; i < 4 * AUDIO_PCM_CAPACITY; ++i) {
        media[i][0] = (int32_t)i + 1;
        media[i][1] = -(int32_t)i - 1;
    }

    /* Silence, and a stopped clock, until the prefill is reached. */
    audio_pcm_reset(&q);
    assert(push(0, AUDIO_PCM_PREFILL - 1) == AUDIO_PCM_PREFILL - 1);
    assert(pop() == 0 && !q.started && q.played == 0);
    assert(push(AUDIO_PCM_PREFILL - 1, 1) == 1);
    assert(pop() == 1 && q.started && q.played == 1);

    /* A full queue takes nothing more; space returns as frames are played. */
    unsigned delivered = AUDIO_PCM_PREFILL;
    delivered += push(delivered, 2 * AUDIO_PCM_CAPACITY);
    assert(delivered == AUDIO_PCM_CAPACITY + 1 && push(delivered, 1) == 0);
    for (unsigned i = 1; i < delivered; ++i) assert(pop() == (int32_t)i + 1);

    /* Starvation: the clock runs on through silence. The gap counts once, when
     * later media arrives, and the late prefix is taken and dropped. */
    for (unsigned i = 0; i < 10; ++i) assert(pop() == 0);
    assert(q.played == delivered + 10 && q.underflows == 0);
    assert(push(delivered, 4) == 4 && push(delivered + 4, 12) == 12);
    assert(pop() == (int32_t)delivered + 11);
    assert(q.underflows == 1 && q.missing == 10 && q.first_underflow == delivered);
    for (unsigned i = 11; i < 16; ++i) assert(pop() == (int32_t)(delivered + i) + 1);

    /* The end of the media is not an underflow, and the clock keeps running. */
    audio_pcm_finish(&q);
    assert(pop() == 0 && q.drained && q.underflows == 1 && q.missing == 10);
    unsigned played = q.played;
    assert(pop() == 0 && q.played == played + 1);

    /* A play shorter than the prefill still plays, once the end is known. */
    audio_pcm_reset(&q);
    assert(push(0, 3) == 3 && pop() == 0 && !q.started);
    audio_pcm_finish(&q);
    assert(pop() == 1 && pop() == 2 && !q.drained && pop() == 3 && q.drained);

    /* An empty play drains at once. */
    audio_pcm_reset(&q);
    audio_pcm_finish(&q);
    assert(pop() == 0 && q.drained && !q.started);

    puts("audio PCM queue tests passed");
    return 0;
}

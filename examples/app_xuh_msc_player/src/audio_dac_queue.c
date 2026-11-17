#include "audio_dac_queue.h"
#if APP_XUH_MSC_PLAYER_AUDIO_DAC
#include <xcore/hwtimer.h>
#include "audio_pcm.h"

enum { POLL_TICKS = 25000 };

static audio_pcm_t pcm;
static volatile unsigned reset_request, reset_ack;

/* The I2S callback: the only reader of the queue, and the only code that
 * resets it, so a reset never races a pop. */
void audio_dac_next(int32_t samples[2])
{
    unsigned request = reset_request;
    if (request != reset_ack) {
        audio_pcm_reset(&pcm);
        __asm__ volatile("" ::: "memory");
        reset_ack = request;
    }
    audio_pcm_pop(&pcm, samples);
}

static void wait_for(volatile unsigned *word, unsigned value)
{
    hwtimer_t timer = hwtimer_alloc();
    if (!timer) __builtin_trap();
    while (*word != value) hwtimer_delay(timer, POLL_TICKS);
    hwtimer_free(timer);
    __asm__ volatile("" ::: "memory");
}

void audio_dac_reset(void)
{
    unsigned request = reset_request + 1;
    reset_request = request;
    wait_for(&reset_ack, request);
}

unsigned audio_dac_write(const int32_t *samples, unsigned frames)
{
    return audio_pcm_push(&pcm, samples, frames);
}

void audio_dac_finish(void)
{
    audio_pcm_finish(&pcm);
    wait_for(&pcm.drained, 1);
}

void audio_dac_status(unsigned status[DAC_STATUS_WORDS])
{
    status[DAC_PLAYED] = pcm.played;
    status[DAC_STARTED] = pcm.started;
    status[DAC_UNDERFLOWS] = pcm.underflows;
    status[DAC_MISSING] = pcm.missing;
    status[DAC_FIRST_UNDERFLOW] = pcm.first_underflow;
}
#endif

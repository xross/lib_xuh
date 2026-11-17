#include <assert.h>
#include <xcore/hwtimer.h>
#include <xcore/select.h>
#include "dac_fixture.h"
#include "decode_lead.h"
#include "jpeg_pipeline.h"

/* Replace only the I2S/PLL adapter, not the queue or its reset/EOF mailbox.
 * One call per stereo sample period, on the same tile as the real MP2 task.
 * Absolute deadlines plus a remainder give exactly 48 kHz at 100 MHz. */
void test_dac_consumer(chanend control)
{
    hwtimer_t timer = hwtimer_alloc();
    assert(timer);
    unsigned due = hwtimer_get_time(timer), fraction = 0, limit = 0;
    unsigned observation[FIXTURE_WORDS] = {0};
    for (;;) {
        hwtimer_set_trigger_time(timer, due);
        SELECT_RES_ORDERED(CASE_THEN(timer, on_sample), CASE_THEN(control, on_control)) {
        on_sample: {
            (void)hwtimer_get_time(timer);
            int32_t pair[2];
            audio_dac_next(pair);
            audio_dac_status(observation);
            ++observation[FIXTURE_CALLBACKS];
            /* Reset/startup must never expose PCM from an earlier play. */
            if (!observation[DAC_STARTED]) assert(!pair[0] && !pair[1]);
            if (pair[0] || pair[1]) ++observation[FIXTURE_NONZERO];
            if (observation[DAC_STARTED] && observation[DAC_PLAYED] <= limit) {
                ++observation[FIXTURE_CAPTURED];
                observation[FIXTURE_HASH] = test_pcm_hash(observation[FIXTURE_HASH], pair);
            }
            fraction += 100000000u;
            due += fraction / AUDIO_DAC_RATE;
            fraction %= AUDIO_DAC_RATE;
            break;
        }
        on_control: {
            unsigned request[2];
            pipeline_receive(control, request, 2);
            if (request[0] == FIXTURE_ARM) {
                limit = request[1];
                for (unsigned i = FIXTURE_CALLBACKS; i < FIXTURE_WORDS; ++i)
                    observation[i] = 0;
            } else assert(request[0] == FIXTURE_SNAPSHOT);
            audio_dac_status(observation);
            observation[FIXTURE_AUDIO_VALID] = decode_lead_read(&observation[FIXTURE_AUDIO_MS]);
            pipeline_send(control, observation, FIXTURE_WORDS);
            break;
        }
        }
    }
}

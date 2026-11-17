#include <platform.h>
#include "audio_pipeline.h"
void test_audio_pipeline(chanend channel);
#if APP_XUH_MSC_PLAYER_AUDIO_DAC
void test_dac_pipeline(chanend audio, chanend consumer);
void test_dac_consumer(chanend control);
void test_dac_watchdog(void);
#endif

int main(void)
{
    chan audio;
#if APP_XUH_MSC_PLAYER_AUDIO_DAC
    chan consumer;
#endif
    par {
        /* Tiles as in the player. */
        on tile[1]: mp2_audio_task(audio);
#if APP_XUH_MSC_PLAYER_AUDIO_DAC
        on tile[1]: test_dac_consumer(consumer);
        on tile[0]: test_dac_pipeline(audio, consumer);
        on tile[0]: test_dac_watchdog();
#else
        on tile[0]: test_audio_pipeline(audio);
#endif
    }
    return 0;
}

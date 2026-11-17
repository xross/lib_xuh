#ifndef APP_AUDIO_DAC_H_
#define APP_AUDIO_DAC_H_

#include <stdint.h>

#ifndef APP_XUH_MSC_PLAYER_AUDIO_DAC
#define APP_XUH_MSC_PLAYER_AUDIO_DAC 0
#endif

enum { AUDIO_DAC_RATE = 48000, AUDIO_DAC_MCLK = 24576000,
       AUDIO_DAC_SLOT_BITS = 32 };

/* Tile 1 only. lib_i2s's XC interface is isolated in this adapter. */
void audio_dac_task(void);

/* Implemented by the PCM consumer. Called
 * synchronously by the I2S task: bounded work only, no channel I/O, waits,
 * decoding, allocation or logging. Samples are interleaved full-scale S32. */
void audio_dac_next(int32_t samples[2]);

#endif

#include "audio_dac.h"
#if APP_XUH_MSC_PLAYER_AUDIO_DAC
#include <platform.h>
#include <xs1.h>
#include "i2s.h"
extern "C" {
#include "sw_pll.h"
}

/* A DAC SHIM fitted directly on J3: BCLK X1D39, LRCLK X1D35, DIN X1D24.
 * The application PLL feeds port 1D internally; no MCLK wire is needed.
 * Clock block 1 on this tile belongs to the LCD's SPI master. */
on tile[1]: out port p_dac_bclk = XS1_PORT_1P;
on tile[1]: out buffered port:32 p_dac_lrclk = XS1_PORT_1L;
on tile[1]: out buffered port:32 p_dac_data[1] = {XS1_PORT_1I};
on tile[1]: in port p_dac_mclk = XS1_PORT_1D;
on tile[1]: clock dac_bclk = XS1_CLKBLK_2;

[[distributable]]
static void dac_samples(server i2s_frame_callback_if output)
{
    while (1) {
        select {
        case output.init(i2s_config_t &?config, tdm_config_t &?tdm):
            config.mode = I2S_MODE_I2S;
            config.mclk_bclk_ratio = AUDIO_DAC_MCLK /
                (AUDIO_DAC_RATE * 2 * AUDIO_DAC_SLOT_BITS);
            break;
        case output.restart_check() -> i2s_restart_t restart:
            restart = I2S_NO_RESTART;
            break;
        case output.send(size_t count, int32_t samples[count]):
            int32_t pair[2];
            audio_dac_next(pair);
            samples[0] = pair[0];
            samples[1] = pair[1];
            break;
        case output.receive(size_t count, int32_t samples[count]):
            break; /* Output only. */
        }
    }
}

void audio_dac_task(void)
{
    if (sw_pll_fixed_clock(AUDIO_DAC_MCLK, SW_PLL_TILE_1) != SW_PLL_SUCCESS)
        __builtin_trap();
    interface i2s_frame_callback_if output;
    par {
        dac_samples(output);
        i2s_frame_master(output, p_dac_data, 1, null, 0,
                         AUDIO_DAC_SLOT_BITS, p_dac_bclk, p_dac_lrclk,
                         p_dac_mclk, dac_bclk);
    }
}
#endif

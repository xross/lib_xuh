#include <xs1.h>
#include <platform.h>
#include <stdio.h>
#include <print.h>

#include "xuh.h"
#include "hostapp.h"
#include "jpeg_pipeline.h"
#include "mpeg_pipeline.h"
#include "audio_pipeline.h"

#include <spi.h>
#include "lcd_display.h"

on tile[1]: out buffered port:32 p_lcd_sclk = XS1_PORT_1M;
on tile[1]: out buffered port:32 p_lcd_mosi = XS1_PORT_1N;
on tile[1]: out port p_lcd_cs = XS1_PORT_1O;
/* Bit 0 is D/C and bit 1 is reset; bits 2 and 3 (X1D30, X1D31) stay unused. */
on tile[1]: out port p_lcd_control = XS1_PORT_4F;
on tile[1]: clock lcd_spi_clock = XS1_CLKBLK_1;

#define NUM_EPS 3

void main()
{
#if APP_XUH_MSC_PLAYER_MPEG_AUDIO
    chan c_audio;
#endif
    chan xuhChans_out[NUM_EPS];
    chan xuhChans_in[NUM_EPS];
    interface spi_master_if lcd_spi[1];
    chan c_lcd_image;
#if MSC_JPEG_PIPELINE
    chan c_jpeg_load, c_jpeg_output, c_lcd_render;
#elif MSC_MPEG_PIPELINE
    chan c_mpeg_load, c_lcd_render;
#endif

    par
    {
#if APP_XUH_MSC_PLAYER_MPEG_AUDIO
        on tile[1]: mp2_audio_task(c_audio);
#endif
        on tile[1]: spi_master(lcd_spi, 1, p_lcd_sclk, p_lcd_mosi,
                               null, p_lcd_cs, 1, lcd_spi_clock);
#if MSC_JPEG_PIPELINE
        on tile[1]: jpeg_pipeline_receive(c_lcd_image, c_lcd_render);
        on tile[1]: lcd_display_task(lcd_spi[0], c_lcd_render);
        on tile[0]: jpeg_pipeline_decode(c_jpeg_load, c_jpeg_output);
        on tile[0]: jpeg_pipeline_transmit(c_jpeg_output, c_lcd_image);
#elif MSC_MPEG_PIPELINE
        on tile[1]: mpeg_pipeline_receive(c_lcd_image, c_lcd_render);
        on tile[1]: lcd_display_task(lcd_spi[0], c_lcd_render);
        on tile[0]: mpeg_pipeline_decode(c_mpeg_load, c_lcd_image);
#else
        on tile[1]: lcd_display_task(lcd_spi[0], c_lcd_image);
#endif

#ifndef DEBUG
        on tile[0]:
        {
            XUH_Manager(xuhChans_out, NUM_EPS, xuhChans_in, NUM_EPS);
        }
#endif

        on tile[0]:
        {
            USBHost(xuhChans_out, NUM_EPS, xuhChans_in, NUM_EPS
#if MSC_JPEG_PIPELINE
                    , c_jpeg_load
#elif MSC_MPEG_PIPELINE
                    , c_mpeg_load
#else
                    , c_lcd_image
#endif
#if APP_XUH_MSC_PLAYER_MPEG_AUDIO
                    , c_audio
#endif
                    );
        }
    }
}

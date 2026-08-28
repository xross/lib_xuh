
#include <xs1.h>
#include <platform.h>
#include <stdio.h>
#include <print.h>

#include "gpioDefines.h"
#include "xuh.h"
#include "usbh.h"

#if APP_XUH_MSC_ENABLE_LCD
#include <spi.h>
#include "lcd_st7789v.h"

on tile[0]: out buffered port:32 p_lcd_sclk = XS1_PORT_1L;
on tile[0]: out buffered port:32 p_lcd_mosi = XS1_PORT_1M;
on tile[0]: out port p_lcd_cs = XS1_PORT_1N;
on tile[0]: out port p_lcd_dc = XS1_PORT_1O;
on tile[0]: out port p_lcd_rst = XS1_PORT_1P;
on tile[0]: clock lcd_spi_clock = XS1_CLKBLK_1;
#endif

#if APP_XUH_MSC_HAS_USB_SWITCH_GPIO
/*
 * [0]: bit 0 of usb swtich select signal
 * [1]: bit 1 of usb swtich select signal
 * [2]: Enable vbus output */
on tile[0] : out port p_gpio = XS1_PORT_4F;
#endif

/* TODO Move me. */
void delay(unsigned x)
{
    timer t;
    unsigned time;
    t :> time;
    t when timerafter(time + x) :> void;
}

#define NUM_EPS 3

void main()
{
    chan xuhChans_out[NUM_EPS];
    chan xuhChans_in[NUM_EPS];
#if APP_XUH_MSC_ENABLE_LCD
    interface spi_master_if lcd_spi[1];
#endif
#if APP_XUH_MSC_ENABLE_LCD
    chan c_lcd_image;
#endif

    /* Enable 5V, VBUS and select USB A socket in USB switch */
    //p_gpio <: (0 << PORT_32A_VBUS_OUT_EN_N_BITSHIFT) | (1 << PORT_32A_EN_5VA_BITSHIFT)
    //| (1 << PORT_32A_USB_SEL_1_BITSHIFT);

    par
    {
#if APP_XUH_MSC_ENABLE_LCD
        on tile[0]: spi_master(lcd_spi, 1, p_lcd_sclk, p_lcd_mosi,
                               null, p_lcd_cs, 1, lcd_spi_clock);
        on tile[0]: lcd_st7789v_task(lcd_spi[0], c_lcd_image);
#endif

#if APP_XUH_MSC_HAS_USB_SWITCH_GPIO
        on tile[0]:
        {
            p_gpio <: 6;
            while(1);
        }
#endif

#ifndef DEBUG
        on tile[1]:
        {
            XUH_Manager(xuhChans_out, NUM_EPS, xuhChans_in, NUM_EPS);
        }
#endif

        on tile[1]:
        {
            USBHost(xuhChans_out[0], xuhChans_in[0], xuhChans_out[2],
                    xuhChans_in[1]
#if APP_XUH_MSC_ENABLE_LCD
                    , c_lcd_image
#endif
                    );
        }
    }
}

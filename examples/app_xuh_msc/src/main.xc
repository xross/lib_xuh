
#include <xs1.h>
#include <platform.h>
#include <stdio.h>
#include <print.h>

#include "gpioDefines.h"
#include "xuh.h"
#include "usbh.h"

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

extern void MassStorage(XUH_Ep ep0_out, XUH_Ep ep0_in, XUH_Ep ep_out2, XUH_Ep ep_in1);

#define NUM_EPS 3

void main()
{
    chan xuhChans_out[NUM_EPS];
    chan xuhChans_in[NUM_EPS];

    /* Enable 5V, VBUS and select USB A socket in USB switch */
    //p_gpio <: (0 << PORT_32A_VBUS_OUT_EN_N_BITSHIFT) | (1 << PORT_32A_EN_5VA_BITSHIFT)
    //| (1 << PORT_32A_USB_SEL_1_BITSHIFT);

    par
    {
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
            USBHost(xuhChans_out[0], xuhChans_in[0], xuhChans_out[2], xuhChans_in[1]);
        }
    }
}

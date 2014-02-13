
#include <xs1.h>
#include <print.h>
#include "gpioDefines.h"

#include "usb.h"
#include "usb_defs.h"
#include "xuh.h"
#include "stdio.h"


out port p_gpio = XS1_PORT_32A;



void delay(unsigned x)
{
    timer t;
    unsigned time;
    t :> time;
    t when timerafter(time + x) :> void;
}

/* TODO 
 * - It is annoying that EP's are hard coded.  Would be good if could assign an Address to an EP 
 * - Do we really need to do all the token loading/storing?
 */

extern void MassStorage(XUH_Ep ep0_out, XUH_Ep ep0_in, XUH_Ep ep_out2, XUH_Ep ep_in1);

void USBHost(chanend c_out, chanend c_in, chanend c_out2, chanend c_in1);


#define NUM_EPS 3

int main()
{
    chan xuhChans_out[NUM_EPS];
    chan xuhChans_in[NUM_EPS];

    p_gpio <: (0 << PORT_32A_VBUS_OUT_EN_N_BITSHIFT) | (1 << PORT_32A_EN_5VA_BITSHIFT) | (1 << PORT_32A_USB_SEL_1_BITSHIFT);

   // printstr("XUD TEST\n");

    par
    {
        XUH_Manager(xuhChans_out, NUM_EPS, xuhChans_in, NUM_EPS);

        USBHost(xuhChans_out[0], xuhChans_in[0], xuhChans_out[1], xuhChans_in[2]);
    }
}

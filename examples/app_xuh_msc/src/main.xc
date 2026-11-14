#include <platform.h>

#include "xuh.h"
#include "usb_host.h"

/* Channel slots in each direction: the control endpoint and one bulk endpoint. */
#define NUM_EPS 2

int main()
{
    chan c_out[NUM_EPS], c_in[NUM_EPS];

    par {
        on tile[1]: XUH_Manager(c_out, NUM_EPS, c_in, NUM_EPS);
        on tile[1]: UsbHost(c_out, NUM_EPS, c_in, NUM_EPS);
    }
    return 0;
}

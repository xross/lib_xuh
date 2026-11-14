#ifndef USB_HOST_H_
#define USB_HOST_H_

#include <xccompat.h>

/* USB host application task. Runs on the tile of XUH_Manager(), connected to
 * it by all of its channels. */
void UsbHost(chanend c_out[], unsigned n_out, chanend c_in[], unsigned n_in);

#endif

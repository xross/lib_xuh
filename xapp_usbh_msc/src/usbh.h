#ifndef __USBH_H__

//#include "usb_std_requests.h"

void USBHost(chanend c_out, chanend c_in, chanend c_out2, chanend c_in1);

#if !defined(__XC__)

/* USB class driver struct. Mostly function pointers */
typedef struct
{
    void (* const init) (void);
    void (* const DescComp_InterfaceEndpoint) (void*);
    void (* const DescComp_Interface) (void*);
} USBH_Driver_t;

#endif
#endif

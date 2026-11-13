
#ifndef _XUH_H_
#define _XUH_H_ 1

#include <xccompat.h>

typedef unsigned int XUH_Ep;

void XUH_Manager(chanend epChans_out[], unsigned epChanCount_out,
                 chanend epChans_in[], unsigned epChanCount_in);


XUH_Ep XUH_InitEp(chanend c);

/* Bind an idle, non-control channel slot to a device bEndpointAddress.
 * Channel slots are independent of USB endpoint numbers. The caller must use
 * an IN slot for an IN address and an OUT slot for an OUT address. Resets the
 * data toggle to DATA0; call after SET_CONFIGURATION, before the first transfer.
 * Returns 0, or -1 for a null handle/reserved or control endpoint address. */
int XUH_SetEndpointAddress(XUH_Ep ep, unsigned address);


int XUH_InTransfer(XUH_Ep ep, unsigned char buffer[]);

int XUH_OutTransfer(XUH_Ep ep, unsigned char buffer[], unsigned length);
int XUH_SetupTransfer(XUH_Ep ep, unsigned char buffer[8]);

void XUH_SetDeviceAddress(int address);
/*****/

int XUH_TxTransfer(XUH_Ep ep, unsigned char buffer[], unsigned length, unsigned isSetup);


#endif

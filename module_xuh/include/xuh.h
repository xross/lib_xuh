
#ifndef _XUH_H_
#define _XUH_H_ 1

#include <xccompat.h>

typedef unsigned int XUH_Ep;

void XUH_Manager(chanend epChans_out[], unsigned epChanCount_out, 
                 chanend epChans_in[], unsigned epChanCount_in);


XUH_Ep XUH_InitEp(chanend c);


int XUH_InTransfer(XUH_Ep ep, unsigned char buffer[]);

int XUH_OutTransfer(XUH_Ep ep, unsigned char buffer[], unsigned length);
int XUH_SetupTransfer(XUH_Ep ep, unsigned char buffer[8]);

/*****/

int XUH_TxTransfer(XUH_Ep ep, unsigned char buffer[], unsigned length, unsigned isSetup);


#endif

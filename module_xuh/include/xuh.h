
#ifndef _XUH_H_
#define _XUH_H_ 1

typedef unsigned int XUH_Ep;

void XUH_Manager(chanend epChans_out[], unsigned epChanCount_out, 
                 chanend epChans_in[], unsigned epChanCount_in);


XUH_Ep XUH_InitEp(chanend c);


int XUH_SetupTransfer(XUH_Ep ep, unsigned char buffer[8]);
int XUH_InTransfer(XUH_Ep ep, unsigned char buffer[]);


#endif

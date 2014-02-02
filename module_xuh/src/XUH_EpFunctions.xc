
/* XUH_EpFunctions.xc */
#include <xs1.h>
#include "xuh.h"

int XUH_OutTransfer(XUH_Ep ep, unsigned char buffer[], unsigned length)
{
    return XUH_TxTransfer(ep, buffer, length, 0);
}

int XUH_SetupTransfer(XUH_Ep ep, unsigned char buffer[8])
{
    return XUH_TxTransfer(ep, buffer, 8, 1);
}

XUH_Ep XUH_InitEp(chanend c)
{
    XUH_Ep ep = inuint(c);
    return ep;
}

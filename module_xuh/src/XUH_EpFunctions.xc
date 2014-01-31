
/* XUH_EpFunctions.xc */
#include <xs1.h>
#include "xuh.h"

XUH_Ep XUH_InitEp(chanend c)
{
    XUH_Ep ep = inuint(c);
    return ep;
}

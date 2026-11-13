#include "xuh.h"

extern unsigned devAddr;
extern void GenerateTokens();

/* Set device address in host */
void XUH_SetDeviceAddress(int address)
{
    devAddr = address;
    GenerateTokens();
}

int XUH_SetEndpointAddress(XUH_Ep ep, unsigned address)
{
    if (!ep || (address & ~0x8fu) || !(address & 0x0fu)) return -1;
    /* XUH_Ep is a tile-local pointer to the manager's nine-word endpoint ABI,
     * also used by XUH_EpFunc.S. Keep the scheduler slot/chanends unchanged. */
    unsigned *info = (unsigned *)ep;
    info[8] = address & 0x0fu;
    info[4] = 0xc3; /* USB PID DATA0 after configuration. */
    __asm__ volatile("" ::: "memory");
    return 0;
}

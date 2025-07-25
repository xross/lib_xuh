extern unsigned devAddr;
extern void GenerateTokens();

/* Set device address in host */
void XUH_SetDeviceAddress(int address)
{
    devAddr = address;
    GenerateTokens();
}

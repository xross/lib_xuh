
/* XUH_EpFunctions.xc */
#include <xs1.h>
#include <print.h>
#include "xuh.h"
#include "usbh.h"

XUH_Ep XUH_InitEp(chanend c)
{
    XUH_Ep ep = inuint(c);
    return ep;
}

static void USBH_ComposeSetupBuffer(USB_SetupPacket_t sp, unsigned char buffer[8])
{
    buffer[0] = sp.bmRequestType.Recipient
                  | (sp.bmRequestType.Type << 5)
                  | (sp.bmRequestType.Direction << 7);

    buffer[1] = sp.bRequest;

    buffer[2] = sp.wValue & 0xff;
    buffer[3] = (sp.wValue & 0xff00)>>8;

    buffer[4] = sp.wIndex & 0xff;
    buffer[5] = (sp.wIndex & 0xff00)>>8;

    buffer[6] = sp.wLength & 0xff;
    buffer[7] = (sp.wLength & 0xff00)>>8;
}

int XUH_OutTransfer(XUH_Ep ep, unsigned char buffer[], unsigned length)
{
    return XUH_TxTransfer(ep, buffer, length, 0);
}

int XUH_SetupTransfer(XUH_Ep ep, unsigned char buffer[8])
{
    return XUH_TxTransfer(ep, buffer, 8, 1);
}

int XUH_ControlTransfer_In(XUH_Ep ep_out, XUH_Ep ep_in, USB_SetupPacket_t sp, unsigned char buffer[])
{
    unsigned char spBuffer[64];
    unsigned length;

    USBH_ComposeSetupBuffer(sp, spBuffer);

    XUH_SetupTransfer(ep_out, spBuffer);

    length = XUH_InTransfer(ep_in, buffer);

	XUH_OutTransfer(ep_out, spBuffer, 0);

    return length;
}


int XUH_SetAddress(XUH_Ep ep_out, XUH_Ep ep_in, int addr)
{
    unsigned char spBuffer[64];
    unsigned length;
    USB_SetupPacket_t sp;
    USB_BmRequestType_t bmRequestType;

    bmRequestType.Recipient =  0x00 ;
    bmRequestType.Type =  0x00 ;
    bmRequestType.Direction =  0 ;

    sp.bRequest =  0x05 ;
    sp.wValue = addr;

    sp.wIndex = 0;
    sp.wLength = 0;

    USBH_ComposeSetupBuffer(sp, spBuffer);
    XUH_SetupTransfer(ep_out, spBuffer);
    length = XUH_InTransfer(ep_in, spBuffer);


    XUH_SetDeviceAddress(addr);
}

int XUH_GetDescriptor(XUH_Ep ep_out, XUH_Ep ep_in, unsigned char buffer[], unsigned descReq, unsigned wLength, unsigned wIndex)
{
    USB_SetupPacket_t sp;
    USB_BmRequestType_t bmRequestType;

    bmRequestType.Recipient =  0x00 ;
    bmRequestType.Type =  0x00 ;
    bmRequestType.Direction =  1 ;

    sp.bmRequestType = bmRequestType;
    sp.bRequest =  0x06 ;
    sp.wValue = descReq;

    sp.wIndex = wIndex;
    sp.wLength = wLength;

    return XUH_ControlTransfer_In(ep_out, ep_in, sp, buffer);
}

int XUH_GetStringDescriptor(XUH_Ep ep_out, XUH_Ep ep_in, unsigned char buffer[], unsigned stringIndex, unsigned wLength)
{
    return XUH_GetDescriptor(ep_out, ep_in, buffer,  ( 0x03 << 8)  | stringIndex, wLength, 0);
}


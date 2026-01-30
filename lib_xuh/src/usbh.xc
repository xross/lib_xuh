#include "xuh.h"
#include "usb_std_requests.h"

// TODO move these.
#define USB_STRING                      0x03
#define USB_WVALUE_GETDESC_STRING       (USB_STRING << 8)

int USBH_ControlTransfer_In(XUH_Ep ep_out, XUH_Ep ep_in, USB_SetupPacket_t sp, unsigned char buffer[])
{
    unsigned char spBuffer[64];
    unsigned length;

    USB_ComposeSetupBuffer(sp, spBuffer);

    XUH_SetupTransfer(ep_out, spBuffer);

    length = XUH_InTransfer(ep_in, buffer);

	XUH_OutTransfer(ep_out, spBuffer, 0);

    return length;
}

/* Send SetAddress() to device and also set device address in XUH */
int USBH_SetAddress(XUH_Ep ep_out, XUH_Ep ep_in, int addr)
{
    unsigned char spBuffer[64];
    unsigned length;
    USB_SetupPacket_t sp;
    USB_BmRequestType_t bmRequestType;

    bmRequestType.Recipient = USB_BM_REQTYPE_RECIP_DEV;
    bmRequestType.Type = USB_BM_REQTYPE_TYPE_STANDARD;
    bmRequestType.Direction = USB_BM_REQTYPE_DIRECTION_H2D;

    sp.bmRequestType = bmRequestType;

    sp.bRequest = USB_SET_ADDRESS;
    sp.wValue = addr;

    sp.wIndex = 0;
    sp.wLength = 0;

    USB_ComposeSetupBuffer(sp, spBuffer);

    XUH_SetupTransfer(ep_out, spBuffer);

    length = XUH_InTransfer(ep_in, spBuffer);

    /* SetAddress in Host */
    XUH_SetDeviceAddress(addr);
}

int  USBH_GetDescriptor(XUH_Ep ep_out, XUH_Ep ep_in, unsigned char buffer[], unsigned descReq, unsigned wLength, unsigned wIndex)
{
    USB_SetupPacket_t sp;
    USB_BmRequestType_t bmRequestType;

    bmRequestType.Recipient = USB_BM_REQTYPE_RECIP_DEV;
    bmRequestType.Type = USB_BM_REQTYPE_TYPE_STANDARD;
    bmRequestType.Direction = USB_BM_REQTYPE_DIRECTION_D2H;

    sp.bmRequestType = bmRequestType;
    sp.bRequest = USB_GET_DESCRIPTOR;
    sp.wValue = descReq;

    sp.wIndex = wIndex;
    sp.wLength = wLength;

    return USBH_ControlTransfer_In(ep_out, ep_in, sp, buffer);
}

int USBH_GetStringDescriptor(XUH_Ep ep_out, XUH_Ep ep_in, unsigned char buffer[], unsigned stringIndex, unsigned wLength)
{
    return USBH_GetDescriptor(ep_out, ep_in, buffer,  USB_WVALUE_GETDESC_STRING | stringIndex, wLength, 0);
}

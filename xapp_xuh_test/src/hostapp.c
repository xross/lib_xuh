
#include <xccompat.h>

#include "usb.h"
#include "usb_defs.h"
#include "xuh.h"
#include "stdio.h"
#include "usb_descriptors.h"

#define MAX_EP0_PKT_SIZE 64

#define DEV_ADDR 3

/* TODO:
 * Tidy GetDesc()/ControlTrans() - use structs 
 *
 */

void MassStorage(XUH_Ep ep_out0, XUH_Ep ep_in0, XUH_Ep ep_out2, XUH_Ep ep_in1);


void delay(unsigned x);

void UnUnicode(unsigned char out[], unsigned char in[], int length)
{
    int j, i;
    for(i = 2, j = 0; i < length; i+=2, j+=1)
    {
        out[j] = in[i]; 
    }
}

void ComposeSetupBuffer(USB_SetupPacket_t sp, unsigned char buffer[8])
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

int XUH_ControlTransfer_In(XUH_Ep ep_out, XUH_Ep ep_in, USB_SetupPacket_t sp, unsigned char buffer[])
{
    unsigned char spBuffer[MAX_EP0_PKT_SIZE];
    unsigned length;

   ComposeSetupBuffer(sp, spBuffer);

    XUH_SetupTransfer(ep_out, spBuffer);
    
    length = XUH_InTransfer(ep_in, buffer);

	XUH_OutTransfer(ep_out, spBuffer, 0);

    return length;
}

int XUH_SetAddress(XUH_Ep ep_out, XUH_Ep ep_in, int addr)
{ 
    unsigned char spBuffer[MAX_EP0_PKT_SIZE];
    unsigned length;
    USB_SetupPacket_t sp;
    USB_BmRequestType_t bmRequestType;
    
    bmRequestType.Recipient = USB_BM_REQTYPE_RECIP_DEV;
    bmRequestType.Type = USB_BM_REQTYPE_TYPE_STANDARD;
    bmRequestType.Direction = USB_BM_REQTYPE_DIRECTION_H2D;
   
    sp.bRequest = USB_SET_ADDRESS;
    sp.wValue = addr;

    sp.wIndex = 0;
    sp.wLength = 0;
    
    ComposeSetupBuffer(sp, spBuffer);
    XUH_SetupTransfer(ep_out, spBuffer);
    length = XUH_InTransfer(ep_in, spBuffer);

    /* SetAddress in Host */
    XUH_SetDeviceAddress(addr);
}


int  XUH_GetDescriptor(XUH_Ep ep_out, XUH_Ep ep_in, unsigned char buffer[], unsigned descReq, unsigned wLength, unsigned wIndex)
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
    
    return XUH_ControlTransfer_In(ep_out, ep_in, sp, buffer);
}

void USBHost(chanend c_out, chanend c_in, chanend c_out2, chanend c_in1)
{
    USB_BmRequestType_t bmRequestType;
    USB_SetupPacket_t sp;
    unsigned char buffer[64];
    unsigned char spBuffer[MAX_EP0_PKT_SIZE];
    int length;
    int tmp; 

    USB_Descriptor_Device_t deviceDesc;
    USB_Descriptor_String_t productStringDesc;

    XUH_Ep ep_out = XUH_InitEp(c_out);
    XUH_Ep ep_out2 = XUH_InitEp(c_out2);
    XUH_Ep ep_in = XUH_InitEp(c_in);
    XUH_Ep ep_in1 = XUH_InitEp(c_in1);
    
    delay(100000000);
    
    /* Get first 8 bbytes of device desciptor */
    length =  XUH_GetDescriptor(ep_out, ep_in, buffer,  USB_WVALUE_GETDESC_DEV, 8,  0);

    /* Set device address - sends request to device and sets it in XUH */
    XUH_SetAddress(ep_out, ep_in, DEV_ADDR);

    /* Get the rest of the device desciptor */
    length = XUH_GetDescriptor(ep_out, ep_in, (unsigned char*) &deviceDesc,  USB_WVALUE_GETDESC_DEV, 64, 0);
  
    /* Get product string */
    tmp = deviceDesc.iProduct;
    length = XUH_GetDescriptor(ep_out, ep_in, (unsigned char*) &productStringDesc,  USB_WVALUE_GETDESC_STRING | tmp, 64, 0);

#if 0
    printstr("Found device: ");
    UnUnicode(buffer, (unsigned char*) &productStringDesc, productStringDesc.bLength);
    for(int i = 0; i < productStringDesc.bLength/2-1; i++)
        printchar(productStringDesc.bString[i]);

#endif
    
    /* Get Config Descriptor header */
    uint8_t configHeader[sizeof(USB_Descriptor_Configuration_Header_t)];

    length = XUH_GetDescriptor(ep_out, ep_in, configHeader,  USB_WVALUE_GETDESC_CONFIG, sizeof(USB_Descriptor_Configuration_Header_t), 0);
    //printintln(configHeader[offsetof(USB_Descriptor_Configuration_Header_t, bMaxPower)]);
    
    unsigned *configSizePtr = ((USB_Descriptor_Configuration_Header_t*)configHeader)->wTotalLength;

    /* Get the rest of the Config Descriptor */
    XUH_GetDescriptor(ep_out, ep_in, buffer,  USB_WVALUE_GETDESC_CONFIG, configSizePtr, 0);
    
#if 0
    length = XUH_GetDescriptor(ep_out, ep_in, buffer,  USB_WVALUE_GETDESC_CONFIG, 10, 0);

    length = buffer[USB_CONFIG_DESC_WTOTALLENGTH];

    /* Get the rest of the config desc */
    length = XUH_GetDescriptor(ep_out, ep_in, buffer,  USB_WVALUE_GETDESC_CONFIG, length, 0);
#endif

    /* PARSE CONFIG DESC FOR EP ADDRESS */


    /* Set Config() */
    bmRequestType.Recipient = USB_BM_REQTYPE_RECIP_DEV;
    bmRequestType.Type = USB_BM_REQTYPE_TYPE_STANDARD;
    bmRequestType.Direction = USB_BM_REQTYPE_DIRECTION_H2D;
   
    sp.bmRequestType = bmRequestType;
    sp.bRequest = USB_SET_CONFIGURATION;
    sp.wValue = 1;
    sp.wIndex = 0;
    sp.wLength = 0;
    
    ComposeSetupBuffer(sp, spBuffer);

    XUH_SetupTransfer(ep_out, spBuffer);
    
    length = XUH_InTransfer(ep_in, buffer);

    delay(50000000);
    
    /* Device kinda up and running now.. lets try and do some mass storage stuff... */
    MassStorage(ep_out, ep_in, ep_out2, ep_in1);

    while(1);    
}

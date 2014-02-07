
#include <xs1.h>
#include <print.h>
#include "gpioDefines.h"

#include "usb.h"
#include "usb_defs.h"
#include "xuh.h"
#include "stdio.h"

#define MAX_EP0_PKT_SIZE 64

out port p_gpio = XS1_PORT_32A;

void delay(unsigned x)
{
    timer t;
    unsigned time;
    t :> time;
    t when timerafter(time + x) :> void;
}

/* TODO 
 * - It is annoying that EP's are hard coded.  Would be good if could assign an Address to an EP 
 * - Multiple transfers per frame
 * - Do we really need to do all the token loading/storing?
 */

extern void MassStorage(XUH_Ep ep_out2, XUH_Ep ep_in1);


void UnUnicode(unsigned char buffer[])
{
    int length = buffer[0];
   // buffer[1] = bDescriptorType
    
    for(int i = 2, j = 0; i < length; i+=2, j+=1)
    {
        buffer[j] = buffer[i]; 
    }

}

void ComposeSetupBuffer(USB_SetupPacket_t &sp, unsigned char buffer[8])
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

void HostTestApp(chanend c_out, chanend c_in, chanend c_out2, chanend c_in1)
{
    USB_BmRequestType_t bmRequestType;
    USB_SetupPacket_t sp;
    unsigned char productString[64];
    unsigned char buffer[64];
    unsigned char spBuffer[MAX_EP0_PKT_SIZE];
    int length;
    int tmp;

    XUH_Ep ep_out = XUH_InitEp(c_out);
    XUH_Ep ep_out2 = XUH_InitEp(c_out2);
    XUH_Ep ep_in = XUH_InitEp(c_in);
    XUH_Ep ep_in1 = XUH_InitEp(c_in1);
    
    /* Attempt to do a GetDesc(Device) */ 
    length =  XUH_GetDescriptor(ep_out, ep_in, buffer,  USB_WVALUE_GETDESC_DEV, 64, 0);

    tmp = buffer[USB_DEV_DESC_IPRODUCT];

    length = XUH_GetDescriptor(ep_out, ep_in, productString,  USB_WVALUE_GETDESC_STRING | tmp, 64, 0);

    length = XUH_GetDescriptor(ep_out, ep_in, buffer,  USB_WVALUE_GETDESC_CONFIG, 10, 0);

    length = buffer[USB_CONFIG_DESC_WTOTALLENGTH];

    /* Get the rest of the config desc */
    length = XUH_GetDescriptor(ep_out, ep_in, buffer,  USB_WVALUE_GETDESC_CONFIG, length, 0);


#if 0
    printstr("Found device: ");
    length = productString[0]>>1;
    UnUnicode(productString);
    for(int i = 0; i < length-1; i++)
        printchar(productString[i]);
#endif

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

    /* Device kinda up and running now.. lets try and do some mass storage stuff... */
    MassStorage(ep_out2, ep_in1);

    while(1);    

}
#define NUM_EPS 3

int main()
{
    chan xuhChans_out[NUM_EPS];
    chan xuhChans_in[NUM_EPS];

    p_gpio <: (0 << PORT_32A_VBUS_OUT_EN_N_BITSHIFT) | (1 << PORT_32A_EN_5VA_BITSHIFT) | (1 << PORT_32A_USB_SEL_1_BITSHIFT);

   // printstr("XUD TEST\n");

    par
    {
        XUH_Manager(xuhChans_out, NUM_EPS, xuhChans_in, NUM_EPS);

        HostTestApp(xuhChans_out[0], xuhChans_in[0], xuhChans_out[2], xuhChans_in[1]);
    }
}

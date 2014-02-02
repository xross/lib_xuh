
#include <xs1.h>
#include <print.h>
#include "gpioDefines.h"

#include "usb.h"
#include "usb_defs.h"
#include "xuh.h"

out port p_gpio = XS1_PORT_32A;

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

void XUH_ControlTransfer_In(XUH_Ep ep_out, XUH_Ep ep_in, USB_SetupPacket_t sp, unsigned char buffer[])
{
    unsigned char spBuffer[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    unsigned length;

    timer t;
    unsigned time;


    t :> time;
    t when timerafter(time + 80000000) :> void;
    
    // TODO:
    ComposeSetupBuffer(sp, spBuffer);

    XUH_SetupTransfer(ep_out, spBuffer);
    
    length = XUH_InTransfer(ep_in, buffer);

	XUH_OutTransfer(ep_out, spBuffer, 0);

}

void HostTestApp(chanend c_out, chanend c_in)
{
    USB_BmRequestType_t bmRequestType;
    USB_SetupPacket_t sp;

    unsigned char buffer[64];
    int length;

    XUH_Ep ep_out = XUH_InitEp(c_out);
    XUH_Ep ep_in = XUH_InitEp(c_in);
  
    
    /* Attempt to do a GetDesc(Device) */ 
    bmRequestType.Recipient = USB_BM_REQTYPE_RECIP_DEV;
    bmRequestType.Type = USB_BM_REQTYPE_TYPE_STANDARD;
    bmRequestType.Direction = USB_BM_REQTYPE_DIRECTION_D2H;
   
    sp.bmRequestType = bmRequestType;
    sp.bRequest = USB_GET_DESCRIPTOR;
    sp.wValue = USB_WVALUE_GETDESC_DEV;
    sp.wIndex = 0;
    sp.wLength = 64;
    
    XUH_ControlTransfer_In(ep_out, ep_in, sp, buffer);
 
 
    while(1);    

}
#define NUM_EPS 1

int main()
{
    chan xuhChans_out[NUM_EPS];
    chan xuhChans_in[NUM_EPS];

    p_gpio <: (0 << PORT_32A_VBUS_OUT_EN_N_BITSHIFT) | (1 << PORT_32A_EN_5VA_BITSHIFT) | (1 << PORT_32A_USB_SEL_1_BITSHIFT);

    printstr("XUD TEST\n");

    par
    {
        XUH_Manager(xuhChans_out, NUM_EPS, xuhChans_in, NUM_EPS);

        HostTestApp(xuhChans_out[0], xuhChans_in[0]);
    }
}

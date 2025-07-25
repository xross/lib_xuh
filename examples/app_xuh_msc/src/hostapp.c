
#include <xccompat.h>
#include <stdio.h>

#include "usb_defs.h"
#include "xuh.h"
#include "usbh_descriptors.h"
#include "usb_std_descriptors.h" // DescCOmp return only TODO RM me

#include "usbh.h"
#include "usbh_class_msc.h"

#ifdef DEBUG
#include "usbh_dbgdescs.h"
#endif

#include <print.h>



#define USB_DUMMY_DESCS_HID 1

#define MAX_EP0_PKT_SIZE 64

#define DEV_ADDR 3
#define USBH_CLASS_MSC 1

static USBH_Driver_t const usbh_class_drivers[USB_CLASS_MAPPED_INDEX_END] =
{
#if (USB_CLASS_MSC)
    [USB_CLASS_MSC] = {
        .init                       = USBH_MSC_Init,
        .DescComp_InterfaceEndpoint   = USBH_MSC_DescComp_NextInterfaceEp,
        .DescComp_Interface     = USBH_MSC_DescComp_NextInterface,
        //.task = USBH_MSC_Main,
    },
#endif

};

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

static inline void USB_GetNextDescriptor(unsigned short* const BytesRem, void** CurrConfigLoc)
{
    unsigned short CurrDescriptorSize = (*((USB_Descriptor_Header_t*)(*CurrConfigLoc))).bLength;

    unsigned char *x  = *CurrConfigLoc;

    if (*BytesRem < CurrDescriptorSize)
    {
        CurrDescriptorSize = *BytesRem;
        printhexln(CurrDescriptorSize);
    }

    *CurrConfigLoc  = (void*)((unsigned)*CurrConfigLoc + CurrDescriptorSize);
    *BytesRem      -= CurrDescriptorSize;
}


#define DESCRIPTOR_SEARCH_NotFound 0
#define DESCRIPTOR_SEARCH_Fail 1

USB_DescSearchResult_t USB_GetNextDescriptorComp(unsigned * const BytesRem,
                                  void** const CurrConfigLoc, // buffer
                                  unsigned const (*comparatorFunc)(void*)) /* Function ptr to desc compare func */
{
    USB_DescSearchResult_t returnVal;

    while (*BytesRem)
    {
        unsigned char *PrevDescLoc  = *CurrConfigLoc;
        uint16_t PrevBytesRem = *BytesRem;

        USB_GetNextDescriptor(BytesRem, CurrConfigLoc);

        /* Inspect current descriptor */
        returnVal = comparatorFunc(*CurrConfigLoc);
        {


            // The following is returned if another interface is found before the EP
            // TODO do this interface check at this level?
            if (returnVal == USB_DESCSEARCH_FAIL)
            {
                printstrln("FAIL");
                //*CurrConfigLoc = PrevDescLoc;
                //*BytesRem      = PrevBytesRem;
            }
            /* Match */
           // printstr("GetNextDescComp: found\n");

            //USB_GetNextDescriptor(BytesRem, CurrConfigLoc);
            return returnVal;
        }
        /* Move on to next Desc */

    }

    /* No match! */
    return USB_DESCSEARCH_FAIL;
}

void USBHost(chanend c_out, chanend c_in, chanend c_out2, chanend c_in1)
{
    USB_BmRequestType_t bmRequestType;
    USB_SetupPacket_t sp;
    unsigned char buffer[1024];
    unsigned char spBuffer[MAX_EP0_PKT_SIZE];
    int length;
    int tmp;

    USB_Descriptor_Device_t deviceDesc;
    USB_Descriptor_String_t productStringDesc;

#ifndef DEBUG
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
    length = XUH_GetDescriptor(ep_out, ep_in, (unsigned char*) &productStringDesc,  USB_WVALUE_GETDESC_STRING | 3, 64, 0);

#if 0
    printstr("Found device: ");
    UnUnicode(buffer, (unsigned char*) &productStringDesc, productStringDesc.bLength);
    for(int i = 0; i < productStringDesc.bLength/2-1; i++)
        printchar(productStringDesc.bString[i]);

#endif
#endif

    /* Get Config Descriptor header */
    unsigned short configHeader[sizeof(USB_Descriptor_Configuration_Header_t)];

#ifndef DEBUG
    length = XUH_GetDescriptor(ep_out, ep_in, configHeader,  USB_WVALUE_GETDESC_CONFIG, sizeof(USB_Descriptor_Configuration_Header_t), 0);
    //printintln(configHeader[offsetof(USB_Descriptor_Configuration_Header_t, bMaxPower)]);
#endif

    unsigned *configSizePtr = ((USB_Descriptor_Configuration_Header_t*)configHeader)->wTotalLength;

#if defined(DEBUG) && (USB_DUMMY_DESCS_HID == 1)
    for(int i = 0; i < sizeof(dummyConfigDesc_MSC); i++)
        buffer[i] = dummyConfigDesc_MSC[i];

    configSizePtr = sizeof(dummyConfigDesc_MSC);
#else
    /* Get the rest of the Config Descriptor */
    XUH_GetDescriptor(ep_out, ep_in, buffer,  USB_WVALUE_GETDESC_CONFIG, configSizePtr, 0);
#endif

    /* PARSE CONFIG DESC FOR EP ADDRESS */

    /* - Run though descirptors.
     * - Try and match with class drivers in array (call desc comp function in class struct)
     *      - Pass in a "device accepted" flag to signal we are happy with the dev (and stop parse)
     * - If match then:
     *      - SetConfig
     *      - Run User function (pass in user eps)
     *
     */
    {
        unsigned devAccepted = 0;
        USB_DescSearchResult_t result;
        USB_Descriptor_Interface_t* interface = NULL;

        unsigned char *buffPtr = buffer;

        while(!devAccepted)
        {
            /* Get the next EP desc */
            /* TODO Use DescComp func from drivers array */
            /* Get the next Endpoint desc */

            if(!(interface) ||
                    ((result = USB_GetNextDescriptorComp(&configSizePtr, &buffPtr,
                    USBH_MSC_DescComp_NextInterfaceEp)) < 0))//!= USB_DESCSEARCH_MATCH)) // or ACCEPT
            {
                /* Find next MSC interface.. */
                if(USB_GetNextDescriptorComp(&configSizePtr, &buffPtr,  USBH_MSC_DescComp_NextInterface) != USB_DESCSEARCH_MATCH)
                {
                    printstr("End of desciptors hit\n");
                    break;
                }

                /* Update interface */
			    interface = (USB_Descriptor_Interface_t*) buffPtr;

                // TODO:    Save Ep Data to an array or similar (to pass to app EP cores)
                // TODO:    Need to set devAccepted to 1 at some point! - Pass to NextInterfaceEP?
                // What if we can accept a device with different endpoint counts (e.g. audio dev could input/output)
                continue;
            }
            else
            {
		        USB_Descriptor_Endpoint_t* EndpointDesc = (USB_Descriptor_Endpoint_t*)buffPtr;
                /* Found an EP! Retrieve the endpoint address from the endpoint descriptor */
                //printstr("Found ep at addr: ");
                //printhexln(EndpointDesc->bEndpointAddress);

                if(result == USB_DESCSEARCH_ACCEPT)
                {
                    //printstr("Accepting Device\n");
                    devAccepted = 1;
                }
            }
        }

        if(devAccepted)
        {
            //printstrln("Enumerating dev. Doing a SetConfig()!");
        }
        else
        {
            /* Couldn't enum device. */
            /* TODO need to wait for a new device */
            while(1);
        }
    }

    //TODO Assign addreses to the EP resources


#if 1
//#ifndef DEBUG
    /* Set Config() */
    bmRequestType.Recipient = USB_BM_REQTYPE_RECIP_DEV;
    bmRequestType.Type = USB_BM_REQTYPE_TYPE_STANDARD;
    bmRequestType.Direction = USB_BM_REQTYPE_DIRECTION_H2D;

    sp.bmRequestType = bmRequestType;
    sp.bRequest = USB_SET_CONFIGURATION;
    sp.wValue = 1;
    sp.wIndex = 0;
    sp.wLength = 0;

    USB_ComposeSetupBuffer(sp, spBuffer);

    XUH_SetupTransfer(ep_out, spBuffer);

    length = XUH_InTransfer(ep_in, buffer);

    delay(5000000);
    /* Device kinda up and running now.. lets try and do some mass storage stuff... */

    //TODO call the right func based on device!!
    MassStorage(ep_out, ep_in, ep_out2, ep_in1);
#endif

    while(1);
}

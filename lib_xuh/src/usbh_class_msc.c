
#include "usb_defs.h"
#include "usbh_descriptors.h"
#include "usb_std_descriptors.h"
#include "usbh_class_msc.h"
#include "print.h"

unsigned g_MSC_EpCount_In = 0;
unsigned g_MSC_EpCount_Out = 0;

/* Return success if this is a valid/suitable EP descriptor */
USB_DescSearchResult_t USBH_MSC_DescComp_NextInterfaceEp(void * configDesc)
{
    USB_Descriptor_Header_t* Header = (USB_Descriptor_Header_t*) configDesc;

    /* First check if we are looking at an EP descriptor.. */
    /* TODO Only pass EP descs into this func? */
    if (Header->bDescriptorType == USB_DESCTYPE_ENDPOINT)
    {
        USB_Descriptor_Endpoint_t* endpointDesc = (USB_Descriptor_Endpoint_t*)configDesc;

        /* Check the endpoint type, break out if correct BULK type endpoint found */
        if ((endpointDesc->bmAttributes & USB_EPTYPE_MASK) == USB_EPTYPE_BULK)
        {
            if(endpointDesc->bEndpointAddress & USB_EPDIR_MASK)
            {
                g_MSC_EpCount_In++;
            }
            else
            {
                g_MSC_EpCount_Out++;
            } 

            if((g_MSC_EpCount_Out == 1) && (g_MSC_EpCount_In == 1))
            {
                return USB_DESCSEARCH_ACCEPT;
            }
            return USB_DESCSEARCH_MATCH;
        }
    }
    else if (Header->bDescriptorType == USB_DESCTYPE_INTERFACE)
    {
        /* Read past the current interface */
        return USB_DESCSEARCH_FAIL;
    }
   
    /* Didn't match this time.. */
    return USB_DESCSEARCH_NOMATCH;
}

/* Return USB_DESCSEARCH_MATCH if match the inteface
 * Check for MSC interface 
 * Check EP count
 * */
USB_DescSearchResult_t USBH_MSC_DescComp_NextInterface(void * curDesc)
{
    USB_Descriptor_Header_t* Header = (USB_Descriptor_Header_t*)curDesc;
    g_MSC_EpCount_In = 0;
    g_MSC_EpCount_Out = 0;

    /* Look at descriptor type */
    /* TODO should we only pass interface descs to this func? */
    if (Header->bDescriptorType == USB_DESCTYPE_INTERFACE)
	{
		USB_Descriptor_Interface_t* Interface = (USB_Descriptor_Interface_t*)curDesc;

		/* Check the descriptor class and protocol, break out if correct class/protocol interface found */
		if ((Interface->bInterfaceClass    == USB_CLASS_MASS_STORAGE)    &&
		    (Interface->bInterfaceSubClass == MASS_STORE_SUBCLASS) &&
		    (Interface->bInterfaceProtocol == MASS_STORE_PROTOCOL))
		{
            /* Check EP count */ 
            if(Interface->bNumEndpoints == 2)
            {
                /* Match! */
                return USB_DESCSEARCH_MATCH;
            }
        }
	}
    return USB_DESCSEARCH_NOMATCH;
}

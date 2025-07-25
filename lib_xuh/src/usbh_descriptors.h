
#ifndef _usb_descriptors_h_
#define _usb_descriptors_h_

#define uint8_t unsigned char
#define uint16_t unsigned short

/* General USB */

#define USB_EPDIR_MASK              0x80
#define USB_EPTYPE_MASK             0x03
enum USB_EndpointType_t
{
    USB_EPTYPE_CONTROL              = 0x00,
    USB_EPTYPE_ISOCHRONOUS          = 0x01,
    USB_EPTYPE_BULK                 = 0x02,
    USB_EPTYPE_INTERRUPT            = 0x03
};

/* Enums/structs for Standard Descriptors */


#if 0
/* USB Standard Endpoint Descriptor (section 9.6.1 table 9-13) */
typedef struct
{
    unsigned char bLength;             /* Size of the descriptor (bytes) */
    unsigned char bDescriptorType;     /* Descriptor type, either a value. See \ref USB_DescriptorTypes_t or 
                                  * a value given by the specific class */
    unsigned char  bEndpointAddress;   /* Address of the endpoint, includes a direction mask */
    unsigned char  bmAttributes;       /* Endpoint attributes, comprised of a mask of the endpoint type 
                                  * See EP_TYPE_ ad EP_ADDR) */
    unsigned short wMaxPacketSize;     /* Maximum packet size (bytes) that the endpoint can receive */
    unsigned char  bInterval;          /* Polling interval in milliseconds for the endpoint. 
                                  * Relevant to Isocornous and Interrupt endpoints only */
} USB_Descriptor_Endpoint_t;


/* USB Standard Interface Descriptor (section 9.6.1 table 9-12) */
typedef struct
{
    unsigned char bLength;             /* Size of the descriptor (bytes) */
    unsigned char bDescriptorType;     /* Type of the descriptor, either a value in \ref USB_DescriptorTypes_t 
                                        * or a value given by the specific class */
    unsigned char bInterfaceNumber;    /* Index of the interface in the current config */
	unsigned char bAlternateSetting;   /* Alternate setting for this interface number. Multiple alternatives
                                        * are supported per interface (with different EP configs) */
    unsigned char bNumEndpoints;       /* Total endpoint count in this interface */
    unsigned char bInterfaceClass;     /* Interface class code */

    unsigned char bInterfaceSubClass;  /* Interface subclass code */
    unsigned char bInterfaceProtocol;  /* Interface protocol code */
    unsigned char iInterface;          /* Index of the string descriptor in the string table */
} USB_Descriptor_Interface_t;

/* USB Standard Device Descriptor (section 9.6.1, table 9-8) */
typedef struct
{
    unsigned char bLength;              /* Size of the descriptor (bytes) */
    unsigned char bDescriptorType;      /* Descriptor type, either a value in \ref USB_DescriptorTypes_t 
                                         * or a value given by the specific class */
    unsigned short bcdUSB;              /* Supported USB version */
    unsigned char  bDeviceClass;        /* USB device class code */
    unsigned char  bDeviceSubClass;     /* USB device subclass code */
    unsigned char  bDeviceProtocol;     /* USB device protocol code */
    unsigned char  bMaxPacketSize0;     /* Maximum packet size for endpoint 0 (bytes) */
    unsigned short idVendor;            /* Vendor ID */
    unsigned short idProduct;           /* Product ID */
    unsigned short bcdDevice;           /* Device release number in binary-coded decimal */
    unsigned char  iManufacturer;       /* Index of string descriptor describing manufacturer */
    unsigned char  iProduct;            /* Index of string descriptor describing product */
    unsigned char  iSerialNumber;       /* Index of String descriptor describing the devices serial number */
    unsigned char  bNumConfigurations;  /* Total number of configurations supported by the device */
} USB_Descriptor_Device_t;



/* USB Standard Configuration Descriptor (section 9.6.1 table 9-10) */
typedef struct
{
    unsigned char  bLength;             /* Size of the descriptor (bytes) */
    unsigned char  bDescriptorType;     /* Type of the descriptor, either a value in \ref USB_DescriptorTypes_t or a value
                                         * given by the specific class */
    unsigned short wTotalLength;        /* Size of the configuration descriptor header and all sub descriptors inside
                                         * the configuration */
    unsigned char  bNumInterfaces;      /* Total interface count in the configuration */
    unsigned char  bConfigurationValue; /* Value to use as an argument to the SetConfiguration() request to select this 
                                         * configuration */
    unsigned char  iConfiguration;      /* Index of string descriptor describing this configuration */
    unsigned char  bmAttributes;        /* Configuration characteristics 
                                         * D7: Reserved (set to one)
                                         * D6: Self-powered
                                         * D5: Remote Wakeup
                                         * D4...0: Reserved (reset to zero) 
                                        */
    unsigned char  bMaxPower;           /* Maximum power consumption of the USB device from the bus in this specific 
                                         * configuration when the device is fully operational. Expressed in 2 mA units 
                                         * (i.e., 50 = 100 mA) */
} USB_Descriptor_Configuration_Header_t;
#endif

/* Return val for descriptor parsing functions */
/* TODO move me */
typedef enum USB_DescSearchResult_t
{
    USB_DESCSEARCH_NOMATCH = -2,
    USB_DESCSEARCH_FAIL = -1,
    USB_DESCSEARCH_MATCH = 0,
    USB_DESCSEARCH_ACCEPT = 1

} USB_DescSearchResult_t;


#endif

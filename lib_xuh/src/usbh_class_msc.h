		
/* TODO Enums */
#define MASS_STORE_SUBCLASS            0x06
#define MASS_STORE_PROTOCOL            0x50
void USBH_MSC_Init();
USB_DescSearchResult_t USBH_MSC_DescComp_NextInterfaceEp(void * configDesc);
USB_DescSearchResult_t USBH_MSC_DescComp_NextInterface(void * configDesc);



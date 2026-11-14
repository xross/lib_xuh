#include <stdio.h>

#include "xuh_host.h"
#include "xuh_msc.h"
#include "activity_led.h"
#include "hostapp.h"

/* In msctest.c: mount the volume and play from it. */
void MassStorageApp(
#if APP_XUH_MSC_ENABLE_LCD
                    chanend c_lcd_image
#if APP_XUH_MSC_MPEG_AUDIO
                    , chanend c_audio
#endif
#elif APP_XUH_MSC_MPEG_AUDIO
                    chanend c_audio
#else
                    void
#endif
                    );

/* lib_xuh reports what it is doing on the bus: show it on the LED. */
XUH_ACTIVITY_CALLBACK static void usb_activity(XUH_Activity activity)
{
    activity_led_usb(activity == XUH_ACTIVITY_IDLE ? 0 :
                     activity == XUH_ACTIVITY_ERROR ? ACTIVITY_LED_ERROR :
                                                      ACTIVITY_LED_BUSY);
}

void USBHost(chanend c_out[], unsigned n_out, chanend c_in[], unsigned n_in
#if APP_XUH_MSC_ENABLE_LCD
             , chanend c_lcd_image
#endif
#if APP_XUH_MSC_MPEG_AUDIO
             , chanend c_audio
#endif
             )
{
    static XUH_Host host;
    static XUH_Device device;
    static XUH_Msc msc;
    XUH_Status status;

    /* Slot 0 in each direction is the control endpoint; the rest are for
     * class drivers. */
    XUH_Host_Init(&host, c_out[0], c_in[0], &c_out[1], n_out - 1, &c_in[1], n_in - 1);
    XUH_Host_SetActivityCallback(&host, usb_activity);

    /* lib_xuh enumerates one device after reset: a second
     * XUH_Host_WaitForDevice() fails, which ends this loop. */
    while ((status = XUH_Host_WaitForDevice(&host, &device)) == XUH_OK) {
        printf("USB: device %04x:%04x, %u interface(s)\n",
               device.vendorId, device.productId, device.interfaceCount);

        status = XUH_Msc_Open(&msc, &host, &device);
        if (status == XUH_OK) {
            /* A mass storage device: it is now FatFs drive 0. */
            printf("USB: mass storage, %u blocks of %u bytes\n",
                   (unsigned)msc.blockCount, (unsigned)msc.blockSize);
            MassStorageApp(
#if APP_XUH_MSC_ENABLE_LCD
                           c_lcd_image
#if APP_XUH_MSC_MPEG_AUDIO
                           , c_audio
#endif
#elif APP_XUH_MSC_MPEG_AUDIO
                           c_audio
#endif
                           );
        } else if (status == XUH_ERR_UNSUPPORTED) {
            printf("USB: not a supported mass storage device\n");
        } else {
            printf("USB: mass storage open failed, status=%d\n", status);
        }
    }
    if (status != XUH_ERR_UNSUPPORTED)
        printf("USB: enumeration failed, status=%d\n", status);

    while (1);
}

#ifndef HOSTAPP_H_
#define HOSTAPP_H_

#include <xccompat.h>

/* USB host application task. Runs on the tile of XUH_Manager(), connected to
 * it by all of its channels. */
void USBHost(chanend c_out[], unsigned n_out, chanend c_in[], unsigned n_in
#if APP_XUH_MSC_ENABLE_LCD
             , chanend c_lcd_image
#endif
#if APP_XUH_MSC_MPEG_AUDIO
             , chanend c_audio
#endif
             );

#endif

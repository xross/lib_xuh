#ifndef HOSTAPP_H_
#define HOSTAPP_H_

#include <xccompat.h>

/* USB host application task. Runs on the tile of XUH_Manager(), connected to
 * it by all of its channels. c_load carries loaded video to the decoder. */
void USBHost(chanend c_out[], unsigned n_out, chanend c_in[], unsigned n_in,
             chanend c_load
#if APP_XUH_MSC_PLAYER_MPEG_AUDIO
             , chanend c_audio
#endif
             );

#endif

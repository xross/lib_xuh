#ifndef APP_FATFS_CONF_H_
#define APP_FATFS_CONF_H_
/* The one filesystem task owns FatFs. A static LFN workspace permits descriptive
 * input names such as VIDEO_320x180.MPG without heap or extra task stack. */
#define FF_USE_LFN 1
#endif

#ifndef YUV_RGB565_H_
#define YUV_RGB565_H_
#include <stdint.h>

/* Two rows of planar limited-range BT.601 YUV420: Y0,Y1,Cb,Cr, without padding.
 * Destination holds two rows of big-endian RGB565 pixels. Width must be even. */
void yuv420_strip_to_rgb565(const uint8_t *strip, uint8_t *destination, unsigned width);
#endif

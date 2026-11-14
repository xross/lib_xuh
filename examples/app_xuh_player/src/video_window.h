#ifndef VIDEO_WINDOW_H_
#define VIDEO_WINDOW_H_
#include "video_config.h"

/* Screen coordinates are independent of the decoded picture's dimensions.
 * MPEG rows cross the channel in words; chroma covers pairs of luma rows. A
 * chroma row is half the width, so the width must be a multiple of eight for
 * it to be a whole number of words. */
typedef struct {
    unsigned x, y, width, height;
} video_window_t;

static inline int video_window_valid(unsigned width, unsigned height)
{
    return width && height && width <= PIPELINE_LCD_WIDTH &&
        height <= PIPELINE_LCD_HEIGHT && !(width & 7) && !(height & 1);
}

static inline video_window_t video_window_center(unsigned width, unsigned height)
{
    video_window_t window = {(PIPELINE_LCD_WIDTH - width) / 2,
        (PIPELINE_LCD_HEIGHT - height) / 2, width, height};
    return window;
}

static inline unsigned video_window_rgb_offset(video_window_t window, unsigned row)
{
    return ((window.y + row) * PIPELINE_LCD_WIDTH + window.x) * 2;
}
#endif

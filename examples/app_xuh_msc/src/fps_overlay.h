#ifndef FPS_OVERLAY_H_
#define FPS_OVERLAY_H_

/* On-screen frame-rate readout for the video pipelines, e.g. "17.2 FPS" in the
 * top-left corner. Build with -DAPP_XUH_MSC_FPS_OVERLAY=0 to leave frames
 * untouched (required when frame hashes are compared with a reference). */
#ifndef APP_XUH_MSC_FPS_OVERLAY
#define APP_XUH_MSC_FPS_OVERLAY 1
#endif

typedef struct {
    unsigned window_start;  /* Reference time (100 MHz ticks) the window began. */
    unsigned frames;        /* Frames counted in the current window. */
    unsigned tenths;        /* Last completed measurement, in 0.1 FPS. */
    unsigned started;
} fps_overlay_t;

/* Count one frame staged for display at reference time `now`, then draw the
 * most recent measurement into a PIPELINE_LCD_WIDTH-wide big-endian RGB565
 * frame. The rate is the mean over windows of about half a second; nothing is
 * drawn until the first window completes. Reset by zeroing the state. */
void fps_overlay_frame(fps_overlay_t *state, unsigned now, unsigned char *frame);
#endif

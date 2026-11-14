#ifndef FPS_OVERLAY_H_
#define FPS_OVERLAY_H_

/* On-screen FPS gauge: measured-rate bar with a muted spatial gradient
 * from brick red through amber to leaf green at the nominal source rate,
 * with a white source-rate tick and numeric readout in the top-left corner.
 * Build with -DAPP_XUH_PLAYER_FPS_OVERLAY=0 to leave frames
 * untouched (required when frame hashes are compared with a reference). */
#ifndef APP_XUH_PLAYER_FPS_OVERLAY
#define APP_XUH_PLAYER_FPS_OVERLAY 1
#endif

/* Fixed physical screen rectangle, never relative to a video window. */
enum { FPS_OVERLAY_X = 4, FPS_OVERLAY_Y = 4,
       FPS_OVERLAY_WIDTH = 104, FPS_OVERLAY_HEIGHT = 22 };

typedef struct {
    unsigned window_start;  /* Reference time (100 MHz ticks) the window began. */
    unsigned frames;        /* Frames counted in the current window. */
    unsigned tenths;        /* Smoothed measurement, in 0.1 FPS. */
    unsigned started;
    unsigned measured;
    unsigned target_tenths; /* Nominal source FPS x10; zero defaults to 25 for MJPEG. */
} fps_overlay_t;

/* Count one frame staged for display at reference time `now`, then draw the
 * most recent measurement into a PIPELINE_LCD_WIDTH-wide big-endian RGB565
 * frame. Half-second averages are smoothed with the preceding reading. The
 * gauge shows a placeholder until the first measurement. Reset by zeroing
 * the state. Set target_tenths from source metadata. The scale is at least
 * 40 FPS and expands for higher-rate sources. No extra framebuffer is allocated. */
void fps_overlay_frame(fps_overlay_t *state, unsigned now, unsigned char *frame);
#endif

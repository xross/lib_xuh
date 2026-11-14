#ifndef FPS_OVERLAY_H_
#define FPS_OVERLAY_H_

/* On-screen FPS gauge: text-width 0..40 FPS bar with a fixed, muted spatial
 * gradient from brick red through amber to leaf green at 25 FPS,
 * with a white 25-FPS target tick and numeric readout in the top-left corner.
 * Build with -DAPP_XUH_PLAYER_FPS_OVERLAY=0 to leave frames
 * untouched (required when frame hashes are compared with a reference). */
#ifndef APP_XUH_PLAYER_FPS_OVERLAY
#define APP_XUH_PLAYER_FPS_OVERLAY 1
#endif

typedef struct {
    unsigned window_start;  /* Reference time (100 MHz ticks) the window began. */
    unsigned frames;        /* Frames counted in the current window. */
    unsigned tenths;        /* Smoothed measurement, in 0.1 FPS. */
    unsigned started;
    unsigned measured;
} fps_overlay_t;

/* Count one frame staged for display at reference time `now`, then draw the
 * most recent measurement into a PIPELINE_LCD_WIDTH-wide big-endian RGB565
 * frame. Half-second averages are smoothed with the preceding reading. The
 * gauge shows a placeholder until the first measurement. Reset by zeroing
 * the state. No extra framebuffer is allocated. */
void fps_overlay_frame(fps_overlay_t *state, unsigned now, unsigned char *frame);
#endif

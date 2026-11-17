#ifndef VIDEO_CONFIG_H_
#define VIDEO_CONFIG_H_

/* Portable display geometry and pipeline selection; no XCORE resource types. */
#ifndef APP_XUH_MSC_PLAYER_ENABLE_MPEG1
#define APP_XUH_MSC_PLAYER_ENABLE_MPEG1 0
#endif
#if APP_XUH_MSC_PLAYER_ENABLE_MPEG1 && APP_XUH_MSC_PLAYER_ENABLE_JPEG
#error Select one video decoder per build configuration
#endif
#define MSC_JPEG_PIPELINE (APP_XUH_MSC_PLAYER_ENABLE_JPEG && !APP_XUH_MSC_PLAYER_LCD_TEST_MODE)
#define MSC_MPEG_PIPELINE (APP_XUH_MSC_PLAYER_ENABLE_MPEG1 && !APP_XUH_MSC_PLAYER_LCD_TEST_MODE)
#define MSC_VIDEO_PIPELINE (MSC_JPEG_PIPELINE || MSC_MPEG_PIPELINE)
#define PIPELINE_LCD_WIDTH 320
#define PIPELINE_LCD_HEIGHT 240
#define PIPELINE_TICKS_PER_SECOND 100000000u

/* Player settings. Change them here, not in CMake.
 *
 * Memory given to the video decoder when playback is paced to audio.
 * lib_mpeg divides it between the pictures and compressed input; the input
 * part is where video waits for its audio. This default completed the
 * reference 320x180 program stream. Video-only and unpaced builds use the
 * library's normal arena. */
#ifndef APP_XUH_MSC_PLAYER_MPEG_ARENA_BYTES
#define APP_XUH_MSC_PLAYER_MPEG_ARENA_BYTES (400 * 1024)
#endif
#if APP_XUH_MSC_PLAYER_MPEG_ARENA_BYTES < 384 * 1024 || (APP_XUH_MSC_PLAYER_MPEG_ARENA_BYTES % 8)
#error APP_XUH_MSC_PLAYER_MPEG_ARENA_BYTES must be at least 393216 and a multiple of eight
#endif

/* Time from a decoded picture being handed over to the end of its write to
 * the display, in ms: colour conversion and the SPI transfer. Paced playback
 * releases each decoded picture this much before it is due. It depends on the
 * picture size, the display and the SPI rate, not on the content: decoding is
 * finished before the picture is held. About 25 ms for 320x180 pictures on the
 * ILI9341 at 50 MHz SPI. The panel's own refresh delay is not included. */
#ifndef APP_XUH_MSC_PLAYER_DISPLAY_LATENCY_MS
#define APP_XUH_MSC_PLAYER_DISPLAY_LATENCY_MS 25
#endif
#if APP_XUH_MSC_PLAYER_DISPLAY_LATENCY_MS < 0 || APP_XUH_MSC_PLAYER_DISPLAY_LATENCY_MS > 500
#error APP_XUH_MSC_PLAYER_DISPLAY_LATENCY_MS must be between 0 and 500
#endif
#endif

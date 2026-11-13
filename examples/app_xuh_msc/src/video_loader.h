#ifndef VIDEO_LOADER_H_
#define VIDEO_LOADER_H_
#include "jpeg_pipeline.h"

#if MSC_VIDEO_PIPELINE && !defined(__XC__)
/* Storage-independent read-ahead loader shared by the MJPEG and MPEG-1 players.
 *
 * A source supplies "units": one complete JPEG frame, or one chunk of an MPEG
 * elementary stream. The loader fills the alternate input slot while the
 * decoder still owns the current one, so storage reads overlap decoding.
 *
 * Loader -> decoder: ready [slot, sequence, bytes], finally stop
 *                    [PIPELINE_STOP, error, 0].
 * Decoder -> loader: ack [sequence, failed] once it no longer reads that slot;
 *                    after stop, PIPELINE_LOADER_STATS_WORDS statistics.
 * The loader receives the previous unit's ack before publishing the next. */
/* xcore needs every indirect call grouped with its possible targets to size
 * stacks. Declare each source callback with the matching VIDEO_SOURCE_* macro. */
#ifdef __xcore__
#define VIDEO_SOURCE_GROUP(name) __attribute__((fptrgroup("video_source_" #name)))
#else
#define VIDEO_SOURCE_GROUP(name)
#endif
#define VIDEO_SOURCE_OPEN   VIDEO_SOURCE_GROUP(open)
#define VIDEO_SOURCE_READ   VIDEO_SOURCE_GROUP(read)
#define VIDEO_SOURCE_CLOSE  VIDEO_SOURCE_GROUP(close)
#define VIDEO_SOURCE_BUFFER VIDEO_SOURCE_GROUP(buffer)

typedef struct {
    const char *name;
    void *context;
    /* 0, or a non-zero storage error code. */
    VIDEO_SOURCE_OPEN int (*open)(void *context);
    /* 1: unit read, 0: clean end of input, -1: read/format/capacity error. */
    VIDEO_SOURCE_READ int (*read)(void *context, unsigned char *destination, unsigned capacity, unsigned *bytes);
    /* 0, or a non-zero storage error code. */
    VIDEO_SOURCE_CLOSE int (*close)(void *context);
    VIDEO_SOURCE_BUFFER unsigned char *(*buffer)(unsigned slot);
    unsigned capacity;
} video_source_t;

typedef struct {
    unsigned units;                 /* Units published to the decoder. */
    unsigned failed;                /* Open, read, close or decoder-reported failure. */
    unsigned start;                 /* Reference time at the start of the play. */
    unsigned long long load_us, elapsed_ticks;
    unsigned stats[PIPELINE_LOADER_STATS_WORDS];
} video_play_t;

/* Runs one complete play. Always completes the stop/statistics exchange, so the
 * pipeline tasks are ready for another play whatever the result. */
void video_loader_play(chanend c_load, const video_source_t *source, video_play_t *play);

/* Prints completed-frame FPS, startup latency and the stage means common to
 * both players. Call only when play->stats[PIPELINE_FRAMES] is non-zero. */
void video_loader_report(unsigned number, const video_play_t *play);
#endif
#endif

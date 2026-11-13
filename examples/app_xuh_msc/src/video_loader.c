#include "video_loader.h"

#if MSC_VIDEO_PIPELINE
#include <stdio.h>
#include <string.h>
#include <xcore/hwtimer.h>
#include "mpeg_pipeline.h"

void video_loader_play(chanend c_load, const video_source_t *source, video_play_t *play)
{
    unsigned pending = 0, ack[2];
    memset(play, 0, sizeof(*play));
    int open_rc = source->open(source->context);
    printf("%s open: rc=%d\n", source->name, open_rc);
    if (open_rc) play->failed = 1;
    unsigned previous = play->start = get_reference_time();
    while (!play->failed) {
        unsigned slot = play->units & 1, bytes = 0;
        unsigned load_start = get_reference_time();
        int result = source->read(source->context, source->buffer(slot), source->capacity, &bytes);
        play->load_us += (get_reference_time() - load_start) / 100;

        /* While the alternate buffer was being filled, the decoder consumed
         * the previous unit. It must release ownership before publication. */
        if (pending) {
            pipeline_receive(c_load, ack, 2);
            pending = 0;
            if (ack[0] != play->units - 1 || ack[1]) play->failed = 1;
        }
        if (result < 0) {
            printf("%s: read/format/capacity error at unit %u, bytes=%u\n",
                   source->name, play->units, bytes);
            play->failed = 1;
        }
        if (result <= 0 || play->failed) break;
        unsigned ready[3] = {slot, play->units, bytes};
        pipeline_send(c_load, ready, 3);
        ++play->units;
        pending = 1;
        unsigned now = get_reference_time();
        play->elapsed_ticks += (unsigned)(now - previous);
        previous = now;
    }
    if (!open_rc) {
        int close_rc = source->close(source->context);
        if (close_rc) { printf("%s close: rc=%d\n", source->name, close_rc); play->failed = 1; }
    }
    if (pending) {
        pipeline_receive(c_load, ack, 2);
        if (ack[0] != play->units - 1 || ack[1]) play->failed = 1;
    }
    unsigned stop[3] = {PIPELINE_STOP, play->failed, 0};
    pipeline_send(c_load, stop, 3);
    pipeline_receive(c_load, play->stats, PIPELINE_LOADER_STATS_WORDS);
    play->elapsed_ticks += (unsigned)(get_reference_time() - previous);
}

void video_loader_report(unsigned number, const video_play_t *play)
{
    const unsigned *stats = play->stats;
    unsigned frames = stats[PIPELINE_FRAMES];
    unsigned long long elapsed_ms = play->elapsed_ticks / 100000;
    unsigned fps = play->elapsed_ticks ? (unsigned)((unsigned long long)frames *
                   PIPELINE_TICKS_PER_SECOND * 100 / play->elapsed_ticks) : 0;
    printf("Play %u complete: %u frames in %llu.%03u seconds: %u.%02u FPS\n",
           number, frames, elapsed_ms / 1000, (unsigned)(elapsed_ms % 1000), fps / 100, fps % 100);
    printf("Startup to first displayed frame: %u us\n",
           (stats[PIPELINE_FIRST_COMPLETE] - play->start) / 100);
    printf("Average stage times (us per frame; overlapping stages are not additive):\n");
    printf("  %-24s %-9s %s\n", "Stage", "Tile", "us/frame");
    printf("  %-24s %-9s %u\n", "Load", "1", (unsigned)(play->load_us / frames));
#if MSC_MPEG_PIPELINE
    printf("  %-24s %-9s %u\n", "Wait for loader", "1", stats[MPEG_INPUT_WAIT_US] / frames);
    printf("  %-24s %-9s %u\n", "Feed", "1", stats[MPEG_FEED_US] / frames);
#endif
    printf("  %-24s %-9s %u\n", "Decode", "1", stats[PIPELINE_DECODE_TICKS] / frames);
    printf("  %-24s %-9s %u\n", "Transmit / ack wait", "1 -> 0", stats[PIPELINE_TRANSFER_TICKS] / frames);
#if MSC_MPEG_PIPELINE
    /* These split the handoff above; do not add them to the total again. */
    printf("  %-24s %-9s %u\n", "  Send YUV / backpressure", "1 -> 0", stats[MPEG_SEND_US] / frames);
    printf("  %-24s %-9s %u\n", "  Wait receiver ack", "1", stats[MPEG_ACK_WAIT_US] / frames);
    printf("  %-24s %-9s %u\n", "Receive YUV / wait", "0", stats[PIPELINE_RECEIVE_TICKS] / frames);
    printf("  %-24s %-9s %u\n", "YUV -> RGB565", "0", stats[PIPELINE_CONVERT_TICKS] / frames);
    printf("  %-24s %-9s %u\n", "Wait previous LCD", "0", stats[PIPELINE_RENDER_WAIT_TICKS] / frames);
#else
    printf("  %-24s %-9s %u\n", "Receive / RGB565 convert", "0", stats[PIPELINE_CONVERT_TICKS] / frames);
#endif
    printf("  %-24s %-9s %u\n", "LCD SPI", "0", stats[PIPELINE_RENDER_TICKS] / frames);
}
#endif

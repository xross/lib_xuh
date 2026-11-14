#include <xs1.h>
#include <xcore/chanend.h>

#include "stdio.h"
#include <string.h>

#include "ff.h"

#include "jpeg_pipeline.h"
#include "mpeg_pipeline.h"
#include "mjpeg_reader.h"
#include "video_loader.h"
#include "activity_led.h"
#include "audio_pipeline.h"
#if APP_XUH_MSC_PLAYER_MPEG_AUDIO
#include "mpeg_ps_reader.h"
#endif

static FATFS Fatfs;
#if MSC_JPEG_PIPELINE
/* FatFs-backed source for the shared loader: one unit is one JPEG frame. */
static mjpeg_reader_t mjpeg_reader;

VIDEO_SOURCE_OPEN static int mjpeg_source_open(void *context)
{
    (void)context;
    memset(&mjpeg_reader, 0, sizeof(mjpeg_reader));
    return f_open(&mjpeg_reader.file, "VIDEO.MJP", FA_READ);
}

VIDEO_SOURCE_READ static int mjpeg_source_read(void *context, unsigned char *destination, unsigned capacity, unsigned *bytes)
{
    (void)context;
    UINT size = 0;
    int result = mjpeg_read_frame(&mjpeg_reader, destination, capacity, &size);
    *bytes = size;
    if (result < 0) printf("VIDEO.MJP: disk rc=%u\n", mjpeg_reader.error);
    return result;
}

VIDEO_SOURCE_CLOSE static int mjpeg_source_close(void *context)
{
    (void)context;
    return f_close(&mjpeg_reader.file);
}

static int play_mjpeg(chanend c_load, unsigned number)
{
    static const video_source_t source = {
        "VIDEO.MJP", NULL, mjpeg_source_open, mjpeg_source_read, mjpeg_source_close,
        jpeg_pipeline_input_buffer, PIPELINE_INPUT_BYTES };
    static video_play_t play;
    printf("Play %u: raw MJPEG at full speed, 320x240 landscape...\n", number);
    video_loader_play(c_load, &source, &play);
    unsigned frames = play.stats[PIPELINE_FRAMES];
    printf("Pipeline drained: loaded=%u displayed=%u sequence/ownership/integrity errors=%u\n",
           play.units, frames, play.stats[PIPELINE_ERRORS]);
    if (play.failed || play.stats[PIPELINE_ERRORS] || frames != play.units || !frames) {
        activity_led_usb(ACTIVITY_LED_ERROR);
        printf("MJPEG playback failed\n");
        return 1;
    }
    video_loader_report(number, &play);
    return 0;
}

static void run_jpeg_pipeline(chanend c_load)
{
    for (unsigned play = 1; ; ++play)
        if (play_mjpeg(c_load, play)) return;
}
#endif

#if MSC_MPEG_PIPELINE
/* FatFs-backed source for the shared loader: one unit is one stream chunk. */
static FIL mpeg_file;
static unsigned long long mpeg_bytes;
#if APP_XUH_MSC_PLAYER_MPEG_AUDIO
static mpeg_ps_reader_t mpeg_ps;
static chanend mpeg_audio_channel;
static unsigned audio_stats[AUDIO_STATS_WORDS];
#ifndef APP_XUH_MSC_PLAYER_MPEG_FILE
#define APP_XUH_MSC_PLAYER_MPEG_FILE "VIDEO_320x180.MPG"
#endif

PS_READ_CALLBACK static int mpeg_ps_disk_read(void *context, unsigned char *data,
                                            unsigned capacity, unsigned *bytes)
{
    UINT count = 0;
    FRESULT rc = f_read((FIL *)context, data, capacity, &count);
    *bytes = count;
    return rc;
}

PS_AUDIO_CALLBACK static int mpeg_ps_audio_send(void *context,
                                               const unsigned char *data, unsigned bytes)
{
    return audio_pipeline_send(*(chanend *)context, data, bytes);
}
#else
#ifndef APP_XUH_MSC_PLAYER_MPEG_FILE
#define APP_XUH_MSC_PLAYER_MPEG_FILE "VIDEO.M1V"
#endif
#endif
#define MPEG_FILE_NAME APP_XUH_MSC_PLAYER_MPEG_FILE

VIDEO_SOURCE_OPEN static int mpeg_source_open(void *context)
{
    (void)context;
    mpeg_bytes = 0;
    int rc = f_open(&mpeg_file, MPEG_FILE_NAME, FA_READ);
#if APP_XUH_MSC_PLAYER_MPEG_AUDIO
    memset(audio_stats, 0, sizeof(audio_stats));
    if (!rc) {
        mpeg_ps_init(&mpeg_ps, mpeg_ps_disk_read, &mpeg_file);
        if (audio_pipeline_start(mpeg_audio_channel, APP_XUH_MSC_PLAYER_AUDIO_DECODE)) {
            audio_pipeline_stop(mpeg_audio_channel, audio_stats);
            f_close(&mpeg_file);
            return -1;
        }
    }
#endif
    return rc;
}

VIDEO_SOURCE_READ static int mpeg_source_read(void *context, unsigned char *destination, unsigned capacity, unsigned *bytes)
{
    (void)context;
#if APP_XUH_MSC_PLAYER_MPEG_AUDIO
    int result = mpeg_ps_read_video(&mpeg_ps, destination, capacity, bytes,
                                    mpeg_ps_audio_send, &mpeg_audio_channel);
    mpeg_bytes += *bytes;
    return result;
#else
    UINT count = 0;
    FRESULT rc = f_read(&mpeg_file, destination, capacity, &count);
    *bytes = count;
    mpeg_bytes += count;
    if (rc) { printf("%s: disk rc=%u\n", MPEG_FILE_NAME, rc); return -1; }
    /* On this regular file, a zero-length read is the end of the stream. */
    return count ? 1 : 0;
#endif
}

VIDEO_SOURCE_CLOSE static int mpeg_source_close(void *context)
{
    (void)context;
    int rc = f_close(&mpeg_file);
#if APP_XUH_MSC_PLAYER_MPEG_AUDIO
    audio_pipeline_stop(mpeg_audio_channel, audio_stats);
    if (audio_stats[AUDIO_ERRORS]) return -1;
#endif
    return rc;
}

static int play_mpeg1(chanend c_load, unsigned number)
{
    static const video_source_t source = {
        MPEG_FILE_NAME, NULL, mpeg_source_open, mpeg_source_read, mpeg_source_close,
        mpeg_pipeline_input_buffer, MPEG_LOAD_BYTES };
    static video_play_t play;
    printf("Play %u: MPEG-1 at full speed (no pacing or dropped frames)...\n", number);
    video_loader_play(c_load, &source, &play);
    const unsigned *stats = play.stats;
    unsigned frames = stats[PIPELINE_FRAMES];
    printf("MPEG pipeline drained: decoded=%u displayed=%u errors=%u status=%d bytes=%llu\n",
           stats[MPEG_DECODED_FRAMES], frames, stats[PIPELINE_ERRORS],
           (int)stats[MPEG_DECODE_STATUS], mpeg_bytes);
    printf("MPEG arena: peak=%u live-after-close=%u bytes\n", stats[MPEG_ARENA_PEAK], stats[MPEG_ARENA_LIVE]);
#if APP_XUH_MSC_PLAYER_MPEG_AUDIO
    printf("MP2 %s: bytes=%u blocks=%u samples/channel=%u rate=%u errors=%u arena=%u checksum=%08x\n",
           APP_XUH_MSC_PLAYER_AUDIO_DECODE ? "decode/discard" : "demux only",
           audio_stats[AUDIO_BYTES], audio_stats[AUDIO_BLOCKS], audio_stats[AUDIO_SAMPLES],
           audio_stats[AUDIO_RATE], audio_stats[AUDIO_ERRORS], audio_stats[AUDIO_PEAK],
           audio_stats[AUDIO_CHECKSUM]);
    printf("MP2 total decode: %u us; average %u us/block\n",
           audio_stats[AUDIO_DECODE_US], audio_stats[AUDIO_BLOCKS] ?
           audio_stats[AUDIO_DECODE_US] / audio_stats[AUDIO_BLOCKS] : 0);
#endif
    if (play.failed || stats[PIPELINE_ERRORS] || frames != stats[MPEG_DECODED_FRAMES] || !frames) {
        activity_led_usb(ACTIVITY_LED_ERROR);
        printf("MPEG-1 playback failed\n");
        return 1;
    }
    video_loader_report(number, &play);
    return 0;
}

static void run_mpeg_pipeline(chanend c_load)
{
    for (unsigned play = 1; ; ++play)
        if (play_mpeg1(c_load, play)) return;
}
#endif


/* Runs once lib_xuh has opened a mass storage device, which is then FatFs
 * drive 0. Mounts the volume and plays from it; returns when playback fails.
 * c_load is the channel to the decoder that the loader feeds. */
void MassStorageApp(chanend c_load
#if APP_XUH_MSC_PLAYER_MPEG_AUDIO
                    , chanend c_audio
#endif
                    )
{
#if APP_XUH_MSC_PLAYER_MPEG_AUDIO
    mpeg_audio_channel = c_audio;
#endif
    FRESULT rc = f_mount(&Fatfs, "", 1);
    if (rc) {
        activity_led_usb(ACTIVITY_LED_ERROR);
        printf("Mount failed, rc=%u\n", rc);
        return;
    }

#if MSC_JPEG_PIPELINE
    run_jpeg_pipeline(c_load);
#elif MSC_MPEG_PIPELINE
    run_mpeg_pipeline(c_load);
#endif

}

#include <xs1.h>
#include <print.h>
#include <xcore/chanend.h>
#include <xcore/hwtimer.h>

#include "stdio.h"
#include <string.h>

#if APP_XUH_PLAYER_ENABLE_JPEG
#include "bmp_writer.h"
#include "jpeg.h"
#endif

#include "ff.h"

#include "timing.h"

#include "jpeg_pipeline.h"
#include "mpeg_pipeline.h"
#include "mjpeg_reader.h"
#include "video_loader.h"
#include "activity_led.h"
#include "audio_pipeline.h"
#if APP_XUH_PLAYER_MPEG_AUDIO
#include "mpeg_ps_reader.h"
#endif

#define LCD_WIDTH             PIPELINE_LCD_WIDTH
#define LCD_HEIGHT            PIPELINE_LCD_HEIGHT

void die(FRESULT rc ) /* Stop with dying message */
{
  activity_led_usb(ACTIVITY_LED_ERROR);
  printf("\nFailed with rc=%u.\n", rc);
  for(;;);
}
void mountdie(FRESULT rc ) /* Stop with dying message */
{
  activity_led_usb(ACTIVITY_LED_ERROR);
  printf("\nmountFailed with rc=%u.\n", rc);
  for(;;);
}
void opendie(FRESULT rc ) /* Stop with dying message */
{
  activity_led_usb(ACTIVITY_LED_ERROR);
  printf("\nopenFailed with rc=%u.\n", rc);
  for(;;);
}

FATFS Fatfs;
FIL   Fil;
//BYTE  fatfsbuffer[1024];
#if !MSC_VIDEO_PIPELINE
BYTE Buff[512*60];      /* File read buffer */
#endif
#if APP_XUH_PLAYER_ENABLE_JPEG && !MSC_JPEG_PIPELINE
static BYTE RgbBuff[JPEG_DECODE_OUTPUT_SIZE(JPEG_OUTPUT_WIDTH, JPEG_OUTPUT_HEIGHT)];
static BYTE DecodeWork[JPEG_DECODE_WORK_SIZE] WORD_ALIGNED;
#endif

#if APP_XUH_PLAYER_ENABLE_JPEG
#if !MSC_JPEG_PIPELINE
static int read_jpeg_from_msc(const char* path, BYTE* dst, UINT dst_size, UINT* bytes_read)
{
    FRESULT rc;

    *bytes_read = 0;

    rc = f_open(&Fil, path, FA_READ);
    if (rc)
    {
        printf("failed rc=%u\n", rc);
        return 1;
    }

    rc = f_read(&Fil, dst, dst_size, bytes_read);

    if (rc) {
        printf("read failed rc=%u\n", rc);
        f_close(&Fil);
        return 1;
    }

    rc = f_close(&Fil);
    if (rc) {
        printf("close failed rc=%u\n", rc);
        return 1;
    }

    if (*bytes_read == 0 || *bytes_read == dst_size) {
        printf("invalid size %u bytes\n", *bytes_read);
        return 1;
    }

    return 0;
}
#endif

#if APP_XUH_PLAYER_ENABLE_LCD && !APP_XUH_PLAYER_LCD_TEST_MODE && !MSC_JPEG_PIPELINE
static unsigned send_rgb888_to_lcd(chanend c_lcd_image, const BYTE *rgb,
                               unsigned width, unsigned height,
                               unsigned channels)
{
    unsigned crop_width = width < LCD_WIDTH ? width : LCD_WIDTH;
    unsigned crop_height = height < LCD_HEIGHT ? height : LCD_HEIGHT;
    unsigned crop_x = (width - crop_width) / 2;
    unsigned crop_y = (height - crop_height) / 2;

    chanend_out_word(c_lcd_image, crop_width);
    chanend_out_word(c_lcd_image, crop_height);

    for (unsigned y = 0; y < crop_height; ++y) {
        const BYTE *row = rgb + ((crop_y + y) * width + crop_x) * channels;

        for (unsigned x = 0; x < crop_width; ++x) {
            chanend_out_byte(c_lcd_image, row[x * channels]);
            chanend_out_byte(c_lcd_image, row[x * channels + 1]);
            chanend_out_byte(c_lcd_image, row[x * channels + 2]);
        }
    }
    /* Release the cross-tile route, then wait for rendering to finish. */
    chanend_out_control_token(c_lcd_image, XS1_CT_END);
    unsigned render_ticks = chanend_in_word(c_lcd_image);
    chanend_check_control_token(c_lcd_image, XS1_CT_END);
    return render_ticks;
}
#endif
#endif

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
#if APP_XUH_PLAYER_MPEG_AUDIO
static mpeg_ps_reader_t mpeg_ps;
static chanend mpeg_audio_channel;
static unsigned audio_stats[AUDIO_STATS_WORDS];
#define MPEG_FILE_NAME "VIDEO.MPG"

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
#define MPEG_FILE_NAME "VIDEO.M1V"
#endif

VIDEO_SOURCE_OPEN static int mpeg_source_open(void *context)
{
    (void)context;
    mpeg_bytes = 0;
    int rc = f_open(&mpeg_file, MPEG_FILE_NAME, FA_READ);
#if APP_XUH_PLAYER_MPEG_AUDIO
    memset(audio_stats, 0, sizeof(audio_stats));
    if (!rc) {
        mpeg_ps_init(&mpeg_ps, mpeg_ps_disk_read, &mpeg_file);
        if (audio_pipeline_start(mpeg_audio_channel, APP_XUH_PLAYER_AUDIO_DECODE)) {
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
#if APP_XUH_PLAYER_MPEG_AUDIO
    int result = mpeg_ps_read_video(&mpeg_ps, destination, capacity, bytes,
                                    mpeg_ps_audio_send, &mpeg_audio_channel);
    mpeg_bytes += *bytes;
    return result;
#else
    UINT count = 0;
    FRESULT rc = f_read(&mpeg_file, destination, capacity, &count);
    *bytes = count;
    mpeg_bytes += count;
    if (rc) { printf("VIDEO.M1V: disk rc=%u\n", rc); return -1; }
    /* On this regular file, a zero-length read is the end of the stream. */
    return count ? 1 : 0;
#endif
}

VIDEO_SOURCE_CLOSE static int mpeg_source_close(void *context)
{
    (void)context;
    int rc = f_close(&mpeg_file);
#if APP_XUH_PLAYER_MPEG_AUDIO
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
    printf("Play %u: MPEG-1 at full speed, 320x240 landscape (no pacing or dropped frames)...\n", number);
    video_loader_play(c_load, &source, &play);
    const unsigned *stats = play.stats;
    unsigned frames = stats[PIPELINE_FRAMES];
    printf("MPEG pipeline drained: decoded=%u displayed=%u errors=%u status=%d bytes=%llu\n",
           stats[MPEG_DECODED_FRAMES], frames, stats[PIPELINE_ERRORS],
           (int)stats[MPEG_DECODE_STATUS], mpeg_bytes);
    printf("MPEG arena: peak=%u live-after-close=%u bytes\n", stats[MPEG_ARENA_PEAK], stats[MPEG_ARENA_LIVE]);
#if APP_XUH_PLAYER_MPEG_AUDIO
    printf("MP2 %s: bytes=%u blocks=%u samples/channel=%u rate=%u errors=%u arena=%u checksum=%08x\n",
           APP_XUH_PLAYER_AUDIO_DECODE ? "decode/discard" : "demux only",
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
 * drive 0. Mounts the volume and plays from it; returns when playback fails. */
void MassStorageApp(
#if APP_XUH_PLAYER_ENABLE_LCD
                    chanend c_lcd_image
#if APP_XUH_PLAYER_MPEG_AUDIO
                    , chanend c_audio
#endif
#elif APP_XUH_PLAYER_MPEG_AUDIO
                    chanend c_audio
#else
                    void
#endif
                    )
{
#if APP_XUH_PLAYER_MPEG_AUDIO
    mpeg_audio_channel = c_audio;
#endif
    FRESULT rc;
    UINT br;

    rc = f_mount(&Fatfs, "", 0);             /* Register volume work area (never fails) for SD host interface #0 */
    if(rc)
        mountdie(rc);

    {
        FATFS *fs;
        DWORD fre_clust, fre_sect, tot_sect;

        /* Get volume information and free clusters of drive 0 */
        rc = f_getfree("0:", &fre_clust, &fs);
        if(rc) die(rc);

        /* Get total sectors and free sectors */
        tot_sect = (fs->n_fatent - 2) * fs->csize;
        fre_sect = fre_clust * fs->csize;

        /* Print free space in unit of KB (assuming 512 bytes/sector) */
        //printf("%lu KB total drive space.\n"
        //        "%lu KB available.\n",
        //        fre_sect / 2, tot_sect / 2);
    }

#if MSC_JPEG_PIPELINE
    run_jpeg_pipeline(c_lcd_image);
#elif MSC_MPEG_PIPELINE
    run_mpeg_pipeline(c_lcd_image);
#elif APP_XUH_PLAYER_ENABLE_JPEG
    const char * const jpeg_paths[JPEG_FILE_COUNT] = JPEG_FILE_PATHS;
    /* The reference timer runs at 100 MHz. Unsigned subtraction handles wrap. */
    const unsigned ticks_per_second = 100000000;
    const unsigned benchmark_ticks = 5 * ticks_per_second;
    unsigned frames = 0;
    unsigned elapsed;
    unsigned load_ticks = 0, decode_ticks = 0, display_ticks = 0, spi_ticks = 0;
    printf("Benchmarking full JPEG read/decode/render for 5 seconds...\n");
    unsigned start = get_time();
    do {
        unsigned image = frames % (sizeof(jpeg_paths) / sizeof(jpeg_paths[0]));
        const char *path = jpeg_paths[image];
        unsigned stage_start = get_time();
        if (read_jpeg_from_msc(path, Buff, sizeof(Buff), &br)) {
            printf("Failed to read %s\n", path);
            return;
        }

        load_ticks += get_time() - stage_start;
        stage_start = get_time();
        jpeg_decode_info_t info = {0};
        memset(RgbBuff, 0, sizeof(RgbBuff));
        memset(DecodeWork, 0, sizeof(DecodeWork));

        jpeg_decode_result_t result = jpeg_decode(
            Buff,
            br,
            RgbBuff,
            sizeof(RgbBuff),
            DecodeWork,
            sizeof(DecodeWork),
            JPEG_DECODE_SCALE,
            &info);

        if (result != JPEG_DECODE_OK) {
            printf("%s: decode failed, result=%d\n", path, result);
            return;
        }

        decode_ticks += get_time() - stage_start;
#if APP_XUH_PLAYER_ENABLE_LCD && !APP_XUH_PLAYER_LCD_TEST_MODE
        stage_start = get_time();
        spi_ticks += send_rgb888_to_lcd(c_lcd_image, RgbBuff, info.output_width,
                                      info.output_height, info.channels);
        display_ticks += get_time() - stage_start;
#endif
        ++frames;
        elapsed = get_time() - start;
    } while (elapsed < benchmark_ticks);

    unsigned elapsed_ms = elapsed / (ticks_per_second / 1000);
    unsigned fps_hundredths = (unsigned)(((unsigned long long)frames *
                                         ticks_per_second * 100) / elapsed);
#if APP_XUH_PLAYER_ENABLE_LCD && !APP_XUH_PLAYER_LCD_TEST_MODE
    printf("Rendered %u frames in %u.%03u seconds: %u.%02u FPS\n",
#else
    printf("Decoded %u frames in %u.%03u seconds (no LCD rendering): %u.%02u FPS\n",
#endif
           frames, elapsed_ms / 1000, elapsed_ms % 1000,
           fps_hundredths / 100, fps_hundredths % 100);
    printf("Average stage times (us): load=%u decode=%u transfer/convert=%u LCD SPI=%u\n",
           load_ticks / frames / 100, decode_ticks / frames / 100,
           (display_ticks - spi_ticks) / frames / 100, spi_ticks / frames / 100);

#else
    printf("Opening TEST.TXT\n");
    rc = f_open(&Fil, "TEST.TXT", FA_READ);
    printf("TEST.TXT f_open: rc=%u\n", rc);
    if (rc) return;

    printf("Reading TEST.TXT\n");
    rc = f_read(&Fil, Buff, sizeof(Buff), &br);
    printf("TEST.TXT f_read: rc=%u bytes=%u\n", rc, br);
    FRESULT read_rc = rc;

    printf("Closing TEST.TXT\n");
    rc = f_close(&Fil);
    printf("TEST.TXT f_close: rc=%u\n", rc);
    if (read_rc || rc) return;

    for (UINT i = 0; i < br; i++)
        printchar(Buff[i]);
    printf("\nTEST.TXT complete\n");
#endif

}

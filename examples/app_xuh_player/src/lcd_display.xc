#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <timer.h>
#include <xs1.h>

#include "lcd_display.h"
#include "jpeg_pipeline.h"
#include "mpeg_pipeline.h"
#include "fps_overlay.h"
#include "audio_meter.h"

#define LCD_WIDTH        PIPELINE_LCD_WIDTH
#define LCD_HEIGHT       PIPELINE_LCD_HEIGHT
#define LCD_SPI_KHZ      50000
#define BOX_SIZE         32
#define BOX_UPDATE_WIDTH (BOX_SIZE + 1)
#define BOX_Y            ((LCD_HEIGHT - BOX_SIZE) / 2)
#define FRAME_DELAY_MS   16

/* Commands shared by the ST7789V and ILI9341. */
#define LCD_SWRESET     0x01
#define LCD_SLPOUT      0x11
#define LCD_INVOFF      0x20
#define LCD_INVON       0x21
#define LCD_DISPON      0x29
#define LCD_CASET       0x2A
#define LCD_RASET       0x2B
#define LCD_RAMWR       0x2C
#define LCD_MADCTL      0x36
#define LCD_COLMOD      0x3A

#define ILI9341_FRMCTR1  0xB1
#define ILI9341_DFUNCTR  0xB6
#define ILI9341_PWCTR1   0xC0
#define ILI9341_PWCTR2   0xC1
#define ILI9341_VMCTR1   0xC5
#define ILI9341_VMCTR2   0xC7

#define ILI9341_MADCTL_LANDSCAPE 0x28
#define ST7789V_MADCTL_LANDSCAPE 0x60

#define RGB565_BLACK     0x0000
#define RGB565_YELLOW    0xFFE0

extern out port p_lcd_dc;
extern out port p_lcd_rst;

static void lcd_set_dc(unsigned data)
{
    p_lcd_dc <: data;
}

static void lcd_write_byte(client spi_master_if spi, unsigned data, unsigned is_data)
{
    lcd_set_dc(is_data);
    spi.transfer8((uint8_t)data);
}

static void lcd_command(client spi_master_if spi, unsigned command)
{
    lcd_write_byte(spi, command, 0);
}

static void lcd_data(client spi_master_if spi, unsigned data)
{
    lcd_write_byte(spi, data, 1);
}

static void lcd_data16(client spi_master_if spi, unsigned data)
{
    lcd_data(spi, data >> 8);
    lcd_data(spi, data);
}

static void lcd_write_command(client spi_master_if spi, unsigned command)
{
    spi.begin_transaction(0, LCD_SPI_KHZ, SPI_MODE_0);
    lcd_command(spi, command);
    spi.end_transaction(0);
}

static void lcd_write_command_data(client spi_master_if spi,
                                   unsigned command, unsigned data)
{
    spi.begin_transaction(0, LCD_SPI_KHZ, SPI_MODE_0);
    lcd_command(spi, command);
    lcd_data(spi, data);
    spi.end_transaction(0);
}

#if APP_XUH_PLAYER_LCD_CONTROLLER == LCD_CONTROLLER_ILI9341
static void lcd_write_command_bytes(client spi_master_if spi,
                                    unsigned command,
                                    const uint8_t data[], unsigned count)
{
    spi.begin_transaction(0, LCD_SPI_KHZ, SPI_MODE_0);
    lcd_command(spi, command);
    for (unsigned i = 0; i < count; ++i)
        lcd_data(spi, data[i]);
    spi.end_transaction(0);
}

static void lcd_init_ili9341(client spi_master_if spi)
{
    const uint8_t vcom[] = {0x3E, 0x28};
    const uint8_t frame_rate[] = {0x00, 0x18};
    const uint8_t display_function[] = {0x08, 0x82, 0x27};

    /* Set panel power, VCOM and scan timing while the display is asleep. */
    lcd_write_command_data(spi, ILI9341_PWCTR1, 0x23);
    lcd_write_command_data(spi, ILI9341_PWCTR2, 0x10);
    lcd_write_command_bytes(spi, ILI9341_VMCTR1, vcom, sizeof(vcom));
    lcd_write_command_data(spi, ILI9341_VMCTR2, 0x86);
    lcd_write_command_bytes(spi, ILI9341_FRMCTR1, frame_rate, sizeof(frame_rate));
    lcd_write_command_bytes(spi, ILI9341_DFUNCTR, display_function,
                            sizeof(display_function));
    lcd_write_command_data(spi, LCD_COLMOD, 0x55); /* 16-bit RGB565. */
    /* MV | BGR: 320 x 240 landscape, matching the panel's colour order. */
    lcd_write_command_data(spi, LCD_MADCTL, ILI9341_MADCTL_LANDSCAPE);
    lcd_write_command(spi, LCD_INVOFF);

    lcd_write_command(spi, LCD_SLPOUT);
    delay_milliseconds(120);
}
#elif APP_XUH_PLAYER_LCD_CONTROLLER == LCD_CONTROLLER_ST7789V
static void lcd_init_st7789v(client spi_master_if spi)
{
    /* Preserve the original ST7789V command order and settings. */
    lcd_write_command(spi, LCD_SLPOUT);
    delay_milliseconds(120);

    lcd_write_command_data(spi, LCD_COLMOD, 0x55); /* 16-bit RGB565. */
    lcd_write_command_data(spi, LCD_MADCTL, ST7789V_MADCTL_LANDSCAPE);
    lcd_write_command(spi, LCD_INVON);
}
#endif

static void lcd_reset(void)
{
    p_lcd_dc <: 0;
    p_lcd_rst <: 0;
    delay_milliseconds(20);

    p_lcd_rst <: 1;
    delay_milliseconds(150);
}

static void lcd_init(client spi_master_if spi)
{
    lcd_reset();

    lcd_write_command(spi, LCD_SWRESET);
    delay_milliseconds(150);
#if APP_XUH_PLAYER_LCD_CONTROLLER == LCD_CONTROLLER_ILI9341
    lcd_init_ili9341(spi);
#elif APP_XUH_PLAYER_LCD_CONTROLLER == LCD_CONTROLLER_ST7789V
    lcd_init_st7789v(spi);
#endif
    lcd_write_command(spi, LCD_DISPON);
    delay_milliseconds(20);
}

static void lcd_set_address_window(client spi_master_if spi,
                                   unsigned x0, unsigned y0,
                                   unsigned x1, unsigned y1)
{
    lcd_command(spi, LCD_CASET);
    lcd_data16(spi, x0);
    lcd_data16(spi, x1);
    lcd_command(spi, LCD_RASET);
    lcd_data16(spi, y0);
    lcd_data16(spi, y1);
    lcd_command(spi, LCD_RAMWR);
}

static void lcd_begin_window(client spi_master_if spi,
                             unsigned x0, unsigned y0,
                             unsigned x1, unsigned y1)
{
    spi.begin_transaction(0, LCD_SPI_KHZ, SPI_MODE_0);
    lcd_set_address_window(spi, x0, y0, x1, y1);
    lcd_set_dc(1);
}

static void lcd_fill_screen(client spi_master_if spi, uint16_t color)
{
    uint8_t line[LCD_WIDTH * 2];

    for (unsigned x = 0; x < LCD_WIDTH; ++x) {
        line[2 * x] = color >> 8;
        line[2 * x + 1] = color;
    }

    lcd_begin_window(spi, 0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
    for (unsigned y = 0; y < LCD_HEIGHT; ++y)
        spi.transfer_array(line, null, sizeof(line));
    spi.end_transaction(0);
}

#if APP_XUH_PLAYER_LCD_TEST_MODE
static void lcd_fill_box(client spi_master_if spi, unsigned x, uint16_t color)
{
    uint8_t line[BOX_SIZE * 2];

    for (unsigned i = 0; i < BOX_SIZE; ++i) {
        line[2 * i] = color >> 8;
        line[2 * i + 1] = color;
    }

    lcd_begin_window(spi, x, BOX_Y, x + BOX_SIZE - 1, BOX_Y + BOX_SIZE - 1);
    for (unsigned y = 0; y < BOX_SIZE; ++y)
        spi.transfer_array(line, null, sizeof(line));
    spi.end_transaction(0);
}

static void lcd_move_box(client spi_master_if spi, unsigned old_x, unsigned new_x)
{
    unsigned x0 = old_x < new_x ? old_x : new_x;
    unsigned x1 = (old_x > new_x ? old_x : new_x) + BOX_SIZE - 1;
    uint8_t line[BOX_UPDATE_WIDTH * 2];

    for (unsigned x = 0; x < BOX_UPDATE_WIDTH; ++x) {
        uint16_t color = x0 + x >= new_x && x0 + x < new_x + BOX_SIZE
                         ? RGB565_YELLOW : RGB565_BLACK;
        line[2 * x] = color >> 8;
        line[2 * x + 1] = color;
    }

    lcd_begin_window(spi, x0, BOX_Y, x1, BOX_Y + BOX_SIZE - 1);
    for (unsigned y = 0; y < BOX_SIZE; ++y)
        spi.transfer_array(line, null, sizeof(line));
    spi.end_transaction(0);
}
#endif

#if MSC_VIDEO_PIPELINE
/* lib_spi's synchronous array length is a compile-time parameter. Preserve
 * the single-call full-width path; other MPEG widths are multiples of eight. */
static void lcd_write_video_row(client spi_master_if spi,
                                const uint8_t data[], unsigned bytes)
{
    if (bytes == LCD_WIDTH * 2) {
        spi.transfer_array(data, null, LCD_WIDTH * 2);
        return;
    }
    unsigned offset = 0;
    for (; offset + 64 <= bytes; offset += 64)
        spi.transfer_array(&data[offset], null, 64);
    for (; offset < bytes; offset += 8)
        spi.transfer_array(&data[offset], null, 8);
}

static void lcd_render_pipeline(client spi_master_if spi, chanend c_render)
{
    unsigned expected = 0;
#if MSC_MPEG_PIPELINE
    unsigned previous_width = LCD_WIDTH, previous_height = LCD_HEIGHT;
#endif
    timer profile_timer;
    while (1) {
        unsigned slot = inuint(c_render);
        unsigned sequence = inuint(c_render);
        chkct(c_render, XS1_CT_END);
        /* End of a play: the receiver has already drained every frame. */
        if (slot == PIPELINE_STOP) {
            expected = 0;
            continue;
        }
        /* Slot and sequence are checked before accessing shared tile-0 RAM. */
        if (sequence != expected || slot != (sequence & 1)) __builtin_trap();
        ++expected;
        unsigned start, end;
#if MSC_MPEG_PIPELINE
        unsigned errors = mpeg_pipeline_check_lcd(slot);
#else
        unsigned errors = jpeg_pipeline_check_lcd(slot);
#endif
        profile_timer :> start;
        unsafe {
#if MSC_MPEG_PIPELINE
            unsigned char * unsafe buffer = mpeg_pipeline_lcd_buffer(slot);
            video_window_t window = mpeg_pipeline_lcd_window(slot);
            if (window.width != previous_width || window.height != previous_height) {
                lcd_fill_screen(spi, RGB565_BLACK);
                previous_width = window.width;
                previous_height = window.height;
            }
#else
            unsigned char * unsafe buffer = jpeg_pipeline_lcd_buffer(slot);
            video_window_t window = {0, 0, LCD_WIDTH, LCD_HEIGHT};
#endif
            lcd_begin_window(spi, window.x, window.y,
                             window.x + window.width - 1, window.y + window.height - 1);
            for (unsigned y = 0; y < window.height; ++y)
                lcd_write_video_row(spi,
                    (const uint8_t *)(buffer + video_window_rgb_offset(window, y)), window.width * 2);
            spi.end_transaction(0);
#if MSC_MPEG_PIPELINE && APP_XUH_PLAYER_FPS_OVERLAY
            /* The gauge is drawn in screen coordinates. If any of it lies
             * outside the video, refresh its fixed rectangle separately. */
            if (window.x > FPS_OVERLAY_X || window.y > FPS_OVERLAY_Y ||
                window.x + window.width < FPS_OVERLAY_X + FPS_OVERLAY_WIDTH ||
                window.y + window.height < FPS_OVERLAY_Y + FPS_OVERLAY_HEIGHT) {
                lcd_begin_window(spi, FPS_OVERLAY_X, FPS_OVERLAY_Y,
                                 FPS_OVERLAY_X + FPS_OVERLAY_WIDTH - 1,
                                 FPS_OVERLAY_Y + FPS_OVERLAY_HEIGHT - 1);
                for (unsigned y = 0; y < FPS_OVERLAY_HEIGHT; ++y)
                    spi.transfer_array((const uint8_t *)(buffer +
                        ((FPS_OVERLAY_Y + y) * LCD_WIDTH + FPS_OVERLAY_X) * 2),
                        null, FPS_OVERLAY_WIDTH * 2);
                spi.end_transaction(0);
            }
#endif
#if AUDIO_METER_ENABLED
            /* As for the FPS gauge: the meter's rectangle is fixed on screen. */
            if (window.x > AUDIO_METER_X || window.y > AUDIO_METER_Y ||
                window.x + window.width < AUDIO_METER_X + AUDIO_METER_WIDTH ||
                window.y + window.height < AUDIO_METER_Y + AUDIO_METER_HEIGHT) {
                lcd_begin_window(spi, AUDIO_METER_X, AUDIO_METER_Y,
                                 AUDIO_METER_X + AUDIO_METER_WIDTH - 1,
                                 AUDIO_METER_Y + AUDIO_METER_HEIGHT - 1);
                for (unsigned y = 0; y < AUDIO_METER_HEIGHT; ++y)
                    spi.transfer_array((const uint8_t *)(buffer +
                        ((AUDIO_METER_Y + y) * LCD_WIDTH + AUDIO_METER_X) * 2),
                        null, AUDIO_METER_WIDTH * 2);
                spi.end_transaction(0);
            }
#endif
        }
        profile_timer :> end;
#if MSC_MPEG_PIPELINE
        errors += mpeg_pipeline_check_lcd(slot);
#else
        errors += jpeg_pipeline_check_lcd(slot);
#endif
        outuint(c_render, sequence);
        outuint(c_render, end - start);
        outuint(c_render, errors);
        outct(c_render, XS1_CT_END);
    }
}
#endif

void lcd_display_task(client spi_master_if spi, chanend c_image)
{
#if APP_XUH_PLAYER_LCD_TEST_MODE
    unsigned x = 0;
    int direction = 1;
#endif

    /* Report the build-selected controller once, outside the rendering loop. */
    printf("LCD: %s, %dx%d landscape, SPI %d kHz\n",
           LCD_CONTROLLER_NAME, LCD_WIDTH, LCD_HEIGHT, LCD_SPI_KHZ);
    lcd_init(spi);
    lcd_fill_screen(spi, RGB565_BLACK);

#if APP_XUH_PLAYER_LCD_TEST_MODE
    lcd_fill_box(spi, x, RGB565_YELLOW);

    while (1) {
        unsigned old_x = x;

        if (direction > 0) {
            ++x;
            if (x == LCD_WIDTH - BOX_SIZE)
                direction = -1;
        } else {
            --x;
            if (x == 0)
                direction = 1;
        }

        lcd_move_box(spi, old_x, x);
        delay_milliseconds(FRAME_DELAY_MS);
    }
#elif MSC_VIDEO_PIPELINE
    lcd_render_pipeline(spi, c_image);
#endif
}

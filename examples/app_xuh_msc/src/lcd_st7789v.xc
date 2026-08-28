#if APP_XUH_MSC_ENABLE_LCD

#include <stdint.h>
#include <string.h>
#include <timer.h>

#include "lcd_st7789v.h"

#define LCD_WIDTH        240
#define LCD_HEIGHT       320
#define LCD_SPI_KHZ      30000
#define BOX_SIZE         32
#define BOX_UPDATE_WIDTH (BOX_SIZE + 1)
#define BOX_Y            ((LCD_HEIGHT - BOX_SIZE) / 2)
#define FRAME_DELAY_MS   16

#define ST7789_SWRESET   0x01
#define ST7789_SLPOUT    0x11
#define ST7789_INVON     0x21
#define ST7789_DISPON    0x29
#define ST7789_CASET     0x2A
#define ST7789_RASET     0x2B
#define ST7789_RAMWR     0x2C
#define ST7789_MADCTL    0x36
#define ST7789_COLMOD    0x3A

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

    lcd_write_command(spi, ST7789_SWRESET);
    delay_milliseconds(150);
    lcd_write_command(spi, ST7789_SLPOUT);
    delay_milliseconds(120);

    lcd_write_command_data(spi, ST7789_COLMOD, 0x55); /* 16-bit RGB565. */
    lcd_write_command_data(spi, ST7789_MADCTL, 0x00);
    lcd_write_command(spi, ST7789_INVON);
    lcd_write_command(spi, ST7789_DISPON);
    delay_milliseconds(20);
}

static void lcd_set_address_window(client spi_master_if spi,
                                   unsigned x0, unsigned y0,
                                   unsigned x1, unsigned y1)
{
    lcd_command(spi, ST7789_CASET);
    lcd_data16(spi, x0);
    lcd_data16(spi, x1);
    lcd_command(spi, ST7789_RASET);
    lcd_data16(spi, y0);
    lcd_data16(spi, y1);
    lcd_command(spi, ST7789_RAMWR);
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

#if APP_XUH_MSC_LCD_TEST_MODE
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

#if !APP_XUH_MSC_LCD_TEST_MODE
static uint8_t lcd_framebuffer[LCD_HEIGHT][LCD_WIDTH * 2];

static uint16_t rgb888_to_rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return ((uint16_t)(red & 0xF8) << 8) |
           ((uint16_t)(green & 0xFC) << 3) |
           (blue >> 3);
}

static void lcd_render_image(client spi_master_if spi, chanend c_image)
{
    unsigned image_width = inuint(c_image);
    unsigned image_height = inuint(c_image);
    unsigned x_offset = (LCD_WIDTH - image_width) / 2;
    unsigned y_offset = (LCD_HEIGHT - image_height) / 2;

    memset(lcd_framebuffer, 0, sizeof(lcd_framebuffer));

    for (unsigned y = 0; y < image_height; ++y) {
        for (unsigned x = 0; x < image_width; ++x) {
            uint8_t red = inuchar(c_image);
            uint8_t green = inuchar(c_image);
            uint8_t blue = inuchar(c_image);
            uint16_t pixel = rgb888_to_rgb565(red, green, blue);
            unsigned output_x = x_offset + x;
            unsigned output_y = y_offset + y;

            lcd_framebuffer[output_y][2 * output_x] = pixel >> 8;
            lcd_framebuffer[output_y][2 * output_x + 1] = pixel;
        }
    }

    lcd_begin_window(spi, 0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
    for (unsigned y = 0; y < LCD_HEIGHT; ++y)
        spi.transfer_array(lcd_framebuffer[y], null, LCD_WIDTH * 2);

    spi.end_transaction(0);
}
#endif

void lcd_st7789v_task(client spi_master_if spi, chanend c_image)
{
#if APP_XUH_MSC_LCD_TEST_MODE
    unsigned x = 0;
    int direction = 1;
#endif

    lcd_init(spi);
    lcd_fill_screen(spi, RGB565_BLACK);

#if APP_XUH_MSC_LCD_TEST_MODE
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
#else
    while (1)
        lcd_render_image(spi, c_image);
#endif
}

#endif

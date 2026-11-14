#ifndef LCD_DISPLAY_H_
#define LCD_DISPLAY_H_

#define LCD_CONTROLLER_ILI9341 1
#define LCD_CONTROLLER_ST7789V 2

#ifndef APP_XUH_PLAYER_LCD_CONTROLLER
#error "APP_XUH_PLAYER_LCD_CONTROLLER must select an LCD controller"
#elif APP_XUH_PLAYER_LCD_CONTROLLER == LCD_CONTROLLER_ILI9341
#define LCD_CONTROLLER_NAME "ILI9341"
#elif APP_XUH_PLAYER_LCD_CONTROLLER == LCD_CONTROLLER_ST7789V
#define LCD_CONTROLLER_NAME "ST7789V"
#else
#error "Unsupported APP_XUH_PLAYER_LCD_CONTROLLER"
#endif

#include <spi.h>

void lcd_display_task(client spi_master_if spi, chanend c_image);

#endif

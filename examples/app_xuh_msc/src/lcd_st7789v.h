#ifndef LCD_ST7789V_H_
#define LCD_ST7789V_H_

#if APP_XUH_MSC_ENABLE_LCD

#include <spi.h>

void lcd_st7789v_task(client spi_master_if spi, chanend c_image);

#endif

#endif

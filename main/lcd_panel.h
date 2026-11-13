#pragma once

#include "esp_lcd_panel_ops.h"

#define LCD_H_RES 480
#define LCD_V_RES 480

/**
 * Initialise the 2.1" round ST7701S panel on the Waveshare ESP32-S3-LCD-Driver-Board:
 * sends the ST7701S init sequence over 3-wire SPI, then creates the 16-bit RGB panel
 * with `num_fbs` PSRAM frame buffers and turns the backlight on.
 */
esp_lcd_panel_handle_t lcd_panel_init(int num_fbs);

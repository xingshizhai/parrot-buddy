#pragma once

#define BOARD_NAME          "ESP32-S3-BOX-3"
#define BOARD_LCD_WIDTH     320
#define BOARD_LCD_HEIGHT    240
#define BOARD_BTN_COUNT     3

esp_err_t board_init_all(void);

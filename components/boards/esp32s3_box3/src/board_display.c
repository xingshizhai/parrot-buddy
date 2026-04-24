#include "esp_log.h"
#include "esp_check.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_lvgl_port.h"
#include "esp_lvgl_port_disp.h"
#include "buddy_hal/hal_display.h"
#include "buddy_hal/hal.h"

#define TAG "DISP"

static hal_display_t s_display;
static lv_display_t *s_lv_disp = NULL;

/* Called by board_touch.c after touch is ready */
lv_display_t *board_display_get_lv_disp(void)
{
    return s_lv_disp;
}

static esp_err_t display_backlight_set(hal_display_t *disp, uint8_t percent)
{
    bsp_display_brightness_set(percent);
    return ESP_OK;
}

esp_err_t hal_display_create(const hal_display_cfg_t *cfg, hal_display_t **out)
{
    /* Init LVGL port (task + timer) */
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "lvgl_port_init failed");

    /* Brightness PWM (must be called before bsp_display_backlight_on) */
    ESP_RETURN_ON_ERROR(bsp_display_brightness_init(), TAG, "brightness_init failed");

    /* Create LCD panel + IO handles */
    esp_lcd_panel_handle_t    panel_handle = NULL;
    esp_lcd_panel_io_handle_t io_handle    = NULL;
    bsp_display_config_t bsp_disp_cfg = {
        .max_transfer_sz = (BSP_LCD_H_RES * CONFIG_BSP_LCD_DRAW_BUF_HEIGHT) * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(bsp_display_new(&bsp_disp_cfg, &panel_handle, &io_handle),
                        TAG, "bsp_display_new failed");

    esp_lcd_panel_disp_on_off(panel_handle, true);

    /* Register display with LVGL port (no touch here — board_touch.c does it) */
    uint32_t buf_px = cfg ? cfg->buf_size_px : (BSP_LCD_H_RES * CONFIG_BSP_LCD_DRAW_BUF_HEIGHT);
    bool double_buf = cfg ? cfg->double_buffered : false;

    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle     = io_handle,
        .panel_handle  = panel_handle,
        .buffer_size   = buf_px,
        .double_buffer = double_buf,
        .hres          = BSP_LCD_H_RES,
        .vres          = BSP_LCD_V_RES,
        .monochrome    = false,
        .rotation = {
            .swap_xy  = false,
            .mirror_x = true,
            .mirror_y = true,
        },
        .flags = {
            .buff_dma    = true,
            .buff_spiram = false,
            .swap_bytes  = (BSP_LCD_BIGENDIAN ? true : false),
        },
    };
    s_lv_disp = lvgl_port_add_disp(&disp_cfg);
    if (!s_lv_disp) {
        ESP_LOGE(TAG, "lvgl_port_add_disp failed");
        return ESP_FAIL;
    }

    bsp_display_backlight_on();

    s_display.backlight_set = display_backlight_set;
    *out = &s_display;
    ESP_LOGI(TAG, "display ready %dx%d", BSP_LCD_H_RES, BSP_LCD_V_RES);
    return ESP_OK;
}

void hal_display_destroy(hal_display_t *disp) { }

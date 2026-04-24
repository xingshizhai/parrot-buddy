#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

typedef struct hal_display_s hal_display_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t  rotation;
    bool     double_buffered;
    size_t   buf_size_px;
} hal_display_cfg_t;

struct hal_display_s {
    esp_err_t (*init)(hal_display_t *disp, const hal_display_cfg_t *cfg);
    esp_err_t (*deinit)(hal_display_t *disp);
    esp_err_t (*backlight_set)(hal_display_t *disp, uint8_t percent);
    void      *priv;
};

esp_err_t hal_display_create(const hal_display_cfg_t *cfg, hal_display_t **out);
void      hal_display_destroy(hal_display_t *disp);

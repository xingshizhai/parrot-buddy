#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef struct hal_touch_s hal_touch_t;

typedef struct {
    uint16_t x;
    uint16_t y;
    bool     pressed;
} hal_touch_point_t;

typedef void (*hal_touch_cb_t)(hal_touch_point_t point, void *ctx);

struct hal_touch_s {
    esp_err_t (*init)(hal_touch_t *touch);
    esp_err_t (*deinit)(hal_touch_t *touch);
    esp_err_t (*read)(hal_touch_t *touch, hal_touch_point_t *out);
    esp_err_t (*register_cb)(hal_touch_t *touch, hal_touch_cb_t cb, void *ctx);
    void      *priv;
};

esp_err_t hal_touch_create(hal_touch_t **out);
void      hal_touch_destroy(hal_touch_t *touch);

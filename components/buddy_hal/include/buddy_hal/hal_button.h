#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef struct hal_buttons_s hal_buttons_t;

typedef enum {
    HAL_BTN_BOOT  = 0,
    HAL_BTN_LEFT  = 1,
    HAL_BTN_RIGHT = 2,
    HAL_BTN_MAX,
} hal_button_id_t;

typedef enum {
    HAL_BTN_EVT_PRESS_DOWN = 0,
    HAL_BTN_EVT_PRESS_UP,
    HAL_BTN_EVT_LONG_PRESS,
    HAL_BTN_EVT_DOUBLE_CLICK,
} hal_button_event_t;

typedef void (*hal_button_cb_t)(hal_button_id_t id, hal_button_event_t event, void *ctx);

struct hal_buttons_s {
    esp_err_t (*init)(hal_buttons_t *btns);
    esp_err_t (*deinit)(hal_buttons_t *btns);
    esp_err_t (*register_cb)(hal_buttons_t *btns,
                              hal_button_id_t id,
                              hal_button_event_t event,
                              hal_button_cb_t cb,
                              void *ctx);
    bool      (*is_pressed)(hal_buttons_t *btns, hal_button_id_t id);
    void      *priv;
};

esp_err_t hal_buttons_create(hal_buttons_t **out);
void      hal_buttons_destroy(hal_buttons_t *btns);

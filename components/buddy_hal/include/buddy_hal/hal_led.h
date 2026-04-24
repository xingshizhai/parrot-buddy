#pragma once
#include <stdint.h>
#include "esp_err.h"

typedef struct hal_led_s hal_led_t;

struct hal_led_s {
    esp_err_t (*init)(hal_led_t *led);
    esp_err_t (*deinit)(hal_led_t *led);
    esp_err_t (*set_rgb)(hal_led_t *led, uint8_t r, uint8_t g, uint8_t b);
    esp_err_t (*set_brightness)(hal_led_t *led, uint8_t pct);
    esp_err_t (*blink)(hal_led_t *led, uint32_t on_ms, uint32_t off_ms, int count);  /* count=-1: forever */
    esp_err_t (*off)(hal_led_t *led);
    void      *priv;
};

esp_err_t hal_led_create(hal_led_t **out);
void      hal_led_destroy(hal_led_t *led);

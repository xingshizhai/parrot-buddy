#include "esp_log.h"
#include "buddy_hal/hal_led.h"
#include "buddy_hal/hal.h"

/* ESP32-S3-BOX-3 does not have an onboard RGB LED.
   This is a no-op stub. For boards with LED, implement via led_strip component. */

#define TAG "LED"

static hal_led_t s_led;

static esp_err_t led_init(hal_led_t *l)      { return ESP_OK; }
static esp_err_t led_deinit(hal_led_t *l)    { return ESP_OK; }
static esp_err_t led_set_rgb(hal_led_t *l, uint8_t r, uint8_t g, uint8_t b) { return ESP_OK; }
static esp_err_t led_set_brightness(hal_led_t *l, uint8_t p) { return ESP_OK; }
static esp_err_t led_blink(hal_led_t *l, uint32_t on, uint32_t off, int count) { return ESP_OK; }
static esp_err_t led_off(hal_led_t *l)       { return ESP_OK; }

esp_err_t hal_led_create(hal_led_t **out)
{
    s_led.init           = led_init;
    s_led.deinit         = led_deinit;
    s_led.set_rgb        = led_set_rgb;
    s_led.set_brightness = led_set_brightness;
    s_led.blink          = led_blink;
    s_led.off            = led_off;
    *out = &s_led;
    ESP_LOGI(TAG, "LED stub ready");
    return ESP_OK;
}

void hal_led_destroy(hal_led_t *led) { }

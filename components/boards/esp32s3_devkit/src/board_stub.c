#include "esp_log.h"
#include "buddy_hal/hal.h"

#define TAG "DEVKIT"

/* Minimal no-op HAL implementations for generic ESP32-S3 boards.
   Replace each function with real driver calls when porting. */

static hal_display_t  s_display  = {0};
static hal_touch_t    s_touch    = {0};
static hal_audio_t    s_audio    = {0};
static hal_buttons_t  s_buttons  = {0};
static hal_imu_t      s_imu      = {0};
static hal_led_t      s_led      = {0};

esp_err_t hal_display_create(const hal_display_cfg_t *cfg, hal_display_t **out)
{
    ESP_LOGW(TAG, "display: stub — replace with real driver");
    *out = &s_display;
    return ESP_OK;
}
void hal_display_destroy(hal_display_t *d) {}

esp_err_t hal_touch_create(hal_touch_t **out)
{
    ESP_LOGW(TAG, "touch: stub");
    *out = &s_touch;
    return ESP_OK;
}
void hal_touch_destroy(hal_touch_t *t) {}

esp_err_t hal_audio_create(const hal_audio_cfg_t *cfg, hal_audio_t **out)
{
    ESP_LOGW(TAG, "audio: stub");
    *out = &s_audio;
    return ESP_OK;
}
void hal_audio_destroy(hal_audio_t *a) {}

esp_err_t hal_buttons_create(hal_buttons_t **out)
{
    ESP_LOGW(TAG, "buttons: stub");
    *out = &s_buttons;
    return ESP_OK;
}
void hal_buttons_destroy(hal_buttons_t *b) {}

esp_err_t hal_imu_create(hal_imu_t **out)
{
    ESP_LOGW(TAG, "IMU: stub");
    *out = &s_imu;
    return ESP_OK;
}
void hal_imu_destroy(hal_imu_t *i) {}

esp_err_t hal_led_create(hal_led_t **out)
{
    ESP_LOGW(TAG, "LED: stub");
    *out = &s_led;
    return ESP_OK;
}
void hal_led_destroy(hal_led_t *l) {}

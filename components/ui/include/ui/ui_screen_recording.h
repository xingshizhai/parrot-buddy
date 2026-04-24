#pragma once
#include "esp_err.h"
#include "lvgl.h"
#include "ui/ui_manager.h"
#include <stdbool.h>

typedef void (*recording_state_cb_t)(bool is_recording, uint32_t duration_ms);
typedef void (*recording_level_cb_t)(float level_db);

esp_err_t ui_screen_recording_create(lv_obj_t *parent);
esp_err_t ui_screen_recording_show(void);
esp_err_t ui_screen_recording_hide(void);
esp_err_t ui_screen_recording_set_callbacks(recording_state_cb_t state_cb, recording_level_cb_t level_cb);
esp_err_t ui_screen_recording_update_level(float level_db);
esp_err_t ui_screen_recording_update_duration(uint32_t duration_ms);
esp_err_t ui_screen_recording_update_state(bool is_recording, bool is_streaming);
esp_err_t ui_screen_recording_set_connected(bool connected);

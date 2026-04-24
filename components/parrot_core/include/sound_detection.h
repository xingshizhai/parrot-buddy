#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float rms_threshold_db;      // -40 dBFS default
    float frequency_min_hz;      // 1000 Hz
    float frequency_max_hz;      // 8000 Hz
    uint32_t sample_rate;        // 16000 Hz default
    uint32_t hold_time_ms;       // 100 ms minimum
    uint32_t cooldown_ms;        // 500 ms between detections
} detection_config_t;

typedef void (*detection_callback_t)(float rms_db, float peak_freq, uint32_t timestamp_ms);

esp_err_t sound_detection_init(const detection_config_t *config);
esp_err_t sound_detection_start(detection_callback_t callback);
esp_err_t sound_detection_stop(void);
esp_err_t sound_detection_process(const int16_t *samples, size_t count, uint32_t timestamp_ms);
bool sound_detection_is_active(void);

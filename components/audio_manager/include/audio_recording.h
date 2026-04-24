#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

typedef void (*audio_recording_chunk_cb_t)(const int16_t *samples, size_t count, uint32_t timestamp_ms);
typedef void (*recording_state_cb_t)(bool is_recording, uint32_t duration_ms);

typedef struct {
    uint32_t sample_rate;
    uint32_t silence_timeout_ms;
    uint32_t min_duration_ms;
    float detection_threshold_db;
    audio_recording_chunk_cb_t chunk_callback;
    recording_state_cb_t state_callback;
} recording_config_t;

esp_err_t audio_recording_init(const recording_config_t *config);
esp_err_t audio_recording_start(void);
esp_err_t audio_recording_stop(void);
esp_err_t audio_recording_pause(void);
esp_err_t audio_recording_resume(void);
bool audio_recording_is_active(void);
uint32_t audio_recording_get_duration_ms(void);

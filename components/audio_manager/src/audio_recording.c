#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "audio_recording.h"
#include "audio_manager.h"

static inline uint32_t time_diff_ms(uint32_t newer, uint32_t older) {
    if (newer >= older) {
        return newer - older;
    } else {
        return (UINT32_MAX - older) + newer + 1;
    }
}

#define TAG "AUDIO_REC"
#define BUFFER_SIZE_MS 1000
#define SILENCE_CHECK_INTERVAL_MS 100

static recording_config_t config = {0};
static volatile TaskHandle_t recording_task = NULL;
static volatile bool is_recording = false;
static volatile bool is_paused = false;
static volatile bool exit_requested = false;
static volatile bool task_exited = false;
static uint32_t recording_start_ms = 0;
static uint32_t last_loud_ms = 0;
static uint32_t total_samples = 0;

static float calculate_rms(const int16_t *samples, size_t count) {
    int64_t sum = 0;
    for (size_t i = 0; i < count; i++) {
        int32_t sample = samples[i];
        sum += (int64_t)sample * sample;
    }
    return sqrtf((float)sum / count);
}

static float rms_to_db(float rms) {
    if (rms <= 0) return -100.0f;
    return 20.0f * log10f(rms / 32768.0f);
}

static void recording_task_func(void *arg) {
    size_t buffer_size_samples = BUFFER_SIZE_MS * config.sample_rate / 1000;
    int16_t *buffer = heap_caps_malloc(buffer_size_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate recording buffer");
        vTaskDelete(NULL);
        return;
    }

    uint32_t buffer_pos = 0;

    while (!exit_requested) {
        size_t samples_read = 0;
        size_t max_bytes = (buffer_size_samples - buffer_pos) * sizeof(int16_t);

        if (max_bytes == 0) {
            buffer_pos = 0;
            max_bytes = buffer_size_samples * sizeof(int16_t);
        }

        esp_err_t ret = audio_manager_read(buffer + buffer_pos, max_bytes, &samples_read);

        if (ret != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        buffer_pos += samples_read / sizeof(int16_t);

        if (buffer_pos >= config.sample_rate / 10) {
            float rms = calculate_rms(buffer, buffer_pos);
            float db = rms_to_db(rms);
            uint32_t now = pdTICKS_TO_MS(xTaskGetTickCount());

            if (db > config.detection_threshold_db) {
                last_loud_ms = now;

                if (!is_recording) {
                    is_recording = true;
                    recording_start_ms = now;
                    total_samples = 0;
                    ESP_LOGI(TAG, "Recording started");

                    if (config.state_callback) {
                        config.state_callback(true, 0);
                    }
                }
            }

            if (is_recording && !is_paused) {
                if (config.chunk_callback) {
                    config.chunk_callback(buffer, buffer_pos, now);
                }

                total_samples += buffer_pos;

                if (config.silence_timeout_ms > 0 &&
                    time_diff_ms(now, last_loud_ms) > config.silence_timeout_ms) {

                    uint32_t duration = time_diff_ms(now, recording_start_ms);
                    if (duration >= config.min_duration_ms) {
                        is_recording = false;
                        ESP_LOGI(TAG, "Recording stopped (silence), duration: %ums", duration);

                        if (config.state_callback) {
                            config.state_callback(false, duration);
                        }
                    }
                }
            }

            buffer_pos = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    free(buffer);
    ESP_LOGI(TAG, "Recording task exiting");
    task_exited = true;
    vTaskDelete(NULL);
}

esp_err_t audio_recording_init(const recording_config_t *cfg) {
    if (!cfg || cfg->sample_rate == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    config = *cfg;
    return ESP_OK;
}

esp_err_t audio_recording_start(void) {
    if (recording_task) {
        return ESP_OK;
    }

    TaskHandle_t task = NULL;
    BaseType_t result = xTaskCreate(
        recording_task_func,
        "audio_rec",
        8192,
        NULL,
        7,
        &task
    );
    if (result == pdPASS) {
        recording_task = task;
    }

    if (result != pdPASS) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Audio recording task started");
    return ESP_OK;
}

esp_err_t audio_recording_stop(void) {
    if (recording_task) {
        exit_requested = true;
        int retries = 50;
        while (!task_exited && retries > 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            retries--;
        }
        if (!task_exited) {
            ESP_LOGW(TAG, "Recording task did not exit in time");
        }
        recording_task = NULL;
        task_exited = false;
    }

    is_recording = false;
    is_paused = false;
    exit_requested = false;

    ESP_LOGI(TAG, "Audio recording stopped");
    return ESP_OK;
}

esp_err_t audio_recording_pause(void) {
    is_paused = true;
    return ESP_OK;
}

esp_err_t audio_recording_resume(void) {
    is_paused = false;
    last_loud_ms = pdTICKS_TO_MS(xTaskGetTickCount());
    return ESP_OK;
}

bool audio_recording_is_active(void) {
    return is_recording && !is_paused;
}

uint32_t audio_recording_get_duration_ms(void) {
    if (!is_recording) return 0;
    uint32_t now = pdTICKS_TO_MS(xTaskGetTickCount());
    return time_diff_ms(now, recording_start_ms);
}

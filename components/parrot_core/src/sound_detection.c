#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "sound_detection.h"
#define _USE_MATH_DEFINES
#include <math.h>

static inline uint32_t time_diff_ms(uint32_t newer, uint32_t older) {
    if (newer >= older) {
        return newer - older;
    } else {
        return (UINT32_MAX - older) + newer + 1;
    }
}

#define TAG "SOUND_DETECT"
#define FFT_SIZE 256

static detection_config_t config = {0};
static detection_callback_t callback = NULL;
static bool is_active = false;
static uint32_t last_detection_ms = 0;
static uint32_t detection_start_ms = 0;
static bool detection_ongoing = false;
static float fft_real[FFT_SIZE];
static float fft_imag[FFT_SIZE];

// Simple DFT implementation for Phase 1.
// Replace with ESP-DSP's optimized FFT (esp_dsp.h) in production.
static void compute_dft(const int16_t *samples) {
    for (int i = 0; i < FFT_SIZE; i++) {
        fft_real[i] = (float)samples[i];
        fft_imag[i] = 0.0f;
    }

    for (int k = 0; k < FFT_SIZE; k++) {
        float real_sum = 0;
        float imag_sum = 0;

        for (int n = 0; n < FFT_SIZE; n++) {
            float angle = 2 * M_PI * k * n / FFT_SIZE;
            real_sum += fft_real[n] * cosf(angle) + fft_imag[n] * sinf(angle);
            imag_sum += fft_imag[n] * cosf(angle) - fft_real[n] * sinf(angle);
        }

        fft_real[k] = real_sum;
        fft_imag[k] = imag_sum;
    }
}

static float find_peak_frequency(void) {
    float max_magnitude = 0;
    int peak_bin = 0;

    for (int i = 0; i < FFT_SIZE / 2; i++) {
        float magnitude = sqrtf(fft_real[i] * fft_real[i] + fft_imag[i] * fft_imag[i]);
        if (magnitude > max_magnitude) {
            max_magnitude = magnitude;
            peak_bin = i;
        }
    }

    float peak_freq = (float)peak_bin * config.sample_rate / FFT_SIZE;
    return peak_freq;
}

static float calculate_rms_db(const int16_t *samples, size_t count) {
    int64_t sum = 0;
    for (size_t i = 0; i < count; i++) {
        int32_t sample = samples[i];
        sum += (int64_t)sample * sample;
    }

    float rms = sqrtf((float)sum / count);
    if (rms <= 0) return -100.0f;
    return 20.0f * log10f(rms / 32768.0f);
}

esp_err_t sound_detection_init(const detection_config_t *cfg) {
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }

    if (cfg->frequency_min_hz >= cfg->frequency_max_hz) {
        ESP_LOGE(TAG, "Invalid frequency range: min=%.0f Hz, max=%.0f Hz",
                 cfg->frequency_min_hz, cfg->frequency_max_hz);
        return ESP_ERR_INVALID_ARG;
    }

    if (cfg->sample_rate == 0) {
        ESP_LOGE(TAG, "Invalid sample rate: %u Hz", cfg->sample_rate);
        return ESP_ERR_INVALID_ARG;
    }

    if (cfg->frequency_max_hz > cfg->sample_rate / 2) {
        ESP_LOGW(TAG, "Frequency max (%.0f Hz) exceeds Nyquist limit (%.0f Hz)",
                 cfg->frequency_max_hz, cfg->sample_rate / 2.0f);
    }

    config = *cfg;
    ESP_LOGI(TAG, "Sound detection initialized: threshold=%.1f dB, freq=[%.0f-%.0f] Hz, sample_rate=%u Hz",
             config.rms_threshold_db, config.frequency_min_hz, config.frequency_max_hz, config.sample_rate);

    return ESP_OK;
}

esp_err_t sound_detection_start(detection_callback_t cb) {
    callback = cb;
    is_active = true;
    detection_ongoing = false;
    ESP_LOGI(TAG, "Sound detection started");
    return ESP_OK;
}

esp_err_t sound_detection_stop(void) {
    is_active = false;
    callback = NULL;
    detection_ongoing = false;
    ESP_LOGI(TAG, "Sound detection stopped");
    return ESP_OK;
}

esp_err_t sound_detection_process(const int16_t *samples, size_t count, uint32_t timestamp_ms) {
    if (!is_active || !callback || count < FFT_SIZE) {
        return ESP_ERR_INVALID_STATE;
    }

    // Check cooldown
    if (time_diff_ms(timestamp_ms, last_detection_ms) < config.cooldown_ms) {
        return ESP_OK;
    }

    // Calculate RMS level
    float rms_db = calculate_rms_db(samples, count);

    if (rms_db > config.rms_threshold_db) {
        // Compute DFT to get frequency information
        compute_dft(samples);
        float peak_freq = find_peak_frequency();

        // Check if frequency is in parrot vocal range
        if (peak_freq >= config.frequency_min_hz && peak_freq <= config.frequency_max_hz) {
            if (!detection_ongoing) {
                detection_start_ms = timestamp_ms;
                detection_ongoing = true;
                ESP_LOGD(TAG, "Detection started: %.1f dB @ %.0f Hz", rms_db, peak_freq);
            }

            if (time_diff_ms(timestamp_ms, detection_start_ms) >= config.hold_time_ms) {
                last_detection_ms = timestamp_ms;
                callback(rms_db, peak_freq, timestamp_ms);
                detection_ongoing = false;

                ESP_LOGD(TAG, "Detection triggered: %.1f dB @ %.0f Hz (held %ums)",
                         rms_db, peak_freq, time_diff_ms(timestamp_ms, detection_start_ms));
                return ESP_OK;
            }
        } else {
            detection_ongoing = false;
        }
    } else {
        detection_ongoing = false;
    }

    return ESP_OK;
}

bool sound_detection_is_active(void) {
    return is_active;
}

#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

typedef struct hal_audio_s hal_audio_t;

typedef enum {
    HAL_AUDIO_DIR_RX = 0,
    HAL_AUDIO_DIR_TX,
    HAL_AUDIO_DIR_DUPLEX,
} hal_audio_dir_t;

typedef struct {
    uint32_t        sample_rate;
    uint8_t         bits_per_sample;
    uint8_t         channels;
    hal_audio_dir_t direction;
    size_t          buf_size;
} hal_audio_cfg_t;

struct hal_audio_s {
    esp_err_t (*init)(hal_audio_t *audio, const hal_audio_cfg_t *cfg);
    esp_err_t (*deinit)(hal_audio_t *audio);
    esp_err_t (*set_volume)(hal_audio_t *audio, uint8_t vol_pct);
    esp_err_t (*play)(hal_audio_t *audio, const int16_t *samples, size_t n_samples);
    esp_err_t (*record_start)(hal_audio_t *audio);
    esp_err_t (*record_stop)(hal_audio_t *audio);
    esp_err_t (*record_read)(hal_audio_t *audio, int16_t *buf, size_t n_samples, size_t *got);
    void      *priv;
};

esp_err_t hal_audio_create(const hal_audio_cfg_t *cfg, hal_audio_t **out);
void      hal_audio_destroy(hal_audio_t *audio);

#include <string.h>
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "buddy_hal/hal_audio.h"
#include "buddy_hal/hal.h"

#define TAG "AUDIO"

typedef struct {
    esp_codec_dev_handle_t spk;
    esp_codec_dev_handle_t mic;
    bool                   recording;
    uint32_t               sample_rate;
} audio_priv_t;

static hal_audio_t  s_audio;
static audio_priv_t s_priv;

static esp_err_t audio_init(hal_audio_t *audio, const hal_audio_cfg_t *cfg)
{
    audio_priv_t *p = (audio_priv_t *)audio->priv;
    p->sample_rate = cfg ? cfg->sample_rate : 16000;

    /*
     * Configure I2S for 16 kHz mono duplex.
     * BSP default is 22050 Hz; we override so mic and speaker both run at
     * the voice-optimised rate without resampling.
     */
    i2s_std_config_t i2s_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(p->sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                         I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_SCLK,
            .ws   = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT,
            .din  = BSP_I2S_DSIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };

    esp_err_t r = bsp_audio_init(&i2s_cfg);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "bsp_audio_init failed: %s", esp_err_to_name(r));
        return r;
    }

    p->spk = bsp_audio_codec_speaker_init();
    p->mic = bsp_audio_codec_microphone_init();
    ESP_LOGI(TAG, "codec handles: spk=%p mic=%p", p->spk, p->mic);

    esp_codec_dev_sample_info_t info = {
        .sample_rate     = p->sample_rate,
        .bits_per_sample = cfg ? cfg->bits_per_sample : 16,
        .channel         = 1,
    };

    if (p->spk) {
        esp_codec_dev_open(p->spk, &info);
        esp_codec_dev_set_out_vol(p->spk, 100);
        ESP_LOGI(TAG, "speaker ready (ES8311, %lu Hz mono)", (unsigned long)p->sample_rate);
    }
    if (p->mic) {
        esp_codec_dev_open(p->mic, &info);
        esp_codec_dev_set_in_gain(p->mic, 37.5f); /* 37.5 dB — ES7210 PGA hardware maximum */
        esp_codec_dev_set_in_mute(p->mic, true);  /* muted until record_start */
        ESP_LOGI(TAG, "microphone ready (ES7210, %lu Hz mono)", (unsigned long)p->sample_rate);
    }
    return ESP_OK;
}

static esp_err_t audio_deinit(hal_audio_t *audio)
{
    audio_priv_t *p = (audio_priv_t *)audio->priv;
    if (p->spk) { esp_codec_dev_close(p->spk); esp_codec_dev_delete(p->spk); p->spk = NULL; }
    if (p->mic) { esp_codec_dev_close(p->mic); esp_codec_dev_delete(p->mic); p->mic = NULL; }
    return ESP_OK;
}

static esp_err_t audio_set_volume(hal_audio_t *audio, uint8_t vol_pct)
{
    audio_priv_t *p = (audio_priv_t *)audio->priv;
    if (p->spk) esp_codec_dev_set_out_vol(p->spk, (int)vol_pct);
    return ESP_OK;
}

static esp_err_t audio_play(hal_audio_t *audio, const int16_t *samples, size_t n_samples)
{
    audio_priv_t *p = (audio_priv_t *)audio->priv;
    if (!p->spk) return ESP_ERR_INVALID_STATE;
    int rc = esp_codec_dev_write(p->spk, (void *)samples, (int)(n_samples * sizeof(int16_t)));
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t audio_record_start(hal_audio_t *audio)
{
    audio_priv_t *p = (audio_priv_t *)audio->priv;
    if (!p->mic) return ESP_ERR_NOT_SUPPORTED;
    if (p->recording) return ESP_OK;
    esp_codec_dev_set_in_mute(p->mic, false);
    p->recording = true;
    ESP_LOGI(TAG, "recording started");
    return ESP_OK;
}

static esp_err_t audio_record_stop(hal_audio_t *audio)
{
    audio_priv_t *p = (audio_priv_t *)audio->priv;
    if (!p->mic) return ESP_ERR_NOT_SUPPORTED;
    p->recording = false;
    esp_codec_dev_set_in_mute(p->mic, true);
    ESP_LOGI(TAG, "recording stopped");
    return ESP_OK;
}

static esp_err_t audio_record_read(hal_audio_t *audio, int16_t *buf, size_t n_samples, size_t *got)
{
    audio_priv_t *p = (audio_priv_t *)audio->priv;
    if (!p->mic || !p->recording) return ESP_ERR_INVALID_STATE;
    int rc = esp_codec_dev_read(p->mic, buf, (int)(n_samples * sizeof(int16_t)));
    if (got) *got = (rc == 0) ? n_samples : 0;
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t hal_audio_create(const hal_audio_cfg_t *cfg, hal_audio_t **out)
{
    memset(&s_audio, 0, sizeof(s_audio));
    memset(&s_priv,  0, sizeof(s_priv));

    s_audio.init          = audio_init;
    s_audio.deinit        = audio_deinit;
    s_audio.set_volume    = audio_set_volume;
    s_audio.play          = audio_play;
    s_audio.record_start  = audio_record_start;
    s_audio.record_stop   = audio_record_stop;
    s_audio.record_read   = audio_record_read;
    s_audio.priv          = &s_priv;

    esp_err_t r = audio_init(&s_audio, cfg);
    if (r != ESP_OK) return r;

    *out = &s_audio;
    return ESP_OK;
}

void hal_audio_destroy(hal_audio_t *audio)
{
    audio_deinit(audio);
}

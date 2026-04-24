#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "audio_manager.h"
#include "parrot_core.h"
#include "sound_detection.h"
#include "agent_core.h"

#define TAG "PARROT"

#ifndef CONFIG_PARROT_ENABLED
#define CONFIG_PARROT_ENABLED 1
#endif
#ifndef CONFIG_PARROT_DETECT_SENSITIVITY
#define CONFIG_PARROT_DETECT_SENSITIVITY 3
#endif
#ifndef CONFIG_PARROT_TRIGGER_HOLD_FRAMES
#define CONFIG_PARROT_TRIGGER_HOLD_FRAMES 3
#endif
#ifndef CONFIG_PARROT_REPLY_COOLDOWN_MS
#define CONFIG_PARROT_REPLY_COOLDOWN_MS 1200
#endif
#ifndef CONFIG_PARROT_REPLY_VOLUME_PCT
#define CONFIG_PARROT_REPLY_VOLUME_PCT 75
#endif
#ifndef CONFIG_PARROT_MIN_TRIGGER_RMS
#define CONFIG_PARROT_MIN_TRIGGER_RMS 1200
#endif

#define SAMPLE_RATE_HZ      16000
#define LISTENER_TASK_MS    30
#define MAX_VOICES          3

typedef struct {
    int16_t *pcm;
    size_t n_samples;
} parrot_voice_t;

static TaskHandle_t s_task = NULL;
static volatile bool s_running = false;
static volatile bool s_trigger_pending = false;

static volatile uint32_t s_last_rms = 0;
static float s_noise_rms = 400.0f;
static int s_consecutive_hits = 0;
static int64_t s_last_reply_us = 0;

static parrot_voice_t s_voices[MAX_VOICES] = {0};
static int s_last_voice = -1;

static float sensitivity_multiplier(int level)
{
    switch (level) {
    case 1: return 2.0f;
    case 2: return 2.6f;
    case 3: return 3.2f;
    case 4: return 4.0f;
    default: return 5.0f;
    }
}

static uint32_t chunk_rms(const int16_t *pcm, size_t n)
{
    if (!pcm || n == 0) return 0;

    uint64_t acc = 0;
    for (size_t i = 0; i < n; i += 2) {
        int32_t v = pcm[i];
        acc += (uint64_t)(v * v);
    }
    size_t used = (n + 1) / 2;
    if (used == 0) return 0;

    return (uint32_t)sqrt((double)(acc / used));
}

static void build_voice(int16_t *buf, size_t n, float f0, float sweep, float vib_hz)
{
    if (!buf || n == 0) return;

    for (size_t i = 0; i < n; i++) {
        float t = (float)i / (float)SAMPLE_RATE_HZ;
        float env = 1.0f;
        float attack = 0.015f;
        float release = 0.06f;
        float dur = (float)n / (float)SAMPLE_RATE_HZ;

        if (t < attack) env = t / attack;
        if (t > dur - release) {
            float tail = (dur - t) / release;
            if (tail < env) env = tail;
        }
        if (env < 0.0f) env = 0.0f;

        float vib = sinf(2.0f * (float)M_PI * vib_hz * t) * 0.08f;
        float inst_f = f0 + sweep * t + f0 * vib;
        float p = 2.0f * (float)M_PI * inst_f * t;

        float w = 0.72f * sinf(p)
                + 0.22f * sinf(2.01f * p)
                + 0.12f * sinf(3.03f * p);

        int32_t s = (int32_t)(w * env * 22000.0f);
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        buf[i] = (int16_t)s;
    }
}

static esp_err_t create_voice_bank(void)
{
    const size_t v0 = SAMPLE_RATE_HZ * 14 / 100;
    const size_t v1 = SAMPLE_RATE_HZ * 18 / 100;
    const size_t v2 = SAMPLE_RATE_HZ * 11 / 100;

    s_voices[0].pcm = heap_caps_malloc(v0 * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_voices[1].pcm = heap_caps_malloc(v1 * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_voices[2].pcm = heap_caps_malloc(v2 * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    for (int i = 0; i < MAX_VOICES; i++) {
        if (!s_voices[i].pcm) {
            s_voices[i].pcm = malloc((i == 0 ? v0 : (i == 1 ? v1 : v2)) * sizeof(int16_t));
        }
    }

    if (!s_voices[0].pcm || !s_voices[1].pcm || !s_voices[2].pcm) {
        ESP_LOGE(TAG, "voice bank alloc failed");
        return ESP_ERR_NO_MEM;
    }

    s_voices[0].n_samples = v0;
    s_voices[1].n_samples = v1;
    s_voices[2].n_samples = v2;

    build_voice(s_voices[0].pcm, v0, 1300.0f, 2200.0f, 11.0f);
    build_voice(s_voices[1].pcm, v1, 900.0f,  1200.0f, 7.0f);
    build_voice(s_voices[2].pcm, v2, 1700.0f, -900.0f, 14.0f);
    return ESP_OK;
}

static int choose_voice(void)
{
    int idx = (int)(esp_random() % MAX_VOICES);
    if (idx == s_last_voice) idx = (idx + 1) % MAX_VOICES;
    s_last_voice = idx;
    return idx;
}

static void on_audio_chunk(const int16_t *pcm, size_t n_samples, void *ctx)
{
#if !CONFIG_PARROT_ENABLED
    (void)pcm;
    (void)n_samples;
    (void)ctx;
    return;
#else
    (void)ctx;

    if (!s_running) return;
    if (audio_manager_is_playing()) {
        s_consecutive_hits = 0;
        return;
    }

    uint32_t rms = chunk_rms(pcm, n_samples);
    s_last_rms = rms;

    float m = sensitivity_multiplier(CONFIG_PARROT_DETECT_SENSITIVITY);
    float dyn = s_noise_rms * m;
    float threshold = dyn > CONFIG_PARROT_MIN_TRIGGER_RMS ? dyn : CONFIG_PARROT_MIN_TRIGGER_RMS;

    if ((float)rms < s_noise_rms * 1.2f) {
        s_noise_rms = s_noise_rms * 0.985f + (float)rms * 0.015f;
    }

    if ((float)rms > threshold) {
        s_consecutive_hits++;
    } else {
        if (s_consecutive_hits > 0) s_consecutive_hits--;
    }

    if (s_consecutive_hits >= CONFIG_PARROT_TRIGGER_HOLD_FRAMES) {
        int64_t now_us = esp_timer_get_time();
        int64_t cooldown_us = (int64_t)CONFIG_PARROT_REPLY_COOLDOWN_MS * 1000;
        if (now_us - s_last_reply_us >= cooldown_us) {
            s_trigger_pending = true;
            s_consecutive_hits = 0;
        }
    }

    sound_detection_process(pcm, n_samples, pdTICKS_TO_MS(xTaskGetTickCount()));
#endif
}

static void parrot_task(void *arg)
{
    ESP_LOGI(TAG, "parrot listener started");

    for (;;) {
#if CONFIG_PARROT_ENABLED
        if (s_trigger_pending && !audio_manager_is_playing()) {
            s_trigger_pending = false;
            int idx = choose_voice();
            audio_manager_set_volume(CONFIG_PARROT_REPLY_VOLUME_PCT);
            esp_err_t r = audio_manager_play_raw(s_voices[idx].pcm, s_voices[idx].n_samples);
            if (r == ESP_OK) {
                s_last_reply_us = esp_timer_get_time();
                ESP_LOGI(TAG, "reply voice=%d rms=%lu noise=%.1f", idx,
                         (unsigned long)s_last_rms, (double)s_noise_rms);
            } else {
                ESP_LOGW(TAG, "playback failed: %s", esp_err_to_name(r));
            }
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(LISTENER_TASK_MS));
    }
}

static void on_sound_detected(float rms_db, float peak_freq, uint32_t timestamp_ms) {
    agent_event_t evt = {
        .type = AGENT_EVT_AUDIO_DETECTED,
        .timestamp_ms = timestamp_ms,
        .audio_detected = {
            .rms_level = rms_db,
            .peak_frequency = peak_freq
        }
    };
    agent_core_post_event(&evt);
}

esp_err_t parrot_core_init(void)
{
#if !CONFIG_PARROT_ENABLED
    ESP_LOGI(TAG, "disabled in Kconfig");
    return ESP_OK;
#else
    ESP_ERROR_CHECK(create_voice_bank());
    audio_manager_set_chunk_cb(on_audio_chunk, NULL);

    // Initialize sound detection with relaxed settings
    detection_config_t det_config = {
        .rms_threshold_db = -40.0f,
        .frequency_min_hz = 1000.0f,
        .frequency_max_hz = 8000.0f,
        .sample_rate = 16000,
        .hold_time_ms = 100,
        .cooldown_ms = 500
    };

    esp_err_t ret = sound_detection_init(&det_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize sound detection");
        return ret;
    }

    return ESP_OK;
#endif
}

esp_err_t parrot_core_start(void)
{
#if !CONFIG_PARROT_ENABLED
    return ESP_OK;
#else
    if (s_running) return ESP_OK;

    s_running = true;
    ESP_ERROR_CHECK(audio_manager_record_start());
    ESP_ERROR_CHECK(sound_detection_start(on_sound_detected));

    BaseType_t r = xTaskCreatePinnedToCore(parrot_task, "parrot_core",
                                           4096, NULL, 6, &s_task, 1);
    if (r != pdPASS) {
        s_running = false;
        return ESP_FAIL;
    }
    return ESP_OK;
#endif
}

bool parrot_core_is_running(void)
{
    return s_running;
}

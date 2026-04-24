#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "audio_manager.h"
#include "audio_recording.h"

#define TAG "AUDIO_MGR"

/* ── Tunables ─────────────────────────────────────────────────────────────── */
#define RECORD_CHUNK_SAMPLES    512          /* ~32 ms at 16 kHz             */
#define RECORD_SAMPLE_RATE      16000
#define AUDIO_MANAGER_MAX_RECORD_S   10      /* max recording buffer seconds  */
#define MAX_RECORD_SAMPLES     (AUDIO_MANAGER_MAX_RECORD_S * RECORD_SAMPLE_RATE)

#define PLAY_CHUNK_SAMPLES      512          /* playback granularity for stop */
#define PLAY_QUEUE_DEPTH        8

/* ── Internal types ───────────────────────────────────────────────────────── */
typedef struct {
    int16_t *pcm;        /* heap-allocated; freed by play_task after use      */
    size_t   n_samples;
} play_req_t;

/* ── State ────────────────────────────────────────────────────────────────── */
static hal_audio_t           *s_audio         = NULL;
static TaskHandle_t           s_record_task   = NULL;
static TaskHandle_t           s_play_task     = NULL;
static QueueHandle_t          s_play_queue    = NULL;

static volatile bool          s_recording     = false;
static volatile bool          s_stop_play     = false;
static volatile bool          s_playing       = false;

static audio_chunk_cb_t       s_chunk_cb      = NULL;
static void                  *s_chunk_ctx     = NULL;
static audio_record_done_cb_t s_done_cb       = NULL;
static void                  *s_done_ctx      = NULL;

/* PSRAM-backed recording ring buffer */
static int16_t               *s_rec_buf       = NULL;
static size_t                 s_rec_pos        = 0;

/* Continuous recording mode state */
static volatile bool          recording_mode_enabled = false;
static QueueHandle_t          audio_data_queue       = NULL;

/* ── Recording task ───────────────────────────────────────────────────────── */

static void record_task(void *arg)
{
    static int16_t chunk[RECORD_CHUNK_SAMPLES];

    for (;;) {
        /* Park here until record_start() wakes us */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        ESP_LOGI(TAG, "record task active");
        s_rec_pos = 0;

        if (s_audio->record_start) {
            s_audio->record_start(s_audio);
        }

        while (s_recording) {
            size_t got = 0;
            esp_err_t r = s_audio->record_read(s_audio, chunk, RECORD_CHUNK_SAMPLES, &got);
            if (r != ESP_OK || got == 0) {
                vTaskDelay(1);
                continue;
            }

            /* Forward to streaming callback */
            if (s_chunk_cb) {
                s_chunk_cb(chunk, got, s_chunk_ctx);
            }

            /* Push chunk to continuous recording queue if enabled */
            if (recording_mode_enabled && audio_data_queue) {
                audio_chunk_t aq_chunk;
                size_t copy_samples = got < 512 ? got : 512;
                memcpy(aq_chunk.pcm, chunk, copy_samples * sizeof(int16_t));
                aq_chunk.n_samples = copy_samples;
                xQueueSend(audio_data_queue, &aq_chunk, 0);
            }

            /* Append to session buffer */
            size_t remaining = MAX_RECORD_SAMPLES - s_rec_pos;
            size_t to_copy   = got < remaining ? got : remaining;
            if (to_copy > 0) {
                memcpy(s_rec_buf + s_rec_pos, chunk, to_copy * sizeof(int16_t));
                s_rec_pos += to_copy;
            }
        }

        if (s_audio->record_stop) {
            s_audio->record_stop(s_audio);
        }

        ESP_LOGI(TAG, "recording done: %u samples (%.1f s)",
                 (unsigned)s_rec_pos,
                 (float)s_rec_pos / RECORD_SAMPLE_RATE);

        if (s_done_cb) {
            s_done_cb(s_rec_buf, s_rec_pos, RECORD_SAMPLE_RATE, s_done_ctx);
        }
    }
}

/* ── Playback task ────────────────────────────────────────────────────────── */

static void play_task(void *arg)
{
    play_req_t req;

    for (;;) {
        if (xQueueReceive(s_play_queue, &req, portMAX_DELAY) != pdTRUE) continue;

        s_playing   = true;
        s_stop_play = false;

        size_t offset = 0;
        while (offset < req.n_samples && !s_stop_play) {
            size_t chunk = req.n_samples - offset;
            if (chunk > PLAY_CHUNK_SAMPLES) chunk = PLAY_CHUNK_SAMPLES;

            if (s_audio->play) {
                s_audio->play(s_audio, req.pcm + offset, chunk);
            }
            offset += chunk;
        }

        free(req.pcm);

        if (s_stop_play) {
            /* Drain the rest of the queue */
            while (xQueueReceive(s_play_queue, &req, 0) == pdTRUE) {
                free(req.pcm);
            }
            s_stop_play = false;
            ESP_LOGI(TAG, "playback stopped");
        }

        if (uxQueueMessagesWaiting(s_play_queue) == 0) {
            s_playing = false;
        }
    }
}

/* ── Public API ───────────────────────────────────────────────────────────── */

esp_err_t audio_manager_init(hal_audio_t *audio)
{
    if (!audio) return ESP_ERR_INVALID_ARG;
    s_audio = audio;

    /* Allocate recording buffer in PSRAM if available, else internal RAM */
    s_rec_buf = heap_caps_malloc(MAX_RECORD_SAMPLES * sizeof(int16_t),
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_rec_buf) {
        s_rec_buf = malloc(MAX_RECORD_SAMPLES * sizeof(int16_t));
    }
    if (!s_rec_buf) {
        ESP_LOGE(TAG, "failed to alloc %u byte record buffer",
                 (unsigned)(MAX_RECORD_SAMPLES * sizeof(int16_t)));
        return ESP_ERR_NO_MEM;
    }

    s_play_queue = xQueueCreate(PLAY_QUEUE_DEPTH, sizeof(play_req_t));
    if (!s_play_queue) return ESP_ERR_NO_MEM;

    ESP_LOGI(TAG, "init ok (max record: %d s, buf: %u KB)",
             AUDIO_MANAGER_MAX_RECORD_S,
             (unsigned)(MAX_RECORD_SAMPLES * sizeof(int16_t) / 1024));
    return ESP_OK;
}

esp_err_t audio_manager_start(void)
{
    BaseType_t r;

    r = xTaskCreatePinnedToCore(record_task, "audio_rec",
                                 4096, NULL, 7, &s_record_task, 1);
    if (r != pdPASS) return ESP_FAIL;

    r = xTaskCreatePinnedToCore(play_task, "audio_play",
                                 4096, NULL, 7, &s_play_task, 1);
    if (r != pdPASS) return ESP_FAIL;

    return ESP_OK;
}

esp_err_t audio_manager_record_start(void)
{
    if (!s_audio || !s_record_task) return ESP_ERR_INVALID_STATE;
    if (s_recording) return ESP_OK;

    s_recording = true;
    xTaskNotifyGive(s_record_task);
    return ESP_OK;
}

esp_err_t audio_manager_record_stop(void)
{
    if (!s_recording) return ESP_OK;
    s_recording = false;          /* record_task checks this flag and exits loop */
    return ESP_OK;
}

bool audio_manager_is_recording(void)
{
    return s_recording;
}

void audio_manager_set_chunk_cb(audio_chunk_cb_t cb, void *ctx)
{
    s_chunk_cb  = cb;
    s_chunk_ctx = ctx;
}

void audio_manager_set_record_done_cb(audio_record_done_cb_t cb, void *ctx)
{
    s_done_cb  = cb;
    s_done_ctx = ctx;
}

esp_err_t audio_manager_play_raw(const int16_t *pcm, size_t n_samples)
{
    if (!s_audio || !s_play_queue || !pcm || n_samples == 0)
        return ESP_ERR_INVALID_ARG;

    /* Copy caller's buffer so they can free it immediately */
    int16_t *copy = malloc(n_samples * sizeof(int16_t));
    if (!copy) return ESP_ERR_NO_MEM;
    memcpy(copy, pcm, n_samples * sizeof(int16_t));

    play_req_t req = { .pcm = copy, .n_samples = n_samples };
    if (xQueueSend(s_play_queue, &req, pdMS_TO_TICKS(100)) != pdTRUE) {
        free(copy);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t audio_manager_play_stop(void)
{
    s_stop_play = true;
    return ESP_OK;
}

bool audio_manager_is_playing(void)
{
    return s_playing;
}

esp_err_t audio_manager_set_volume(uint8_t vol_pct)
{
    if (!s_audio || !s_audio->set_volume) return ESP_ERR_INVALID_STATE;
    return s_audio->set_volume(s_audio, vol_pct);
}

/* ── Continuous recording mode ───────────────────────────────────────────── */

esp_err_t audio_manager_read(int16_t *buffer, size_t max_bytes, size_t *bytes_read)
{
    if (!buffer || !bytes_read) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!audio_data_queue) {
        audio_data_queue = xQueueCreate(4, sizeof(audio_chunk_t));
        if (!audio_data_queue) {
            return ESP_FAIL;
        }
    }

    audio_chunk_t chunk;
    if (xQueueReceive(audio_data_queue, &chunk, pdMS_TO_TICKS(100)) == pdTRUE) {
        size_t copy_bytes = (chunk.n_samples * sizeof(int16_t) < max_bytes)
                           ? chunk.n_samples * sizeof(int16_t)
                           : max_bytes;
        memcpy(buffer, chunk.pcm, copy_bytes);
        *bytes_read = copy_bytes;
    } else {
        *bytes_read = 0;
    }

    return ESP_OK;
}

esp_err_t audio_manager_enable_recording_mode(bool enable)
{
    recording_mode_enabled = enable;
    ESP_LOGI(TAG, "Recording mode %s", enable ? "enabled" : "disabled");
    return ESP_OK;
}

esp_err_t audio_manager_set_recording_config(const recording_config_t *config)
{
    return audio_recording_init(config);
}

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include <math.h>

#include "buddy_hal/hal.h"
#include "agent_core.h"
#include "agent_events.h"
#include "audio_manager.h"
#include "audio_recording.h"
#include "parrot_core.h"
#include "sound_detection.h"
#include "transport.h"
#include "protocol.h"
#include "ui/ui_manager.h"
#include "ui/ui_screen_recording.h"
#include "app_config.h"

#define TAG "MAIN"

static uint32_t s_sequence = 0;
static EventGroupHandle_t s_wifi_event_group;
static bool s_wifi_connected = false;

#define WIFI_CONNECTED_BIT (BIT0)

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t init_wifi(void) {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, CONFIG_PARROT_WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, CONFIG_PARROT_WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi init done, connecting to SSID: %s", CONFIG_PARROT_WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                                            pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected successfully");
        return ESP_OK;
    }
    ESP_LOGW(TAG, "WiFi connection timeout, continuing anyway...");
    return ESP_ERR_TIMEOUT;
}




static void on_audio_detected(float rms_db, float peak_freq, uint32_t timestamp_ms) {
    ESP_LOGI(TAG, "Sound detected: %.1f dB @ %.0f Hz", rms_db, peak_freq);

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

static void agent_event_handler(const agent_event_t *evt) {
    switch (evt->type) {
        case AGENT_EVT_AUDIO_DETECTED:
            ESP_LOGI(TAG, "Agent received audio detection");
            break;
        default:
            break;
    }
}

static void on_recording_state_changed(bool is_recording, uint32_t duration_ms) {
    ESP_LOGI(TAG, "Recording state: %s, duration: %u ms", is_recording ? "active" : "stopped", duration_ms);
    bool streaming = transport_is_connected();
    ui_screen_recording_update_state(is_recording, streaming);
}

static void on_audio_chunk(const int16_t *samples, size_t count, uint32_t timestamp_ms) {
    int64_t sum = 0;
    for (size_t i = 0; i < count; i++) {
        int32_t s = samples[i];
        sum += (int64_t)s * s;
    }
    float rms = sqrtf((float)sum / count);
    float db;
    if (rms <= 0) {
        db = -100.0f;
    } else {
        db = 20.0f * log10f(rms / 32768.0f);
    }
    ui_screen_recording_update_level(db);

    uint8_t encoded[2048];
    size_t encoded_len = 0;
    esp_err_t ret = parrot_protocol_encode_audio(samples, count, timestamp_ms, s_sequence++, encoded, sizeof(encoded), &encoded_len);
    if (ret == ESP_OK && transport_is_connected()) {
        transport_send_audio(encoded, encoded_len, timestamp_ms);
    }
}

static void on_transport_state_changed(transport_state_t state, void *ctx) {
    (void)ctx;
    ESP_LOGI(TAG, "Transport state: %d", state);
    bool connected = (state == TRANSPORT_STATE_CONNECTED);
    ui_screen_recording_set_connected(connected);
    if (connected) {
        char control_msg[256];
        snprintf(control_msg, sizeof(control_msg),
                 "{\"type\":\"recording_start\",\"timestamp\":%" PRIu32 ",\"sample_rate\":%d,\"channels\":1}",
                 pdTICKS_TO_MS(xTaskGetTickCount()), CONFIG_PARROT_RECORDING_SAMPLE_RATE);
        transport_send_control(control_msg);
    }
}

static void recording_duration_timer_cb(TimerHandle_t xTimer) {
    (void)xTimer;
    if (audio_recording_is_active()) {
        uint32_t duration = audio_recording_get_duration_ms();
        ui_screen_recording_update_duration(duration);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "%s v%s starting", APP_NAME, APP_VERSION);

    /* 1. Storage (NVS) */
    ESP_ERROR_CHECK(hal_storage_init());

    /* 2. Hardware init */
    hal_display_cfg_t disp_cfg = {
        .width           = 320,
        .height          = 240,
        .rotation        = 0,
        .double_buffered = true,
        .buf_size_px     = 320 * 50,
    };
    ESP_ERROR_CHECK(hal_display_create(&disp_cfg, &g_hal.display));
    ESP_ERROR_CHECK(hal_touch_create(&g_hal.touch));
    ESP_ERROR_CHECK(hal_buttons_create(&g_hal.buttons));
    ESP_ERROR_CHECK(hal_led_create(&g_hal.led));

    hal_audio_cfg_t audio_cfg = {
        .sample_rate     = 16000,
        .bits_per_sample = 16,
        .channels        = 1,
        .direction       = HAL_AUDIO_DIR_DUPLEX,
        .buf_size        = 2048,
    };
    ESP_ERROR_CHECK(hal_audio_create(&audio_cfg, &g_hal.audio));

    /* 3. Agent core */
    ESP_ERROR_CHECK(agent_core_init());

    /* 4. Audio manager */
    ESP_ERROR_CHECK(audio_manager_init(g_hal.audio));

    /* 5. UI — show boot screen while remaining init runs */
    ESP_ERROR_CHECK(ui_manager_init());
    ui_manager_show(UI_SCREEN_BOOT, UI_ANIM_NONE);

    /* 6. Parrot Buddy listener */
    ESP_ERROR_CHECK(audio_manager_start());
    ESP_ERROR_CHECK(parrot_core_init());
    ESP_ERROR_CHECK(parrot_core_start());

    /* 7. Agent core start */
    ESP_ERROR_CHECK(agent_core_start());

    /* 8. Recording configuration */
    recording_config_t rec_config = {
        .sample_rate = CONFIG_PARROT_RECORDING_SAMPLE_RATE,
        .silence_timeout_ms = CONFIG_PARROT_RECORDING_SILENCE_TIMEOUT_MS,
        .min_duration_ms = CONFIG_PARROT_RECORDING_MIN_DURATION_MS,
        .detection_threshold_db = CONFIG_PARROT_RECORDING_DETECTION_THRESHOLD_DB,
        .chunk_callback = on_audio_chunk,
        .state_callback = on_recording_state_changed
    };

    ESP_ERROR_CHECK(audio_manager_set_recording_config(&rec_config));
    ESP_ERROR_CHECK(audio_manager_enable_recording_mode(true));
    ESP_ERROR_CHECK(audio_recording_start());

    static StaticTimer_t duration_timer_buf;
    xTimerCreateStatic("rec_dur", pdMS_TO_TICKS(200), pdTRUE, NULL,
                       recording_duration_timer_cb, &duration_timer_buf);

    /* 9. Register agent core callback */
    ESP_ERROR_CHECK(agent_core_register_callback(agent_event_handler));

    /* 10. Sound detection */
    detection_config_t det_config = {
        .rms_threshold_db = CONFIG_PARROT_RECORDING_DETECTION_THRESHOLD_DB,
        .frequency_min_hz = 1000.0f,
        .frequency_max_hz = 8000.0f,
        .sample_rate = CONFIG_PARROT_RECORDING_SAMPLE_RATE,
        .hold_time_ms = 100,
        .cooldown_ms = 500
    };

    ESP_ERROR_CHECK(sound_detection_init(&det_config));
    ESP_ERROR_CHECK(sound_detection_start(on_audio_detected));

    /* 11. WiFi initialization (required for transport layer) */
    ESP_ERROR_CHECK(init_wifi());

    /* 12. Transport layer (Phase 2: WebSocket streaming) */
    transport_config_t transport_cfg = {
        .server_host = CONFIG_PARROT_RECORDING_WS_SERVER,
        .server_port = CONFIG_PARROT_RECORDING_WS_PORT,
        .ws_path = "/parrot",
        .ring_buffer_size_ms = CONFIG_PARROT_RECORDING_BUFFER_SIZE_MS,
        .state_callback = on_transport_state_changed,
        .data_callback = NULL,
        .ctx = NULL
    };
    ESP_ERROR_CHECK(transport_init(&transport_cfg));
    ESP_ERROR_CHECK(transport_start());

    /* 13. Switch to main screen */
    vTaskDelay(pdMS_TO_TICKS(500));
    ui_manager_show(UI_SCREEN_MAIN, UI_ANIM_FADE);

    ESP_LOGI(TAG, "startup complete");
}



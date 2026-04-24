#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "agent_core.h"

#define TAG "AGENT_CORE"
#define EVENT_QUEUE_SIZE 32

static QueueHandle_t event_queue = NULL;
static TaskHandle_t agent_task = NULL;
static agent_event_cb_t event_callback = NULL;

static void agent_core_task(void *arg) {
    agent_event_t evt;

    while (1) {
        if (xQueueReceive(event_queue, &evt, portMAX_DELAY) == pdTRUE) {
            ESP_LOGD(TAG, "Processing event type: %d", evt.type);

            if (event_callback) {
                event_callback(&evt);
            }

            if (evt.type == AGENT_EVT_AUDIO_STREAM_DATA && evt.stream_data.data) {
                free(evt.stream_data.data);
            }
        }
    }
}

esp_err_t agent_core_init(void) {
    event_queue = xQueueCreate(EVENT_QUEUE_SIZE, sizeof(agent_event_t));
    if (!event_queue) {
        ESP_LOGE(TAG, "Failed to create event queue");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t agent_core_start(void) {
    if (agent_task) {
        ESP_LOGW(TAG, "Agent core already started");
        return ESP_OK;
    }

    BaseType_t result = xTaskCreate(
        agent_core_task,
        "agent_core",
        8192,
        NULL,
        6,
        &agent_task
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create agent core task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Agent core started");
    return ESP_OK;
}

esp_err_t agent_core_stop(void) {
    if (agent_task) {
        vTaskDelete(agent_task);
        agent_task = NULL;
    }

    if (event_queue) {
        agent_event_t evt;
        while (xQueueReceive(event_queue, &evt, 0) == pdTRUE) {
            if (evt.type == AGENT_EVT_AUDIO_STREAM_DATA && evt.stream_data.data) {
                free(evt.stream_data.data);
            }
        }
        vQueueDelete(event_queue);
        event_queue = NULL;
    }

    event_callback = NULL;

    ESP_LOGI(TAG, "Agent core stopped");
    return ESP_OK;
}

esp_err_t agent_core_post_event(const agent_event_t *evt) {
    if (!evt) {
        return ESP_ERR_INVALID_ARG;
    }

    if (evt->type >= AGENT_EVT_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!event_queue) {
        ESP_LOGE(TAG, "Event queue not initialized");
        return ESP_FAIL;
    }

    if (xQueueSend(event_queue, evt, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Event queue full, dropping event type: %d", evt->type);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t agent_core_register_callback(agent_event_cb_t cb) {
    event_callback = cb;
    return ESP_OK;
}

#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    TRANSPORT_STATE_DISCONNECTED = 0,
    TRANSPORT_STATE_CONNECTING,
    TRANSPORT_STATE_CONNECTED,
    TRANSPORT_STATE_ERROR
} transport_state_t;

typedef void (*transport_state_cb_t)(transport_state_t state, void *ctx);
typedef void (*transport_data_cb_t)(const uint8_t *data, size_t len, void *ctx);

typedef struct {
    const char *server_host;
    uint16_t server_port;
    const char *ws_path;
    size_t ring_buffer_size_ms;
    transport_state_cb_t state_callback;
    transport_data_cb_t data_callback;
    void *ctx;
} transport_config_t;

esp_err_t transport_init(const transport_config_t *config);
esp_err_t transport_start(void);
esp_err_t transport_stop(void);
esp_err_t transport_send_audio(const uint8_t *data, size_t len, uint32_t timestamp_ms);
esp_err_t transport_send_control(const char *json_msg);
transport_state_t transport_get_state(void);
bool transport_is_connected(void);

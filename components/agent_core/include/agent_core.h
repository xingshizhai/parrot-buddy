#pragma once
#include "esp_err.h"
#include "agent_events.h"

typedef void (*agent_event_cb_t)(const agent_event_t *evt);

esp_err_t agent_core_init(void);
esp_err_t agent_core_start(void);
esp_err_t agent_core_stop(void);
esp_err_t agent_core_post_event(const agent_event_t *evt);
esp_err_t agent_core_register_callback(agent_event_cb_t cb);

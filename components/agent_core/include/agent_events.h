#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    AGENT_EVT_AUDIO_DETECTED = 0,
    AGENT_EVT_AUDIO_RECORD_START,
    AGENT_EVT_AUDIO_RECORD_STOP,
    AGENT_EVT_AUDIO_STREAM_DATA,
    AGENT_EVT_AUDIO_STREAM_ERROR,
    AGENT_EVT_MAX
} agent_event_type_t;

typedef struct {
    agent_event_type_t type;
    uint32_t timestamp_ms;
    union {
        struct {
            float rms_level;
            float peak_frequency;
        } audio_detected;
        struct {
            uint32_t duration_ms;
            uint32_t sample_rate;
        } record_start;
        struct {
            uint32_t total_samples;
            uint32_t silence_duration_ms;
        } record_stop;
        struct {
            uint8_t *data;
            size_t length;
            uint32_t sequence;
        } stream_data;
        struct {
            esp_err_t error;
            const char *context;
        } stream_error;
    };
} agent_event_t;

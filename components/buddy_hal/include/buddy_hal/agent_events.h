#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    AGENT_EVT_SESSION_UPDATE = 0,
    AGENT_EVT_APPROVAL_REQUEST,
    AGENT_EVT_APPROVAL_RESOLVED,
    AGENT_EVT_TOKEN_UPDATE,
    AGENT_EVT_TRANSPORT_STATE,
    AGENT_EVT_BUTTON,
    AGENT_EVT_IMU_GESTURE,
    AGENT_EVT_HEARTBEAT_ACK,
    AGENT_EVT_AUDIO_RECORD_START,
    AGENT_EVT_AUDIO_RECORD_STOP,
    AGENT_EVT_TURN_COMPLETE,
    AGENT_EVT_MAX,
} agent_event_type_t;

typedef enum {
    IMU_GESTURE_SHAKE = 0,
    IMU_GESTURE_FACE_DOWN,
    IMU_GESTURE_FACE_UP,
} imu_gesture_t;

typedef struct {
    agent_event_type_t type;
    union {
        struct {
            uint32_t running;
            uint32_t waiting;
            uint32_t tokens_total;
            uint32_t tokens_today;
        } session;
        struct {
            char id[64];
            char tool[32];
            char hint[256];
        } approval_req;
        struct {
            char id[64];
            bool approved;
        } approval_resp;
        struct {
            uint32_t total;
        } tokens;
        struct {
            uint8_t transport_id;
            uint8_t connected;
        } transport;
        struct {
            uint8_t button_id;
            uint8_t event_id;
        } button;
        struct {
            imu_gesture_t gesture;
        } imu;
    } data;
    int64_t timestamp_us;
} agent_event_t;

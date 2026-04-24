# Architecture

## Hardware Reference Mapping

This project targets ESP32-S3-BOX-3 and uses Espressif's ESP-BOX software stack as the hardware reference:

- Reference repository: https://github.com/espressif/esp-box
- Board adaptation in this project: `components/boards/esp32s3_box3/`
- Main managed dependency: `espressif/esp-box-3`

When upgrading ESP-IDF or managed components, verify audio/display/touch behavior against the ESP-BOX reference implementation.

## Component Overview

```
┌─────────────────────────────────────────────────────────┐
│                        main.c                           │
│          (initialization sequence, task creation)       │
└───────────────────────────┬─────────────────────────────┘
                            │
        ┌───────────────────┼───────────────────┐
        ▼                   ▼                   ▼
  ┌──────────┐       ┌──────────┐        ┌──────────────┐
  │transport │       │ protocol │        │  buddy_hal   │
  │  layer   │       │  layer   │        │  (HAL iface) │
  │          │       │          │        └──────┬───────┘
  │ BLE      │       │ claude_  │               │
  │ WebSocket│       │ buddy    │        ┌──────▼───────┐
  │ USB      │       │ openclaw │        │ boards/      │
  └────┬─────┘       │ hermes   │        │ esp32s3_box3 │
       │             └────┬─────┘        │ (BSP bridge) │
       │  raw bytes       │ agent_event_t└──────────────┘
       └──────────────────▼
                  ┌────────────────┐
                  │   agent_core   │◄── imu_monitor
                  │  (event queue) │◄── hal_button callbacks
                  └───────┬────────┘
                          │
            ┌───────────┼───────────┬──────────────┐
            ▼           ▼           ▼              ▼
      ┌─────────┐  ┌──────────┐  ┌──────────┐  ┌─────────────┐
      │  state  │  │    ui    │  │  agent   │  │ parrot_core │
      │ machine │  │ manager  │  │  stats   │  │ detect/reply│
      └─────────┘  └──────────┘  └──────────┘  └──────┬──────┘
                                                │
                                                ▼
                                          ┌─────────────┐
                                          │audio_manager│
                                          │ record/play │
                                          └─────────────┘
```

### Layer responsibilities

| Layer | Component | Role |
|-------|-----------|------|
| Transport | `transport/` | Raw byte I/O over BLE / WebSocket / USB; calls `rx_cb` on each complete `\n`-terminated frame |
| Protocol | `protocol/` | Decodes raw JSON frames into `agent_event_t`; encodes outbound messages back to JSON |
| Agent core | `agent_core/` | Single FreeRTOS queue consumer; drives the state machine and dispatches UI updates |
| Parrot engine | `parrot_core/` | Always-on audio listener, lightweight trigger detection, and automatic reply playback |
| State machine | `state_machine/` | 7-state FSM with transition guards and timer-based transient states |
| UI | `ui/` | LVGL screen stack; always called from within `lvgl_port_lock()` |
| HAL | `buddy_hal/` | Board-independent function-table interfaces for display, touch, audio, buttons, IMU, LED, storage |
| Board | `boards/esp32s3_box3/` | Concrete HAL implementations using the ESP-BOX-3 BSP |

---

## Data Flow

### Local Parrot loop (device-only)

```
audio_manager record task
  └─► chunk callback (parrot_core)
        ├─► RMS/noise-floor update
        ├─► trigger hold-frame decision
        └─► cooldown gate
              └─► choose synthetic parrot voice
                    └─► audio_manager_play_raw()
                          └─► board_audio (ES8311 speaker)
```

The local parrot loop is independent from transport/protocol, so the device can work without backend connectivity.

### Inbound (agent → device)

```
Transport RX interrupt / task
  └─► rx_cb(transport_id, raw_bytes, len)          [transport layer]
        └─► proto->decode(raw_bytes) → agent_event_t[]   [protocol layer]
              └─► xQueueSend(agent_core_queue)            [IPC]
                    └─► agent_core_task consumes event
                          ├─► sm_handle_event()           [state machine]
                          ├─► ui_manager_on_state_change() [UI update]
                          └─► agent_stats_update()        [token accounting]
```

### Outbound (device → agent)

```
User touches Approve/Deny on touchscreen
  └─► LVGL button callback posts AGENT_EVT_APPROVAL_RESOLVED
        └─► agent_core_task encodes response via proto->encode()
              └─► transport_send_all(encoded_json)
                    └─► broadcast to every TRANSPORT_STATE_CONNECTED transport
```

---

## State Machine

### States

| State | Description | LED |
|-------|-------------|-----|
| `SLEEP` | Disconnected or face-down | Off |
| `IDLE` | Connected, no agent activity | Dim white |
| `BUSY` | Agent is running tasks | Slow blue pulse |
| `ATTENTION` | Waiting for user approval | Fast amber blink |
| `CELEBRATE` | Token milestone reached (transient, 3 s) | Rainbow |
| `DIZZY` | Shake gesture detected (transient, 2 s) | Spinning |
| `HEART` | Quick approval ≤5 s (transient, 2 s) | Pink pulse |

### Transitions

| From | Event | To |
|------|-------|----|
| ANY | `TRANSPORT_DISCONNECTED` | SLEEP |
| ANY | `FACE_DOWN` | SLEEP |
| SLEEP | `TRANSPORT_CONNECTED` | IDLE |
| IDLE / BUSY | `SESSION_UPDATE` (running > 0) | BUSY |
| BUSY | `SESSION_UPDATE` (running == 0) | IDLE |
| IDLE / BUSY | `APPROVAL_REQUEST` | ATTENTION (saves `prev_state`) |
| ATTENTION | `APPROVAL_RESOLVED` (approved, ≤ 5 s) | HEART |
| ATTENTION | `APPROVAL_RESOLVED` (denied or timeout) | `prev_state` |
| HEART / CELEBRATE / DIZZY | timer expired | `prev_state` |
| ANY (not ATTENTION) | `TOKEN_UPDATE` | CELEBRATE |
| ANY | `SHAKE_DETECTED` | DIZZY |

Transient states (HEART, CELEBRATE, DIZZY) use a one-shot `esp_timer` internally; when it fires they re-post `SM_EVT_TIMER_EXPIRED` into the agent_core queue so the transition happens on the agent_core task, not the timer ISR.

---

## FreeRTOS Tasks

| Task | Core | Priority | Stack | Runs on |
|------|------|----------|-------|---------|
| `agent_core_task` | 0 | 6 | 8 KB | Queue consumer; drives SM |
| `ble_host_task` | 0 | 5 | 8 KB | `nimble_port_run()` |
| `ws_task` | 0 | 5 | 6 KB | WebSocket event loop |
| `usb_rx_task` | 0 | 4 | 4 KB | USB read loop |
| `lvgl_task` | 1 | 4 | 16 KB | `lv_timer_handler()` + holds LVGL lock |
| `imu_monitor_task` | 1 | 3 | 4 KB | 50 Hz ICM-42670 poll |
| `audio_task` | 1 | 7 | 8 KB | ES7210 capture / ES8311 playback |
| `parrot_core_task` | 1 | 6 | 4 KB | Trigger evaluation + auto-reply scheduling |
| `heartbeat_task` | 0 | 2 | 2 KB | Periodic keepalive send (10 s) |
| `stats_persist_task` | 0 | 1 | 2 KB | NVS flush (60 s) |

**Cross-task rule:** all producers write to `agent_core_queue` (32 slots, `agent_event_t`). `agent_core_task` is the only consumer. All LVGL calls are wrapped in `lvgl_port_lock(portMAX_DELAY)`.

---

## Memory Layout

Flash partition table (`partitions.csv`):

| Name | Type | Offset | Size |
|------|------|--------|------|
| nvs | data/nvs | 0x9000 | 24 KB |
| phy_init | data/phy | 0xF000 | 4 KB |
| factory | app/factory | 0x10000 | 15 MB |
| storage | data/spiffs | 0xF10000 | ~956 KB |

The 15 MB app partition leaves comfortable headroom — the current firmware binary is around 1.3 MB.

---

## Key Design Decisions

**HAL as function tables, not virtual classes** — Each HAL type (`hal_display_t`, `hal_audio_t`, …) is a plain C struct of function pointers. Board implementations fill in the struct at init time. This keeps the interface zero-cost, avoids C++ inheritance, and makes mocking straightforward for unit tests.

**Single event queue into agent_core** — All inputs (transport RX, button presses, IMU gestures, timer ticks) are serialized into one queue. This eliminates the need for mutexes inside agent_core and makes the state machine transitions predictable and easy to trace in logs.

**Protocol choice is compile-time** — Selecting a protocol mode in menuconfig conditionally compiles exactly one adapter. There is no runtime protocol registry to confuse or corrupt. The NVS no longer stores a "current protocol" string.

**`buddy_hal` not `hal`** — The component is named `buddy_hal` to avoid a link-time collision with ESP-IDF's internal `hal` pseudo-component.

**Parrot-first runtime path** — The current product path is local audio interaction: detect call-like audio and respond with synthetic parrot voices. Transport/protocol/agent components remain in-tree for later research-backend integration.

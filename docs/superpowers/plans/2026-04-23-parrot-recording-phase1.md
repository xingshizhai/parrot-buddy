# Parrot Recording Phase 1: Core Infrastructure Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement core event system and audio recording infrastructure for parrot sound recording feature.

**Architecture:** Port smart-buddy's agent_core event-driven architecture, extend audio_manager for continuous recording, add sound detection with relaxed thresholds.

**Tech Stack:** ESP-IDF 5.1+, FreeRTOS, LVGL 8.3

---

## File Structure

### New Files
- `components/agent_core/include/agent_core.h` - Agent core API
- `components/agent_core/include/agent_events.h` - Event definitions with audio extensions
- `components/agent_core/src/agent_core.c` - Event queue consumer task
- `components/audio_manager/include/audio_recording.h` - Recording-specific API
- `components/audio_manager/src/audio_recording.c` - Continuous recording implementation
- `components/parrot_core/include/sound_detection.h` - Relaxed detection logic
- `components/parrot_core/src/sound_detection.c` - Detection implementation

### Modified Files
- `main/main.c:14-60` - Integrate agent_core into startup sequence
- `components/audio_manager/include/audio_manager.h:1-50` - Add recording callbacks
- `components/audio_manager/src/audio_manager.c:1-200` - Extend for continuous recording
- `components/parrot_core/include/parrot_core.h:1-30` - Add detection integration
- `components/parrot_core/src/parrot_core.c:1-150` - Connect to event system
- `components/ui/include/ui/ui_manager.h:1-40` - Add recording screen definition
- `components/ui/src/ui_manager.c:1-100` - Add screen navigation

### Configuration Files
- `main/Kconfig.projbuild` - Add recording configuration options
- `sdkconfig.defaults` - Set default recording parameters

---

## Chunk 1: Agent Core Event System

### Task 1: Create Agent Core Component

**Files:**
- Create: `components/agent_core/include/agent_core.h`
- Create: `components/agent_core/include/agent_events.h`
- Create: `components/agent_core/src/agent_core.c`
- Modify: `main/main.c:14-60`

- [ ] **Step 1: Create agent_events.h with audio extensions**

```c
// components/agent_core/include/agent_events.h
#pragma once
#include <stdint.h>
#include <stdbool.h>

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
```

- [ ] **Step 2: Run verification of header file**

Run: `idf.py build`
Expected: No compilation errors for new header

- [ ] **Step 3: Create agent_core.h API**

```c
// components/agent_core/include/agent_core.h
#pragma once
#include "esp_err.h"
#include "agent_events.h"

typedef void (*agent_event_cb_t)(const agent_event_t *evt);

esp_err_t agent_core_init(void);
esp_err_t agent_core_start(void);
esp_err_t agent_core_stop(void);
esp_err_t agent_core_post_event(const agent_event_t *evt);
esp_err_t agent_core_register_callback(agent_event_cb_t cb);
```

- [ ] **Step 4: Run verification of API header**

Run: `idf.py build`
Expected: No compilation errors

- [ ] **Step 5: Create agent_core.c implementation**

```c
// components/agent_core/src/agent_core.c
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
            
            // Free any dynamically allocated data
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
        6,  // Priority
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
        vQueueDelete(event_queue);
        event_queue = NULL;
    }
    
    ESP_LOGI(TAG, "Agent core stopped");
    return ESP_OK;
}

esp_err_t agent_core_post_event(const agent_event_t *evt) {
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
```

- [ ] **Step 6: Run compilation test**

Run: `idf.py build`
Expected: Successful build with new component

- [ ] **Step 7: Integrate into main.c**

```c
// Modify main/main.c
#include "agent_core.h"

void app_main(void) {
    ESP_LOGI(TAG, "%s v%s starting", APP_NAME, APP_VERSION);
    
    /* 1. Storage (NVS) */
    ESP_ERROR_CHECK(hal_storage_init());
    
    /* 2. Hardware init */
    // ... existing hardware init ...
    
    /* 3. Agent Core */
    ESP_ERROR_CHECK(agent_core_init());
    
    /* 4. Audio manager */
    ESP_ERROR_CHECK(audio_manager_init(g_hal.audio));
    
    /* 5. UI */
    ESP_ERROR_CHECK(ui_manager_init());
    ui_manager_show(UI_SCREEN_BOOT, UI_ANIM_NONE);
    
    /* 6. Parrot Buddy listener */
    ESP_ERROR_CHECK(audio_manager_start());
    ESP_ERROR_CHECK(parrot_core_init());
    ESP_ERROR_CHECK(parrot_core_start());
    
    /* 7. Start agent core */
    ESP_ERROR_CHECK(agent_core_start());
    
    /* 8. Switch to main screen */
    vTaskDelay(pdMS_TO_TICKS(500));
    ui_manager_show(UI_SCREEN_MAIN, UI_ANIM_FADE);
    
    ESP_LOGI(TAG, "startup complete");
}
```

- [ ] **Step 8: Run full build test**

Run: `idf.py build`
Expected: Successful build with agent_core integrated

- [ ] **Step 9: Commit changes**

```bash
git add components/agent_core/ main/main.c
git commit -m "feat: add agent_core event system"
```

### Task 2: Extend Audio Manager for Continuous Recording

**Files:**
- Create: `components/audio_manager/include/audio_recording.h`
- Create: `components/audio_manager/src/audio_recording.c`
- Modify: `components/audio_manager/include/audio_manager.h:1-50`
- Modify: `components/audio_manager/src/audio_manager.c:1-200`

- [ ] **Step 1: Create audio_recording.h API**

```c
// components/audio_manager/include/audio_recording.h
#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

typedef void (*audio_chunk_cb_t)(const int16_t *samples, size_t count, uint32_t timestamp_ms);
typedef void (*recording_state_cb_t)(bool is_recording, uint32_t duration_ms);

typedef struct {
    uint32_t sample_rate;
    uint32_t silence_timeout_ms;
    uint32_t min_duration_ms;
    float detection_threshold_db;
    audio_chunk_cb_t chunk_callback;
    recording_state_cb_t state_callback;
} recording_config_t;

esp_err_t audio_recording_init(const recording_config_t *config);
esp_err_t audio_recording_start(void);
esp_err_t audio_recording_stop(void);
esp_err_t audio_recording_pause(void);
esp_err_t audio_recording_resume(void);
bool audio_recording_is_active(void);
uint32_t audio_recording_get_duration_ms(void);
```

- [ ] **Step 2: Run header verification**

Run: `idf.py build`
Expected: No compilation errors

- [ ] **Step 3: Create audio_recording.c implementation**

```c
// components/audio_manager/src/audio_recording.c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "audio_recording.h"
#include "audio_manager.h"
#include <math.h>

// NOTE: Single-buffer implementation for Phase 1.
// Dual-buffer pipeline (Buffer A: capture, Buffer B: stream) will be added in Phase 2.

// Helper function to handle timer wrap-around
static inline uint32_t time_diff_ms(uint32_t newer, uint32_t older) {
    if (newer >= older) {
        return newer - older;
    } else {
        // Handle wrap-around (occurs every ~49.7 days)
        return (UINT32_MAX - older) + newer + 1;
    }
}

#define TAG "AUDIO_REC"
#define BUFFER_SIZE_MS 1000
#define SILENCE_CHECK_INTERVAL_MS 100

static recording_config_t config = {0};
static TaskHandle_t recording_task = NULL;
static bool is_recording = false;
static bool is_paused = false;
static bool exit_requested = false;
static uint32_t recording_start_ms = 0;
static uint32_t last_loud_ms = 0;
static uint32_t total_samples = 0;

static float calculate_rms(const int16_t *samples, size_t count) {
    int64_t sum = 0;
    for (size_t i = 0; i < count; i++) {
        int32_t sample = samples[i];
        sum += sample * sample;
    }
    return sqrtf((float)sum / count);
}

static float rms_to_db(float rms) {
    if (rms <= 0) return -100.0f;
    return 20.0f * log10f(rms / 32768.0f);
}

static void recording_task_func(void *arg) {
    size_t buffer_size_samples = BUFFER_SIZE_MS * config.sample_rate / 1000;
    int16_t *buffer = heap_caps_malloc(buffer_size_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate recording buffer");
        vTaskDelete(NULL);
        return;
    }
    
    uint32_t buffer_pos = 0;
    
    while (!exit_requested) {
        size_t samples_read = 0;
        size_t max_bytes = (buffer_size_samples - buffer_pos) * sizeof(int16_t);
        
        if (max_bytes == 0) {
            buffer_pos = 0;
            max_bytes = buffer_size_samples * sizeof(int16_t);
        }
        
        esp_err_t ret = audio_manager_read(buffer + buffer_pos, max_bytes, &samples_read);
        
        if (ret != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        
        buffer_pos += samples_read / sizeof(int16_t);
        
        // Process when we have enough samples for analysis
        if (buffer_pos >= config.sample_rate / 10) { // 100ms chunks
            float rms = calculate_rms(buffer, buffer_pos);
            float db = rms_to_db(rms);
            uint32_t now = pdTICKS_TO_MS(xTaskGetTickCount());
            
            if (db > config.detection_threshold_db) {
                last_loud_ms = now;
                
                if (!is_recording) {
                    // Start recording
                    is_recording = true;
                    recording_start_ms = now;
                    total_samples = 0;
                    ESP_LOGI(TAG, "Recording started");
                    
                    if (config.state_callback) {
                        config.state_callback(true, 0);
                    }
                }
            }
            
            if (is_recording && !is_paused) {
                // Send chunk to callback
                if (config.chunk_callback) {
                    config.chunk_callback(buffer, buffer_pos, now);
                }
                
                total_samples += buffer_pos;
                
                // Check for silence timeout
                if (config.silence_timeout_ms > 0 && 
                    time_diff_ms(now, last_loud_ms) > config.silence_timeout_ms) {
                    
                    uint32_t duration = time_diff_ms(now, recording_start_ms);
                    if (duration >= config.min_duration_ms) {
                        // Stop recording due to silence
                        is_recording = false;
                        ESP_LOGI(TAG, "Recording stopped (silence), duration: %ums", duration);
                        
                        if (config.state_callback) {
                            config.state_callback(false, duration);
                        }
                    }
                }
            }
            
            buffer_pos = 0;
        }
        
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    // Clean up heap-allocated buffer
    free(buffer);
    ESP_LOGI(TAG, "Recording task exiting");
    vTaskDelete(NULL);
}

esp_err_t audio_recording_init(const recording_config_t *cfg) {
    if (!cfg || cfg->sample_rate == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    config = *cfg;
    
    return ESP_OK;
}

esp_err_t audio_recording_start(void) {
    if (recording_task) {
        return ESP_OK; // Already running
    }
    
    BaseType_t result = xTaskCreate(
        recording_task_func,
        "audio_rec",
        8192,
        NULL,
        7,  // Higher priority than audio manager
        &recording_task
    );
    
    if (result != pdPASS) {
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Audio recording task started");
    return ESP_OK;
}

esp_err_t audio_recording_stop(void) {
    if (recording_task) {
        exit_requested = true;
        vTaskDelay(pdMS_TO_TICKS(100)); // Wait for task to exit
        recording_task = NULL;
    }
    
    is_recording = false;
    is_paused = false;
    exit_requested = false;
    
    ESP_LOGI(TAG, "Audio recording stopped");
    return ESP_OK;
}

esp_err_t audio_recording_pause(void) {
    is_paused = true;
    return ESP_OK;
}

esp_err_t audio_recording_resume(void) {
    is_paused = false;
    last_loud_ms = pdTICKS_TO_MS(xTaskGetTickCount());
    return ESP_OK;
}

bool audio_recording_is_active(void) {
    return is_recording && !is_paused;
}

uint32_t audio_recording_get_duration_ms(void) {
    if (!is_recording) return 0;
    uint32_t now = pdTICKS_TO_MS(xTaskGetTickCount());
    return time_diff_ms(now, recording_start_ms);
}
```

- [ ] **Step 4: Run compilation test**

Run: `idf.py build`
Expected: Successful build with audio_recording component

- [ ] **Step 5: Extend audio_manager.h for recording integration**

```c
// Add to components/audio_manager/include/audio_manager.h
#include "audio_recording.h"

// Internal chunk structure for bridging callback to read interface
typedef struct {
    int16_t pcm[512];  // ~32ms at 16kHz
    size_t n_samples;
} audio_chunk_t;

// Audio data read function for recording
esp_err_t audio_manager_read(int16_t *buffer, size_t max_bytes, size_t *bytes_read);

// Add to audio_manager_t struct or create new functions
esp_err_t audio_manager_enable_recording_mode(bool enable);
esp_err_t audio_manager_set_recording_config(const recording_config_t *config);
```

- [ ] **Step 6: Modify audio_manager.c to support recording mode**

```c
// Add to components/audio_manager/src/audio_manager.c
#include <string.h>
static bool recording_mode_enabled = false;
static QueueHandle_t audio_data_queue = NULL;

esp_err_t audio_manager_read(int16_t *buffer, size_t max_bytes, size_t *bytes_read) {
    // Read from internal audio pipeline
    // This bridges the chunk callback model to a blocking read interface
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

esp_err_t audio_manager_enable_recording_mode(bool enable) {
    recording_mode_enabled = enable;
    ESP_LOGI(TAG, "Recording mode %s", enable ? "enabled" : "disabled");
    return ESP_OK;
}

esp_err_t audio_manager_set_recording_config(const recording_config_t *config) {
    return audio_recording_init(config);
}
```

- [ ] **Step 7: Run full build test**

Run: `idf.py build`
Expected: Successful build with extended audio manager

- [ ] **Step 8: Commit changes**

```bash
git add components/audio_manager/
git commit -m "feat: extend audio_manager for continuous recording"
```

### Task 3: Implement Relaxed Sound Detection

**Files:**
- Create: `components/parrot_core/include/sound_detection.h`
- Create: `components/parrot_core/src/sound_detection.c`
- Modify: `components/parrot_core/include/parrot_core.h:1-30`
- Modify: `components/parrot_core/src/parrot_core.c:1-150`

- [ ] **Step 1: Create sound_detection.h**

```c
// components/parrot_core/include/sound_detection.h
#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float rms_threshold_db;      // -40 dBFS default
    float frequency_min_hz;      // 1000 Hz
    float frequency_max_hz;      // 8000 Hz
    uint32_t sample_rate;        // 16000 Hz default
    uint32_t hold_time_ms;       // 100 ms minimum
    uint32_t cooldown_ms;        // 500 ms between detections
} detection_config_t;

typedef void (*detection_callback_t)(float rms_db, float peak_freq, uint32_t timestamp_ms);

esp_err_t sound_detection_init(const detection_config_t *config);
esp_err_t sound_detection_start(detection_callback_t callback);
esp_err_t sound_detection_stop(void);
esp_err_t sound_detection_process(const int16_t *samples, size_t count, uint32_t timestamp_ms);
bool sound_detection_is_active(void);
```

- [ ] **Step 2: Run header verification**

Run: `idf.py build`
Expected: No compilation errors

- [ ] **Step 3: Create sound_detection.c with FFT-based detection**

```c
// components/parrot_core/src/sound_detection.c
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "sound_detection.h"
#define _USE_MATH_DEFINES
#include <math.h>

// Helper function to handle timer wrap-around
static inline uint32_t time_diff_ms(uint32_t newer, uint32_t older) {
    if (newer >= older) {
        return newer - older;
    } else {
        // Handle wrap-around (occurs every ~49.7 days)
        return (UINT32_MAX - older) + newer + 1;
    }
}

#define TAG "SOUND_DETECT"
#define FFT_SIZE 256

static detection_config_t config = {0};
static detection_callback_t callback = NULL;
static bool is_active = false;
static uint32_t last_detection_ms = 0;
static uint32_t detection_start_ms = 0;
static bool detection_ongoing = false;
static float fft_real[FFT_SIZE];
static float fft_imag[FFT_SIZE];

// TEMPORARY: Simple O(N²) DFT for Phase 1.
// Known limitation: too slow for real-time detection at high sample rates.
// REPLACE with ESP-DSP's optimized FFT (esp_dsp.h) in production.
static void compute_fft(const int16_t *samples) {
    // Copy samples to real part, zero imaginary part
    for (int i = 0; i < FFT_SIZE; i++) {
        fft_real[i] = i < FFT_SIZE ? (float)samples[i] : 0.0f;
        fft_imag[i] = 0.0f;
    }
    
    // Simple DFT (replace with optimized ESP-DSP FFT in production)
    for (int k = 0; k < FFT_SIZE; k++) {
        float real_sum = 0;
        float imag_sum = 0;
        
        for (int n = 0; n < FFT_SIZE; n++) {
            float angle = 2 * M_PI * k * n / FFT_SIZE;
            real_sum += fft_real[n] * cosf(angle) + fft_imag[n] * sinf(angle);
            imag_sum += fft_imag[n] * cosf(angle) - fft_real[n] * sinf(angle);
        }
        
        fft_real[k] = real_sum;
        fft_imag[k] = imag_sum;
    }
}

static float find_peak_frequency(void) {
    float max_magnitude = 0;
    int peak_bin = 0;
    
    for (int i = 0; i < FFT_SIZE / 2; i++) {
        float magnitude = sqrtf(fft_real[i] * fft_real[i] + fft_imag[i] * fft_imag[i]);
        if (magnitude > max_magnitude) {
            max_magnitude = magnitude;
            peak_bin = i;
        }
    }
    
    // Use config.sample_rate instead of hardcoded SAMPLE_RATE
    float peak_freq = (float)peak_bin * config.sample_rate / FFT_SIZE;
    return peak_freq;
}

static float calculate_rms_db(const int16_t *samples, size_t count) {
    int64_t sum = 0;
    for (size_t i = 0; i < count; i++) {
        int32_t sample = samples[i];
        sum += sample * sample;
    }
    
    float rms = sqrtf((float)sum / count);
    if (rms <= 0) return -100.0f;
    return 20.0f * log10f(rms / 32768.0f);
}

esp_err_t sound_detection_init(const detection_config_t *cfg) {
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Validate configuration
    if (cfg->frequency_min_hz >= cfg->frequency_max_hz) {
        ESP_LOGE(TAG, "Invalid frequency range: min=%.0f Hz, max=%.0f Hz",
                 cfg->frequency_min_hz, cfg->frequency_max_hz);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (cfg->sample_rate == 0) {
        ESP_LOGE(TAG, "Invalid sample rate: %u Hz", cfg->sample_rate);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (cfg->frequency_max_hz > cfg->sample_rate / 2) {
        ESP_LOGW(TAG, "Frequency max (%.0f Hz) exceeds Nyquist limit (%.0f Hz)",
                 cfg->frequency_max_hz, cfg->sample_rate / 2.0f);
    }
    
    config = *cfg;
    ESP_LOGI(TAG, "Sound detection initialized: threshold=%.1f dB, freq=[%.0f-%.0f] Hz, sample_rate=%u Hz",
             config.rms_threshold_db, config.frequency_min_hz, config.frequency_max_hz, config.sample_rate);
    
    return ESP_OK;
}

esp_err_t sound_detection_start(detection_callback_t cb) {
    callback = cb;
    is_active = true;
    detection_ongoing = false;
    ESP_LOGI(TAG, "Sound detection started");
    return ESP_OK;
}

esp_err_t sound_detection_stop(void) {
    is_active = false;
    callback = NULL;
    detection_ongoing = false;
    ESP_LOGI(TAG, "Sound detection stopped");
    return ESP_OK;
}

esp_err_t sound_detection_process(const int16_t *samples, size_t count, uint32_t timestamp_ms) {
    if (!is_active || !callback || count < FFT_SIZE) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Check cooldown
    if (time_diff_ms(timestamp_ms, last_detection_ms) < config.cooldown_ms) {
        return ESP_OK;
    }
    
    // Calculate RMS level
    float rms_db = calculate_rms_db(samples, count);
    
    if (rms_db > config.rms_threshold_db) {
        // Compute FFT to get frequency information
        compute_fft(samples);
        float peak_freq = find_peak_frequency();
        
        // Check if frequency is in parrot vocal range
        if (peak_freq >= config.frequency_min_hz && peak_freq <= config.frequency_max_hz) {
            // Start or continue detection
            if (!detection_ongoing) {
                detection_start_ms = timestamp_ms;
                detection_ongoing = true;
                ESP_LOGD(TAG, "Detection started: %.1f dB @ %.0f Hz", rms_db, peak_freq);
            }
            
            // Check if detection has lasted long enough
            if (time_diff_ms(timestamp_ms, detection_start_ms) >= config.hold_time_ms) {
                last_detection_ms = timestamp_ms;
                callback(rms_db, peak_freq, timestamp_ms);
                detection_ongoing = false; // Reset for next detection
                
                ESP_LOGD(TAG, "Detection triggered: %.1f dB @ %.0f Hz (held %ums)", 
                         rms_db, peak_freq, time_diff_ms(timestamp_ms, detection_start_ms));
                return ESP_OK;
            }
        } else {
            // Frequency out of range, reset detection
            detection_ongoing = false;
        }
    } else {
        // Sound below threshold, reset detection
        detection_ongoing = false;
    }
    
    return ESP_OK;
}

bool sound_detection_is_active(void) {
    return is_active;
}
```

- [ ] **Step 4: Run compilation test**

Run: `idf.py build`
Expected: Successful build with sound detection

- [ ] **Step 5: Integrate detection into parrot_core**

```c
// Modify components/parrot_core/src/parrot_core.c
#include "sound_detection.h"
#include "agent_core.h"

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

esp_err_t parrot_core_init(void) {
    // Existing initialization...
    
    // Initialize sound detection with relaxed settings
    detection_config_t det_config = {
        .rms_threshold_db = -40.0f,  // Relaxed threshold
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
}
```

- [ ] **Step 6: Run full build test**

Run: `idf.py build`
Expected: Successful build with integrated detection

- [ ] **Step 7: Commit changes**

```bash
git add components/parrot_core/
git commit -m "feat: add relaxed sound detection for recording"
```

### Task 4: Add Kconfig Configuration Options

**Files:**
- Modify: `main/Kconfig.projbuild`
- Modify: `sdkconfig.defaults`

- [ ] **Step 1: Add recording configuration to Kconfig**

```kconfig
# Add to main/Kconfig.projbuild
menu "Parrot Recording Configuration"

    config PARROT_RECORDING_ENABLE
        bool "Enable parrot sound recording"
        default y
        help
            Enable the parrot sound recording feature with WiFi streaming.
    
    config PARROT_RECORDING_SILENCE_TIMEOUT_MS
        int "Silence detection timeout (ms)"
        default 5000
        range 1000 30000
        help
            Stop recording after this many milliseconds of silence.
    
    config PARROT_RECORDING_MIN_DURATION_MS
        int "Minimum recording duration (ms)"
        default 2000
        range 500 10000
        help
            Minimum length of a recording session.
    
    config PARROT_RECORDING_SAMPLE_RATE
        int "Audio sample rate (Hz)"
        default 16000
        range 8000 48000
        help
            Sample rate for audio recording and streaming.
    
    config PARROT_RECORDING_DETECTION_THRESHOLD_DB
        int "Sound detection threshold (dBFS)"
        default -40
        range -60 -20
        help
            RMS threshold for sound detection (lower = more sensitive).

    config PARROT_RECORDING_BUFFER_SIZE_MS
        int "Audio buffer size (ms)"
        default 1000
        range 100 5000
        help
            Size of audio buffer for network fluctuation handling (used in Phase 2).

    config PARROT_RECORDING_WS_PORT
        int "WebSocket server port"
        default 8080
        range 1024 65535
        help
            Port for WebSocket audio streaming server (used in Phase 2).

endmenu
```

- [ ] **Step 2: Update sdkconfig.defaults with recording defaults**

```bash
# Add to sdkconfig.defaults
CONFIG_PARROT_RECORDING_ENABLE=y
CONFIG_PARROT_RECORDING_SILENCE_TIMEOUT_MS=5000
CONFIG_PARROT_RECORDING_MIN_DURATION_MS=2000
CONFIG_PARROT_RECORDING_SAMPLE_RATE=16000
CONFIG_PARROT_RECORDING_DETECTION_THRESHOLD_DB=-40
CONFIG_PARROT_RECORDING_BUFFER_SIZE_MS=1000
CONFIG_PARROT_RECORDING_WS_PORT=8080
```

- [ ] **Step 3: Run configuration test**

Run: `idf.py menuconfig`
Expected: Recording configuration menu appears

- [ ] **Step 4: Commit configuration changes**

```bash
git add main/Kconfig.projbuild sdkconfig.defaults
git commit -m "feat: add Kconfig options for parrot recording"
```

---

## Chunk 2: Integration and Testing

### Task 5: Integrate Components and Add Basic Tests

**Files:**
- Create: `test/test_agent_core.c`
- Create: `test/test_audio_recording.c`
- Modify: `main/main.c:60-80` - Add integration code

- [ ] **Step 1: Create agent_core test**

```c
// test/test_agent_core.c
#include <stdio.h>
#include "unity.h"
#include "agent_core.h"
#include "agent_events.h"

static int event_count = 0;

static void test_event_callback(const agent_event_t *evt) {
    event_count++;
    TEST_ASSERT_EQUAL(AGENT_EVT_AUDIO_DETECTED, evt->type);
}

TEST_CASE("Agent Core Event Queue", "[agent_core]") {
    TEST_ASSERT_EQUAL(ESP_OK, agent_core_init());
    TEST_ASSERT_EQUAL(ESP_OK, agent_core_register_callback(test_event_callback));
    TEST_ASSERT_EQUAL(ESP_OK, agent_core_start());
    
    agent_event_t evt = {
        .type = AGENT_EVT_AUDIO_DETECTED,
        .timestamp_ms = 1000,
        .audio_detected = {
            .rms_level = -35.5f,
            .peak_frequency = 2500.0f
        }
    };
    
    event_count = 0;
    TEST_ASSERT_EQUAL(ESP_OK, agent_core_post_event(&evt));
    
    // Give task time to process
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL(1, event_count);
    
    TEST_ASSERT_EQUAL(ESP_OK, agent_core_stop());
}
```

- [ ] **Step 2: Run agent_core test**

Run: `idf.py build && idf.py -p PORT flash monitor`
Expected: Test compiles and runs (may need test runner setup)

- [ ] **Step 3: Create audio_recording test**

```c
// test/test_audio_recording.c
#include <stdio.h>
#include "unity.h"
#include "audio_recording.h"

static int chunk_count = 0;
static bool recording_state = false;

static void test_chunk_callback(const int16_t *samples, size_t count, uint32_t timestamp_ms) {
    chunk_count++;
    TEST_ASSERT_TRUE(count > 0);
}

static void test_state_callback(bool is_recording, uint32_t duration_ms) {
    recording_state = is_recording;
}

TEST_CASE("Audio Recording Basic", "[audio_recording]") {
    recording_config_t config = {
        .sample_rate = 16000,
        .silence_timeout_ms = 5000,
        .min_duration_ms = 2000,
        .detection_threshold_db = -40.0f,
        .chunk_callback = test_chunk_callback,
        .state_callback = test_state_callback
    };
    
    TEST_ASSERT_EQUAL(ESP_OK, audio_recording_init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, audio_recording_start());
    
    // Recording should not be active until sound detected
    TEST_ASSERT_FALSE(audio_recording_is_active());
    
    TEST_ASSERT_EQUAL(ESP_OK, audio_recording_stop());
}
```

- [ ] **Step 4: Run audio_recording test**

Run: `idf.py build`
Expected: Test compiles successfully

- [ ] **Step 5: Update main.c for full integration**

```c
// Add to main/main.c after existing initialization
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
            // Trigger recording start via audio manager
            break;
        default:
            break;
    }
}

void app_main(void) {
    // ... existing initialization ...
    
    // Set up recording configuration
    recording_config_t rec_config = {
        .sample_rate = CONFIG_PARROT_RECORDING_SAMPLE_RATE,
        .silence_timeout_ms = CONFIG_PARROT_RECORDING_SILENCE_TIMEOUT_MS,
        .min_duration_ms = CONFIG_PARROT_RECORDING_MIN_DURATION_MS,
        .detection_threshold_db = CONFIG_PARROT_RECORDING_DETECTION_THRESHOLD_DB,
        .chunk_callback = NULL, // Will be set by transport layer
        .state_callback = NULL
    };
    
    ESP_ERROR_CHECK(audio_manager_set_recording_config(&rec_config));
    
    // Register agent core callback
    ESP_ERROR_CHECK(agent_core_register_callback(agent_event_handler));
    
    // Start sound detection
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
    
    // ... rest of startup ...
}
```

- [ ] **Step 6: Run full integration build**

Run: `idf.py build`
Expected: Successful build with all components integrated

- [ ] **Step 7: Flash and test on hardware**

Run: `idf.py -p PORT flash monitor`
Expected: Device boots, logs show agent core and recording initialization

- [ ] **Step 8: Commit integration changes**

```bash
git add main/main.c test/
git commit -m "feat: integrate recording components and add tests"
```

---

## Chunk 3: UI Foundation for Recording

### Task 6: Add Recording Screen to UI

**Files:**
- Modify: `components/ui/include/ui/ui_manager.h:1-40`
- Modify: `components/ui/src/ui_manager.c:1-100`
- Create: `components/ui/src/ui_screen_recording.c`
- Create: `components/ui/include/ui/ui_screen_recording.h`

- [ ] **Step 1: Add recording screen enum**

```c
// Modify components/ui/include/ui/ui_manager.h
typedef enum {
    UI_SCREEN_BOOT = 0,
    UI_SCREEN_MAIN,
    UI_SCREEN_RECORDING,  // New recording screen
    UI_SCREEN_SETTINGS,
    UI_SCREEN_STATUS,
    UI_SCREEN_DEBUG,
    UI_SCREEN_MAX,
} ui_screen_id_t;
```

- [ ] **Step 2: Create recording screen header**

```c
// components/ui/include/ui/ui_screen_recording.h
#pragma once
#include "ui/ui_manager.h"

esp_err_t ui_screen_recording_create(lv_obj_t *parent);
esp_err_t ui_screen_recording_show(void);
esp_err_t ui_screen_recording_hide(void);
esp_err_t ui_screen_recording_update_level(float level_db);
esp_err_t ui_screen_recording_update_duration(uint32_t duration_ms);
esp_err_t ui_screen_recording_update_state(bool is_recording, bool is_streaming);
```

- [ ] **Step 3: Create recording screen implementation**

```c
// components/ui/src/ui_screen_recording.c
#include "ui_screen_recording.h"
#include "lvgl.h"
#include "esp_log.h"

#define TAG "UI_RECORDING"

static lv_obj_t *screen = NULL;
static lv_obj_t *level_bar = NULL;
static lv_obj_t *duration_label = NULL;
static lv_obj_t *state_label = NULL;
static lv_obj_t *level_label = NULL;

static void on_back_click(lv_event_t *e) {
    ui_manager_show(UI_SCREEN_MAIN, UI_ANIM_SLIDE_RIGHT);
}

esp_err_t ui_screen_recording_create(lv_obj_t *parent) {
    screen = lv_obj_create(parent);
    lv_obj_set_size(screen, 320, 240);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    
    // Title
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Parrot Recording");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    
    // Audio level bar
    level_bar = lv_bar_create(screen);
    lv_obj_set_size(level_bar, 280, 30);
    lv_bar_set_range(level_bar, -60, 0);
    lv_bar_set_value(level_bar, -60, LV_ANIM_OFF);
    lv_obj_align(level_bar, LV_ALIGN_TOP_MID, 0, 70);
    
    // Level label
    level_label = lv_label_create(screen);
    lv_label_set_text(level_label, "-60 dB");
    lv_obj_set_style_text_color(level_label, lv_color_white(), 0);
    lv_obj_align_to(level_label, level_bar, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    
    // Duration label
    duration_label = lv_label_create(screen);
    lv_label_set_text(duration_label, "Duration: 0s");
    lv_obj_set_style_text_color(duration_label, lv_color_white(), 0);
    lv_obj_align(duration_label, LV_ALIGN_TOP_MID, 0, 130);
    
    // State label
    state_label = lv_label_create(screen);
    lv_label_set_text(state_label, "Status: Idle");
    lv_obj_set_style_text_color(state_label, lv_color_hex(0x808080), 0);
    lv_obj_align(state_label, LV_ALIGN_TOP_MID, 0, 160);
    
    // Back button
    lv_obj_t *back_btn = lv_btn_create(screen);
    lv_obj_set_size(back_btn, 100, 40);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 20, -20);
    lv_obj_add_event_cb(back_btn, on_back_click, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);
    
    return ESP_OK;
}

esp_err_t ui_screen_recording_show(void) {
    if (!screen) {
        return ESP_FAIL;
    }
    
    lv_scr_load(screen);
    return ESP_OK;
}

esp_err_t ui_screen_recording_hide(void) {
    return ESP_OK;
}

esp_err_t ui_screen_recording_update_level(float level_db) {
    if (!level_bar || !level_label) {
        return ESP_FAIL;
    }
    
    // Clamp level to valid range
    if (level_db < -60) level_db = -60;
    if (level_db > 0) level_db = 0;
    
    lv_bar_set_value(level_bar, (int32_t)level_db, LV_ANIM_ON);
    
    // Update label
    char text[32];
    snprintf(text, sizeof(text), "%.1f dB", level_db);
    lv_label_set_text(level_label, text);
    
    // Change color based on level
    if (level_db > -20) {
        lv_obj_set_style_bg_color(level_bar, lv_color_red(), LV_PART_INDICATOR);
    } else if (level_db > -40) {
        lv_obj_set_style_bg_color(level_bar, lv_color_yellow(), LV_PART_INDICATOR);
    } else {
        lv_obj_set_style_bg_color(level_bar, lv_color_green(), LV_PART_INDICATOR);
    }
    
    return ESP_OK;
}

esp_err_t ui_screen_recording_update_duration(uint32_t duration_ms) {
    if (!duration_label) {
        return ESP_FAIL;
    }
    
    char text[32];
    snprintf(text, sizeof(text), "Duration: %ds", duration_ms / 1000);
    lv_label_set_text(duration_label, text);
    
    return ESP_OK;
}

esp_err_t ui_screen_recording_update_state(bool is_recording, bool is_streaming) {
    if (!state_label) {
        return ESP_FAIL;
    }
    
    char text[32];
    if (is_recording) {
        if (is_streaming) {
            snprintf(text, sizeof(text), "Status: Recording & Streaming");
            lv_obj_set_style_text_color(state_label, lv_color_green(), 0);
        } else {
            snprintf(text, sizeof(text), "Status: Recording (No Stream)");
            lv_obj_set_style_text_color(state_label, lv_color_yellow(), 0);
        }
    } else {
        snprintf(text, sizeof(text), "Status: Idle");
        lv_obj_set_style_text_color(state_label, lv_color_hex(0x808080), 0);
    }
    
    lv_label_set_text(state_label, text);
    return ESP_OK;
}
```

- [ ] **Step 4: Update ui_manager to support recording screen**

```c
// Modify components/ui/src/ui_manager.c
#include "ui_screen_recording.h"

static esp_err_t ui_screen_create(ui_screen_id_t screen_id, lv_obj_t *parent) {
    switch (screen_id) {
        case UI_SCREEN_RECORDING:
            return ui_screen_recording_create(parent);
        // ... existing cases ...
    }
    return ESP_OK;
}
```

- [ ] **Step 5: Update main menu to include recording option**

```c
// Need to modify main menu screen (ui_screen_main.c if exists)
// Add button for "Parrot Recording" that navigates to UI_SCREEN_RECORDING
```

- [ ] **Step 6: Run UI build test**

Run: `idf.py build`
Expected: Successful build with new UI screen

- [ ] **Step 7: Test UI on device**

Run: `idf.py -p PORT flash monitor`
Expected: Recording screen accessible from main menu

- [ ] **Step 8: Commit UI changes**

```bash
git add components/ui/
git commit -m "feat: add recording screen to UI"
```

---

## Verification

### Final Integration Test

- [ ] **Step 1: Build complete firmware**

Run: `idf.py build`
Expected: Successful build with all Phase 1 components

- [ ] **Step 2: Flash to device**

Run: `idf.py -p PORT flash`
Expected: Successful flash

- [ ] **Step 3: Monitor startup logs**

Run: `idf.py -p PORT monitor`
Expected: Logs show:
- Agent core initialization
- Audio recording configuration
- Sound detection initialization
- UI startup with recording screen available

- [ ] **Step 4: Test navigation**

Manual test: Navigate to recording screen from main menu
Expected: Recording screen shows audio level meter and status

- [ ] **Step 5: Test sound detection**

Manual test: Make sounds near microphone
Expected: Logs show detection events (if above threshold)

- [ ] **Step 6: Commit final verification**

```bash
git commit -m "chore: Phase 1 implementation complete"
```

---

## Next Phase Dependencies

Phase 1 provides the foundation for:
1. **Phase 2: Transport & Protocol** - WebSocket streaming implementation
2. **Phase 3: UI Integration** - Complete recording controls and settings
3. **Phase 4: PC Receiver** - Python application for data reception
4. **Phase 5: Polish & Optimization** - Performance tuning and error handling

---

**Plan complete and saved to `docs/superpowers/plans/2026-04-23-parrot-recording-phase1.md`. Ready to execute?**
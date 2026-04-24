# Parrot Sound Recording Feature Specification

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add parrot sound recording functionality to ESP32-S3-BOX-3 with real-time WiFi streaming to PC for training data collection.

**Architecture:** Adapt smart-buddy's event-driven core with single queue for all inputs. Extend audio manager for continuous recording with relaxed detection. Use plugin-based transport layer for WebSocket audio streaming. Add four-screen UI navigation.

**Tech Stack:** ESP-IDF 5.1+, LVGL 8.3, FreeRTOS, WebSocket, Python 3.9+ (PC side)

---

## 1. Requirements

### 1.1 Functional Requirements
1. **UI Navigation**: Four-screen menu:
   - Parrot Sound Recognition (existing)
   - Parrot Sound Recording (new)
   - Settings (enhanced)
   - Debugging (existing)

2. **Recording Core**:
   - Continuous audio capture from microphone array
   - Relaxed detection logic to capture all parrot-like sounds
   - Configurable silence timeout (default: 5s)
   - Minimum recording duration (default: 2s)
   - Automatic start/stop based on detection

3. **Audio Streaming**:
   - Real-time streaming to PC over WiFi (WebSocket)
   - Frame-based transmission with metadata headers
   - Buffering for network fluctuations
   - No local file storage on ESP32

4. **PC Receiver**:
   - Python-based receiver application
   - Data reassembly from intermittent calls into reasonable-length files
   - Real-time visualization of audio levels
   - File management and organization

5. **Configuration**:
   - Silence detection threshold (menuconfig + runtime)
   - Minimum recording duration (menuconfig + runtime)
   - WiFi credentials (menuconfig + runtime UI)
   - Audio gain and sample rate settings

### 1.2 Non-Functional Requirements
1. **Performance**: < 100ms latency from detection to streaming start
2. **Reliability**: Handle network disconnections with buffering
3. **Power**: Optimize for continuous operation (battery-powered scenarios)
4. **Usability**: Simple touchscreen interface with clear status indicators

## 2. Architecture

### 2.1 High-Level Architecture
```
┌─────────────────────────────────────────────────────────────┐
│                       ESP32-S3-BOX-3                        │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│  │   UI     │  │  Audio   │  │  Event   │  │Transport │   │
│  │ Manager  │◄─┤ Manager  │◄─┤   Core   │◄─┤  Layer   │   │
│  │(4 screens)│  │(record/ │  │(single   │  │(WebSocket)│   │
│  └──────────┘  │ stream)  │  │  queue)  │  └─────┬────┘   │
│        │       └──────────┘  └──────────┘        │        │
│        ▼                │               │         │        │
│  ┌──────────┐    ┌──────▼──────┐ ┌─────▼─────┐  │        │
│  │  LVGL    │    │Sound Detection││Protocol   │  │        │
│  │Renderer  │    │ (relaxed)    ││(binary+JSON)│ │        │
│  └──────────┘    └──────────────┘└────────────┘ │        │
│        │               │               │         │        │
│  ┌─────▼─────┐  ┌─────▼─────┐  ┌──────▼──────┐ │        │
│  │Touch Input│  │Mic Array  │  │Config Store │ │        │
│  └───────────┘  └───────────┘  └─────────────┘ │        │
│                                                 │        │
└─────────────────────────────────────────────────┼────────┘
                                                  │ WiFi
                                                  ▼
┌─────────────────────────────────────────────────────────────┐
│                         PC Receiver                         │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│  │WebSocket │  │ Data     │  │ File     │  │Visualization│
│  │ Client   │─►│Reassembly│─►│ Manager  │─►│ & Monitor │   │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘   │
│                                                            │
│  ┌────────────────────────────────────────────────────┐   │
│  │                Training Data Output                │   │
│  │  • WAV files (16kHz, 16-bit mono)                 │   │
│  │  • Timestamped metadata                           │   │
│  │  • Organized by date/time                         │   │
│  └────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 Component Details

#### 2.2.1 Event System (`components/agent_core/`)
- Extend `agent_events.h` with audio-specific events:
  ```c
  AGENT_EVT_AUDIO_DETECTED,      // Sound above threshold detected
  AGENT_EVT_AUDIO_RECORD_START,  // Start recording session
  AGENT_EVT_AUDIO_RECORD_STOP,   // Stop recording (silence timeout)
  AGENT_EVT_AUDIO_STREAM_DATA,   // Audio data ready for streaming
  AGENT_EVT_AUDIO_STREAM_ERROR   // Streaming error
  ```
- Maintain single FreeRTOS queue architecture
- Audio events trigger state machine transitions

#### 2.2.2 Audio Manager (`components/audio_manager/`)
- Extend existing audio manager for continuous recording
- Dual-buffer pipeline:
  - Buffer A: Recording from microphone
  - Buffer B: Processing/streaming
- Sound detection with configurable thresholds:
  - RMS threshold: -40dBFS (relaxed for data collection)
  - Frequency range: 1kHz-8kHz (parrot vocal range)
  - Hold time: 100ms minimum
- Silence detection:
  - Configurable timeout (1-30s, default 5s)
  - Minimum recording duration (0.5-10s, default 2s)

#### 2.2.3 Transport Layer (`components/transport/`)
- Add WebSocket transport for audio streaming
- Extend `transport_t` interface with audio-specific methods:
  ```c
  esp_err_t (*stream_audio)(transport_t *t, const uint8_t *data, size_t len, uint32_t timestamp);
  esp_err_t (*stream_control)(transport_t *t, audio_control_msg_t *msg);
  ```
- Buffering mechanism:
  - Ring buffer for network fluctuations
  - Drop oldest data if buffer overflows
  - Automatic reconnection on network loss

#### 2.2.4 Protocol (`components/protocol/`)
- Binary protocol for efficient audio transmission:
  ```
  [Header: 16 bytes]
    - Magic: 0x50415252 ("PARR")
    - Version: 0x01
    - Type: 0x01 (audio data), 0x02 (control)
    - Timestamp: uint32_t (ms since start)
    - Sequence: uint32_t
    - Length: uint32_t (data length)
    - Checksum: uint16_t (CRC16)
  
  [Data: variable length]
    - Audio: PCM 16-bit, 16kHz mono
    - Control: JSON metadata
  ```
- Control messages (JSON):
  ```json
  {
    "type": "recording_start",
    "timestamp": 1234567890,
    "duration_ms": 2000,
    "sample_rate": 16000,
    "channels": 1
  }
  ```

#### 2.2.5 UI Framework (`components/ui/`)
- Add new screens:
  1. **Recording Screen**:
     - Audio level visualization (VU meter)
     - Recording duration timer
     - Network status indicator
     - File count/statistics
     - Start/Stop manual controls
  
  2. **Enhanced Settings Screen**:
     - Silence timeout slider (1-30s)
     - Minimum duration slider (0.5-10s)
     - Audio gain control (-12dB to +12dB)
     - WiFi configuration
     - Detection sensitivity
  
  3. **Main Menu** (modified):
     - Four large buttons for navigation
     - Current mode highlighting

#### 2.2.6 PC Receiver (`tools/parrot-receiver/`)
- Python application based on `tools/autotune/` foundation
- Features:
  - WebSocket client with reconnection logic
  - Data reassembly:
    - Combine intermittent calls into 5-30s files
    - Remove silences shorter than configurable threshold
    - Add 500ms padding before/after calls
  - File output:
    - WAV format, 16kHz, 16-bit mono
    - Timestamped filenames: `parrot_YYYYMMDD_HHMMSS.wav`
    - Metadata JSON sidecar files
  - Real-time monitoring:
    - Audio waveform display
    - Spectrum analyzer
    - Detection statistics

## 3. Configuration

### 3.1 Kconfig Options
```kconfig
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
    
    config PARROT_RECORDING_BUFFER_SIZE_MS
        int "Audio buffer size (ms)"
        default 1000
        range 100 5000
        help
            Size of audio buffer for network fluctuation handling.
    
    config PARROT_RECORDING_WS_PORT
        int "WebSocket server port"
        default 8080
        range 1024 65535
        help
            Port for WebSocket audio streaming server.
    
    config PARROT_RECORDING_DETECTION_THRESHOLD_DB
        int "Sound detection threshold (dBFS)"
        default -40
        range -60 -20
        help
            RMS threshold for sound detection (lower = more sensitive).

endmenu
```

### 3.2 Runtime Configuration
- Settings stored in NVS
- UI-configurable parameters override Kconfig defaults
- WiFi credentials stored securely

## 4. Data Flow

### 4.1 Recording Session Flow
```
1. Audio input → Mic Array → ES7210 codec
2. Audio Manager → Continuous capture to buffer
3. Sound Detection → RMS calculation + frequency analysis
4. Detection Event → AGENT_EVT_AUDIO_DETECTED
5. Agent Core → Start recording timer
6. Audio Manager → Begin streaming to transport
7. Transport Layer → WebSocket transmission to PC
8. Silence Detection → Timeout after 5s silence
9. Agent Core → AGENT_EVT_AUDIO_RECORD_STOP
10. Transport Layer → Send end-of-recording control message
```

### 4.2 Error Handling
- Network disconnection: Buffer audio, attempt reconnection
- Buffer overflow: Drop oldest data, log warning
- Codec failure: Reset audio pipeline, notify UI
- Storage error: Continue streaming, skip local save

## 5. Testing Strategy

### 5.1 Unit Tests
- Audio detection algorithms
- Buffer management
- Protocol encoding/decoding
- Transport layer reliability

### 5.2 Integration Tests
- End-to-end recording → streaming → file save
- Network disruption recovery
- Configuration persistence
- UI navigation and state management

### 5.3 Performance Tests
- Latency: detection to streaming start
- Memory usage during continuous operation
- Network bandwidth utilization
- Battery impact estimation

## 6. Dependencies

### 6.1 ESP32 Components
- `esp-box-3` (managed component)
- `esp_websocket_client`
- `esp_http_server`
- `lvgl` (already in project)
- `driver/i2s`
- `driver/es7210`, `driver/es8311`

### 6.2 PC Side Dependencies
- Python 3.9+
- `websockets` library
- `numpy`, `scipy`
- `soundfile`, `pyaudio`
- `pyqt5` or `tkinter` for UI

## 7. Implementation Phases

### Phase 1: Core Infrastructure
- Extend event system with audio events
- Enhance audio manager for continuous recording
- Implement basic sound detection

### Phase 2: Transport & Protocol
- Add WebSocket transport
- Implement binary audio protocol
- Create buffering mechanism

### Phase 3: UI Integration
- Add recording screen
- Enhance settings screen
- Update main menu navigation

### Phase 4: PC Receiver
- Develop Python receiver application
- Implement data reassembly logic
- Add monitoring UI

### Phase 5: Polish & Optimization
- Performance tuning
- Error handling improvements
- Documentation and examples

## 8. Success Metrics

1. **Functionality**: All four menu options work correctly
2. **Reliability**: 99% successful recording sessions
3. **Latency**: < 100ms detection to streaming
4. **Usability**: Intuitive touchscreen interface
5. **Data Quality**: Clean WAV files suitable for ML training

## 9. Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Network instability | Data loss | Buffering + reconnection logic |
| Memory constraints | Crashes | Optimized buffer sizes, PSRAM usage |
| Power consumption | Battery life | Sleep modes, efficient processing |
| Audio quality | Poor training data | Proper gain calibration, noise filtering |
| UI complexity | User confusion | Simple design, clear indicators |

---

**Next Steps**: Create implementation plan using `writing-plans` skill.
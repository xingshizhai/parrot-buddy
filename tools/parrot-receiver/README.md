# Parrot Sound Recording Receiver

Python WebSocket client that receives audio data streamed from ESP32-S3-BOX-3 and saves it as WAV files.

## Installation

```bash
pip install -r requirements.txt
```

## Usage

```bash
python receiver.py --host 192.168.1.101 --port 8000 --output ./recordings
```

### Arguments

| Argument | Default | Description |
|----------|---------|-------------|
| `--host` | 192.168.1.101 | ESP32 server IP address |
| `--port` | 8000 | WebSocket server port |
| `--output` | ./recordings | Directory to save WAV files |
| `--silence-db` | -50.0 | Silence threshold in dB |
| `--min-silence-ms` | 3000 | Min silence duration before saving (ms) |
| `--min-recording-ms` | 2000 | Minimum recording duration (ms) |

## Output Files

Each recording session produces:
- `parrot_YYYYMMDD_HHMMSS.wav` - 16kHz, 16-bit mono WAV file
- `parrot_YYYYMMDD_HHMMSS.json` - Metadata (duration, frame count, timestamp, etc.)

## Protocol

The receiver expects a binary WebSocket frame format:
- 21-byte header: magic (0x50415252), version, type, timestamp, sequence, length, CRC16
- PCM 16-bit little-endian audio data

Control messages are sent as JSON text frames.

## ESP32 Menuconfig

Set these in `menuconfig` under `Parrot Buddy > Parrot Recording`:
- `PARROT_RECORDING_WS_SERVER` = `192.168.1.101`
- `PARROT_RECORDING_WS_PORT` = `8000`
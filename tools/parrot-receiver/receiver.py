#!/usr/bin/env python3
"""
Parrot Sound Recording Receiver
WebSocket server that receives audio data from ESP32 and saves WAV files.
"""

import asyncio
import websockets
import struct
import wave
import json
import os
from datetime import datetime
from pathlib import Path
import argparse

PARR_MAGIC = 0x50415252
PARR_VERSION = 0x01
PARR_TYPE_AUDIO = 0x01
PARR_TYPE_CONTROL = 0x02
PARR_HEADER_SIZE = 16


class ParrotHeader:
    magic: int
    version: int
    type: int
    reserved: int
    timestamp_ms: int
    sequence: int
    length: int
    checksum: int

    @staticmethod
    def parse(data: bytes) -> 'ParrotHeader':
        if len(data) < 21:
            raise ValueError(f"Header too short: {len(data)}")
        h = ParrotHeader()
        h.magic = struct.unpack('>I', data[0:4])[0]
        h.version = data[4]
        h.type = data[5]
        h.reserved = data[6]
        h.timestamp_ms = struct.unpack('>I', data[7:11])[0]
        h.sequence = struct.unpack('>I', data[11:15])[0]
        h.length = struct.unpack('>I', data[15:19])[0]
        h.checksum = struct.unpack('>H', data[19:21])[0]
        return h


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    table = [
        0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
        0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
        0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
        0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
        0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
        0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
        0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
        0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
        0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
        0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
        0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
        0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
        0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
        0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
        0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
        0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
        0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
        0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
        0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
        0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
        0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
        0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
        0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
        0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
        0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
        0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
        0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
        0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
        0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
        0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
        0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
        0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
    ]
    for byte in data:
        index = (crc >> 8) ^ byte
        crc = (crc << 8) ^ table[index]
    return crc & 0xFFFF


class AudioSession:
    def __init__(self, output_dir: Path, sample_rate: int = 16000):
        self.output_dir = output_dir
        self.sample_rate = sample_rate
        self.samples: list[int] = []
        self.start_time: datetime | None = None
        self.frame_count = 0
        self.bytes_received = 0

    def add_samples(self, pcm_data: bytes, timestamp_ms: int):
        samples = list(struct.unpack(f'<{len(pcm_data)//2}h', pcm_data))
        self.samples.extend(samples)
        self.bytes_received += len(pcm_data)
        self.frame_count += 1

    def save(self) -> Path | None:
        if len(self.samples) < self.sample_rate // 2:
            return None
        if self.start_time is None:
            self.start_time = datetime.now()
        ts = self.start_time
        filename = ts.strftime("parrot_%Y%m%d_%H%M%S") + ".wav"
        filepath = self.output_dir / filename
        self._write_wav(filepath, self.samples)
        meta = {
            "filename": filename,
            "timestamp": ts.isoformat(),
            "duration_ms": len(self.samples) * 1000 // self.sample_rate,
            "sample_rate": self.sample_rate,
            "channels": 1,
            "bits_per_sample": 16,
            "frame_count": self.frame_count,
            "bytes_received": self.bytes_received
        }
        with open(filepath.with_suffix('.json'), 'w') as f:
            json.dump(meta, f, indent=2)
        return filepath

    def _write_wav(self, filepath: Path, samples: list[int]):
        with wave.open(str(filepath), 'wb') as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)
            wf.setframerate(self.sample_rate)
            wf.writeframes(struct.pack(f'<{len(samples)}h', *samples))

    def is_empty(self) -> bool:
        return len(self.samples) == 0

    def clear(self):
        self.samples.clear()
        self.start_time = None
        self.frame_count = 0
        self.bytes_received = 0


class ParrotReceiverServer:
    def __init__(
        self,
        host: str = "0.0.0.0",
        port: int = 8082,
        output_dir: str = "./recordings",
    ):
        self.host = host
        self.port = port
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.sample_rate = 16000
        self.session: AudioSession | None = None
        self.running = False
        self.total_sessions = 0
        self.total_errors = 0

    async def handle_client(self, ws):
        print(f"Client connected from {ws.remote_address}")
        session = AudioSession(self.output_dir, self.sample_rate)
        buffer = b""

        try:
            async for msg in ws:
                if isinstance(msg, str):
                    print(f"Text message: {msg}")
                    continue

                buffer += msg

                while len(buffer) >= 21:
                    header = ParrotHeader.parse(buffer[:21])
                    if header.magic != PARR_MAGIC:
                        buffer = buffer[1:]
                        self.total_errors += 1
                        continue

                    total_len = 21 + header.length
                    if len(buffer) < total_len:
                        break

                    frame_data = buffer[21:total_len]
                    buffer = buffer[total_len:]

                    if header.type == PARR_TYPE_AUDIO:
                        expected_crc = crc16(frame_data)
                        if expected_crc != header.checksum:
                            print(f"Checksum mismatch: expected {expected_crc:04x}, got {header.checksum:04x}")
                            self.total_errors += 1
                            continue
                        session.add_samples(frame_data, header.timestamp_ms)

                        if len(session.samples) >= self.sample_rate * 10:
                            filepath = session.save()
                            if filepath:
                                self.total_sessions += 1
                                print(f"Saved: {filepath.name} ({len(session.samples)} samples)")

                    elif header.type == PARR_TYPE_CONTROL:
                        try:
                            ctrl = json.loads(frame_data.decode('utf-8'))
                            print(f"Control: {ctrl}")
                        except:
                            pass

        except websockets.exceptions.ConnectionClosed:
            pass
        except Exception as e:
            print(f"Error: {e}")

        if not session.is_empty():
            filepath = session.save()
            if filepath:
                self.total_sessions += 1
                print(f"Saved on disconnect: {filepath.name} ({len(session.samples)} samples)")

    async def start(self):
        self.running = True
        print(f"Parrot Receiver Server starting...")
        print(f"  Listen: {self.host}:{self.port}")
        print(f"  Path: /parrot")
        print(f"  Output: {self.output_dir}")
        print(f"  Sample rate: {self.sample_rate} Hz")

        async with websockets.serve(self.handle_client, self.host, self.port):
            print("Server ready, waiting for connections...")
            await asyncio.Future()

    def stop(self):
        self.running = False
        print(f"Stats: sessions={self.total_sessions}, errors={self.total_errors}")


async def main():
    parser = argparse.ArgumentParser(description="Parrot Sound Recording Receiver (Server)")
    parser.add_argument("--host", default="0.0.0.0", help="Bind host (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=8082, help="WebSocket port (default: 8082)")
    parser.add_argument("--output", default="./recordings", help="Output directory")
    args = parser.parse_args()

    server = ParrotReceiverServer(
        host=args.host,
        port=args.port,
        output_dir=args.output,
    )

    try:
        await server.start()
    except KeyboardInterrupt:
        server.stop()


if __name__ == "__main__":
    asyncio.run(main())

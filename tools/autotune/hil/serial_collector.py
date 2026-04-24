from __future__ import annotations

import queue
import re
import threading
import time
from dataclasses import dataclass
from typing import List


TRIGGER_REGEX = re.compile(r"reply voice=(?P<voice>\d+) rms=(?P<rms>\d+)")


@dataclass
class TriggerEvent:
    sample_name: str
    latency_ms: float
    raw_line: str


class SerialCollector:
    def __init__(self, port: str, baudrate: int = 115200) -> None:
        self._port = port
        self._baudrate = baudrate
        self._thread: threading.Thread | None = None
        self._stop = threading.Event()
        self._lines: "queue.Queue[tuple[float, str]]" = queue.Queue()
        self._serial = None

    def start_capture(self) -> None:
        try:
            import serial  # type: ignore
        except Exception as exc:
            raise RuntimeError("pyserial is required for SerialCollector") from exc

        self._serial = serial.Serial(self._port, self._baudrate, timeout=0.1)
        self._stop.clear()
        self._thread = threading.Thread(target=self._reader_loop, daemon=True)
        self._thread.start()

    def stop_capture(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2.0)
        if self._serial:
            self._serial.close()
        self._thread = None
        self._serial = None

    def pop_events(self, sample_name: str, started_ms: float) -> List[TriggerEvent]:
        events: List[TriggerEvent] = []
        while True:
            try:
                ts_ms, line = self._lines.get_nowait()
            except queue.Empty:
                break

            if TRIGGER_REGEX.search(line):
                events.append(
                    TriggerEvent(
                        sample_name=sample_name,
                        latency_ms=max(0.0, ts_ms - started_ms),
                        raw_line=line,
                    )
                )
        return events

    def _reader_loop(self) -> None:
        while not self._stop.is_set():
            try:
                raw = self._serial.readline()
                if not raw:
                    continue
                line = raw.decode(errors="replace").rstrip("\r\n")
                self._lines.put((time.time() * 1000.0, line))
            except Exception:
                time.sleep(0.05)

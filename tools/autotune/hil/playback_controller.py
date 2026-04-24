from __future__ import annotations

import platform
import subprocess
import time
import wave
from pathlib import Path


class PlaybackController:
    def __init__(self, gap_ms: int = 800) -> None:
        self._gap_ms = gap_ms

    def play_file(self, wav_path: Path) -> None:
        wav_path = wav_path.resolve()
        system = platform.system().lower()

        if system == "windows":
            self._play_windows(wav_path)
        elif system == "darwin":
            self._play_macos(wav_path)
        else:
            self._play_linux(wav_path)

        time.sleep(self._gap_ms / 1000.0)

    def _play_windows(self, wav_path: Path) -> None:
        import winsound

        winsound.PlaySound(str(wav_path), winsound.SND_FILENAME)

    def _play_macos(self, wav_path: Path) -> None:
        subprocess.run(["afplay", str(wav_path)], check=True)

    def _play_linux(self, wav_path: Path) -> None:
        # Prefer ffplay if available, fallback to aplay.
        ffplay = subprocess.run(["which", "ffplay"], capture_output=True, text=True)
        if ffplay.returncode == 0:
            subprocess.run(
                [
                    "ffplay",
                    "-nodisp",
                    "-autoexit",
                    "-loglevel",
                    "quiet",
                    str(wav_path),
                ],
                check=True,
            )
            return

        subprocess.run(["aplay", str(wav_path)], check=True)

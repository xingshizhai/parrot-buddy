from __future__ import annotations

import json
from typing import Any, Dict

import requests


class OllamaClient:
    def __init__(self, base_url: str, model: str, timeout_sec: int = 45) -> None:
        self._url = base_url.rstrip("/")
        self._model = model
        self._timeout = timeout_sec

    def chat_json(self, system_prompt: str, user_payload: Dict[str, Any]) -> Dict[str, Any]:
        req = {
            "model": self._model,
            "stream": False,
            "format": "json",
            "messages": [
                {"role": "system", "content": system_prompt},
                {"role": "user", "content": json.dumps(user_payload, ensure_ascii=True)},
            ],
        }
        resp = requests.post(f"{self._url}/api/chat", json=req, timeout=self._timeout)
        resp.raise_for_status()

        data = resp.json()
        msg = data.get("message", {}).get("content", "{}")
        return json.loads(msg)

    def ping(self) -> bool:
        try:
            resp = requests.get(f"{self._url}/api/tags", timeout=self._timeout)
            return resp.status_code == 200
        except Exception:
            return False

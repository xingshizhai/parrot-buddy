from __future__ import annotations

from typing import Any, Dict

from tools.autotune.llm.ollama_client import OllamaClient


CRITIC_SYSTEM = (
    "You are a balanced firmware reviewer evaluating parameter proposals for an embedded parrot-call detector. "
    "Accept proposals whose integer parameter values fall within the stated search space and show plausible improvement potential. "
    "Only reject proposals with clear out-of-range values, obviously contradictory settings, or explicit safety risks. "
    "Return strict JSON with keys: accept (bool), violations (list), risk_level (low/medium/high), rationale (string)."
)


def critique_patch(client: OllamaClient, payload: Dict[str, Any]) -> Dict[str, Any]:
    return client.chat_json(CRITIC_SYSTEM, payload)

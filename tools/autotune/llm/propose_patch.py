from __future__ import annotations

from typing import Any, Dict

from tools.autotune.llm.ollama_client import OllamaClient


PROPOSER_SYSTEM = (
    "You are a firmware autotuning assistant. "
    "Return strict JSON with keys: proposed_edits, expected_gain, risk_notes, self_confidence."
)

PARAM_PROPOSER_SYSTEM = (
    "You are a firmware autotuning assistant. "
    "For parameter search, return strict JSON with keys: params, expected_gain, risk_notes, self_confidence. "
    "The params object must contain only integer parameter values and no code edits."
)


def propose_patch(client: OllamaClient, payload: Dict[str, Any]) -> Dict[str, Any]:
    return client.chat_json(PROPOSER_SYSTEM, payload)


def propose_params(client: OllamaClient, payload: Dict[str, Any]) -> Dict[str, Any]:
    return client.chat_json(PARAM_PROPOSER_SYSTEM, payload)

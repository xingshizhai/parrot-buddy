from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Dict, List, Optional

from tools.autotune.llm.critique_patch import critique_patch
from tools.autotune.llm.ollama_client import OllamaClient
from tools.autotune.llm.propose_patch import propose_params


@dataclass
class LlmCandidateResult:
    params: Dict[str, int]
    source: str
    critic_accept: bool
    critic_reason: str


class LlmParameterStrategy:
    def __init__(self, ollama_cfg: Dict[str, Any], search_space: Dict[str, Any]) -> None:
        timeout = int(ollama_cfg.get("timeout_sec", 45))
        proposer_cfg = ollama_cfg["proposer"]
        critic_cfg = ollama_cfg["critic"]

        self._proposer = OllamaClient(
            base_url=proposer_cfg["base_url"],
            model=proposer_cfg["model"],
            timeout_sec=timeout,
        )
        self._critic = OllamaClient(
            base_url=critic_cfg["base_url"],
            model=critic_cfg["model"],
            timeout_sec=timeout,
        )
        self._space = search_space.get("parameters", {})
        self._max_attempts = max(1, int(ollama_cfg.get("parameter_retry_attempts", 3)))

    def is_available(self) -> bool:
        return self._proposer.ping() and self._critic.ping()

    def propose(self, trial_id: int, recent_reports: List[Dict[str, Any]]) -> Optional[LlmCandidateResult]:
        rejection_feedback = ""
        last_rejected: Optional[LlmCandidateResult] = None

        for attempt in range(1, self._max_attempts + 1):
            payload = {
                "trial_id": trial_id,
                "attempt": attempt,
                "search_space": self._space,
                "recent_reports": recent_reports[-5:],
                "task": "Propose integer parameter values only. Do not propose code edits.",
            }
            if rejection_feedback:
                payload["critic_feedback"] = rejection_feedback

            try:
                prop = propose_params(self._proposer, payload)
                raw_params = prop.get("params") or prop.get("proposed_params") or prop.get("proposed_edits") or {}
                normalized = self._normalize_and_clamp(raw_params)
                if not normalized:
                    continue

                critic_payload = {
                    "trial_id": trial_id,
                    "attempt": attempt,
                    "candidate_params": normalized,
                    "search_space": self._space,
                    "recent_reports": recent_reports[-5:],
                    "task": "Review candidate params and return accept true/false.",
                }
                review = critique_patch(self._critic, critic_payload)
                accept = bool(review.get("accept", False))
                reason = str(review.get("rationale", ""))

                if accept:
                    return LlmCandidateResult(
                        params=normalized,
                        source="ollama",
                        critic_accept=True,
                        critic_reason=reason,
                    )

                rejection_feedback = reason
                last_rejected = LlmCandidateResult(
                    params=normalized,
                    source="ollama_rejected",
                    critic_accept=False,
                    critic_reason=reason,
                )
            except Exception:
                continue

        return last_rejected

    def _normalize_and_clamp(self, params: Dict[str, Any]) -> Dict[str, int]:
        out: Dict[str, int] = {}
        for name, spec in self._space.items():
            if name not in params:
                continue
            if spec.get("type") != "int":
                continue

            lo = int(spec["min"])
            hi = int(spec["max"])
            try:
                v = int(params[name])
            except Exception:
                continue

            if v < lo:
                v = lo
            if v > hi:
                v = hi
            out[name] = v
        return out

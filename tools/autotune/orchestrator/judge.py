from __future__ import annotations

from dataclasses import dataclass
from typing import Dict


@dataclass
class TrialMetrics:
    true_positive: int
    false_positive: int
    false_negative: int
    avg_latency_ms: float
    false_triggers_per_hour: float


@dataclass
class TrialScore:
    precision: float
    recall: float
    f1: float
    score: float
    gate_pass: bool


class Judge:
    def __init__(self, gate_rules: Dict):
        self._gates = gate_rules.get("hard_gates", {})

    def evaluate(self, m: TrialMetrics) -> TrialScore:
        precision = m.true_positive / max(1, m.true_positive + m.false_positive)
        recall = m.true_positive / max(1, m.true_positive + m.false_negative)
        f1 = 2 * precision * recall / max(1e-9, precision + recall)

        # Cap false_triggers_per_hour penalty at 20 so extreme simulate-mode
        # rates (driven by short session duration) don't collapse the score.
        capped_ftr = min(m.false_triggers_per_hour, 20.0)
        score = (
            0.60 * f1
            - 0.15 * capped_ftr
            - 0.15 * (m.avg_latency_ms / 1000.0)
            - 0.10 * (1.0 - precision)
        )

        gate_pass = (
            m.false_triggers_per_hour <= float(self._gates.get("max_false_triggers_per_hour", 9999))
            and m.avg_latency_ms <= float(self._gates.get("max_avg_latency_ms", 1e9))
            and precision >= float(self._gates.get("min_precision", 0.0))
            and recall >= float(self._gates.get("min_recall", 0.0))
        )

        return TrialScore(
            precision=precision,
            recall=recall,
            f1=f1,
            score=score,
            gate_pass=gate_pass,
        )

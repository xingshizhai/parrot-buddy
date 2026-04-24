from __future__ import annotations

import random
from dataclasses import dataclass
from typing import Dict, Iterator, List, Any


@dataclass
class TrialCandidate:
    trial_id: int
    params: Dict[str, int]


class ParameterScheduler:
    def __init__(self, search_space: Dict[str, Any], seed: int) -> None:
        self._rng = random.Random(seed)
        self._space = search_space.get("parameters", {})

    def generate(self, max_trials: int) -> Iterator[TrialCandidate]:
        for i in range(1, max_trials + 1):
            yield TrialCandidate(trial_id=i, params=self._sample_once())

    def _sample_once(self) -> Dict[str, int]:
        picked: Dict[str, int] = {}
        for name, spec in self._space.items():
            if spec.get("type") != "int":
                continue
            lo = int(spec["min"])
            hi = int(spec["max"])
            picked[name] = self._rng.randint(lo, hi)
        return picked

from __future__ import annotations

from pathlib import Path
from typing import Iterable, List


class PatchGuard:
    def __init__(self, allowed_prefixes: Iterable[str], project_root: str) -> None:
        self._root = Path(project_root).resolve()
        self._allowed = [p.replace('\\', '/').rstrip('/') for p in allowed_prefixes]

    def is_path_allowed(self, rel_path: str) -> bool:
        norm = rel_path.replace('\\', '/').lstrip('./')
        return any(norm == p or norm.startswith(p + '/') for p in self._allowed)

    def validate_paths(self, changed_paths: Iterable[str]) -> List[str]:
        violations: List[str] = []
        for p in changed_paths:
            if not self.is_path_allowed(p):
                violations.append(p)
        return violations

from __future__ import annotations

import subprocess
from pathlib import Path


def idf_build(project_root: Path) -> int:
    return subprocess.run(["idf.py", "build"], cwd=str(project_root)).returncode


def idf_flash(project_root: Path, port: str) -> int:
    return subprocess.run(["idf.py", "-p", port, "flash"], cwd=str(project_root)).returncode

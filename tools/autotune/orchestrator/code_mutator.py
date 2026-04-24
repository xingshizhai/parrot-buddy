from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Tuple


@dataclass
class TextEdit:
    file_path: str
    find_text: str
    replace_text: str


def normalize_text_edits(raw_edits: List[dict]) -> List[TextEdit]:
    out: List[TextEdit] = []
    for e in raw_edits:
        fp = str(e.get("file_path", "")).strip()
        ft = str(e.get("find_text", ""))
        rt = str(e.get("replace_text", ""))
        if not fp or not ft:
            continue
        out.append(TextEdit(file_path=fp, find_text=ft, replace_text=rt))
    return out


def apply_text_edits(project_root: Path, edits: List[TextEdit]) -> Tuple[List[str], Dict[str, str], str]:
    backups: Dict[str, str] = {}
    changed: List[str] = []

    for edit in edits:
        rel = edit.file_path.replace("\\", "/").lstrip("./")
        target = (project_root / rel).resolve()
        try:
            target.relative_to(project_root.resolve())
        except Exception:
            return changed, backups, f"path escapes project root: {rel}"

        if not target.exists() or not target.is_file():
            return changed, backups, f"target file not found: {rel}"

        text = target.read_text(encoding="utf-8")
        if rel not in backups:
            backups[rel] = text

        hit_count = text.count(edit.find_text)
        if hit_count != 1:
            return changed, backups, f"find_text must match exactly once in {rel}, got={hit_count}"

        new_text = text.replace(edit.find_text, edit.replace_text, 1)
        target.write_text(new_text, encoding="utf-8")
        if rel not in changed:
            changed.append(rel)

    return changed, backups, ""


def restore_files(project_root: Path, backups: Dict[str, str]) -> None:
    for rel, text in backups.items():
        target = (project_root / rel).resolve()
        target.write_text(text, encoding="utf-8")

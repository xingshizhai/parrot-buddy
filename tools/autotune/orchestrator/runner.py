from __future__ import annotations

import argparse
import copy
import csv
import random
import json
import subprocess
from dataclasses import asdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List

import yaml

from tools.autotune.hil.playback_controller import PlaybackController
from tools.autotune.hil.flash_and_run import idf_build, idf_flash
from tools.autotune.llm.ollama_client import OllamaClient
from tools.autotune.llm.propose_patch import propose_patch
from tools.autotune.llm.critique_patch import critique_patch
from tools.autotune.hil.serial_collector import SerialCollector, TriggerEvent
from tools.autotune.orchestrator.code_mutator import apply_text_edits, normalize_text_edits, restore_files
from tools.autotune.orchestrator.judge import Judge, TrialMetrics
from tools.autotune.orchestrator.llm_strategy import LlmParameterStrategy
from tools.autotune.orchestrator.patch_guard import PatchGuard
from tools.autotune.orchestrator.scheduler import ParameterScheduler


def load_yaml(path: Path) -> Dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        return yaml.safe_load(f)


def list_wav_files(audio_root: Path) -> List[Path]:
    return sorted([p for p in audio_root.rglob("*.wav") if p.is_file()])


def build_virtual_wavs(labels: Dict[str, bool], count: int = 40) -> List[Path]:
    if labels:
        return [Path(name) for name in sorted(labels.keys())]

    out: List[Path] = []
    for i in range(count // 2):
        out.append(Path(f"parrot_call_{i:03d}.wav"))
    for i in range(count - len(out)):
        out.append(Path(f"noise_{i:03d}.wav"))
    return out


def naive_ground_truth(name: str) -> bool:
    lower = name.lower()
    return "parrot" in lower or "bird" in lower


def load_labels(labels_file: Path | None) -> Dict[str, bool]:
    if labels_file is None or not labels_file.exists():
        return {}

    labels: Dict[str, bool] = {}
    with labels_file.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            name = (row.get("sample_name") or "").strip()
            flag = (row.get("is_positive") or "").strip().lower()
            if not name:
                continue
            labels[name] = flag in ("1", "true", "yes", "y")
    return labels


def split_wavs(wav_files: List[Path], holdout_ratio: float, seed: int) -> tuple[List[Path], List[Path]]:
    if holdout_ratio <= 0.0:
        return wav_files, []
    if holdout_ratio >= 1.0:
        return [], wav_files

    ordered = sorted(wav_files, key=lambda p: p.name)
    # Deterministic split without adding another RNG dependency path.
    cutoff = int(len(ordered) * (1.0 - holdout_ratio))
    if cutoff <= 0:
        cutoff = 1
    if cutoff >= len(ordered):
        cutoff = len(ordered) - 1
    return ordered[:cutoff], ordered[cutoff:]


def compute_metrics(events: List[TriggerEvent], wav_files: List[Path], labels: Dict[str, bool]) -> TrialMetrics:
    event_by_file: Dict[str, List[TriggerEvent]] = {}
    for e in events:
        event_by_file.setdefault(e.sample_name, []).append(e)

    tp = fp = fn = 0
    latencies: List[float] = []

    for wav in wav_files:
        expected_positive = labels.get(wav.name, naive_ground_truth(wav.name))
        got = event_by_file.get(wav.name, [])
        if expected_positive and got:
            tp += 1
            latencies.append(got[0].latency_ms)
        elif expected_positive and not got:
            fn += 1
        elif (not expected_positive) and got:
            fp += 1

    avg_latency = sum(latencies) / len(latencies) if latencies else 9999.0
    duration_hours = max(1.0 / 60.0, len(wav_files) * 3.0 / 3600.0)
    false_per_hour = fp / duration_hours

    return TrialMetrics(
        true_positive=tp,
        false_positive=fp,
        false_negative=fn,
        avg_latency_ms=avg_latency,
        false_triggers_per_hour=false_per_hour,
    )


def simulate_events(
    wav_files: List[Path],
    labels: Dict[str, bool],
    params: Dict[str, int],
    trial_id: int,
) -> List[TriggerEvent]:
    rng = random.Random(1000 + trial_id)
    events: List[TriggerEvent] = []

    sens = int(params.get("PARROT_DETECT_SENSITIVITY", 3))
    hold = int(params.get("PARROT_TRIGGER_HOLD_FRAMES", 3))
    min_rms = int(params.get("PARROT_MIN_TRIGGER_RMS", 1200))

    # Heuristic simulator: higher min_rms lowers both TP/FP, higher sensitivity lowers FP more.
    tp_base = max(0.15, min(0.95, 0.75 - (min_rms - 1200) / 5000.0 + (3 - hold) * 0.03))
    fp_base = max(0.01, min(0.60, 0.20 - sens * 0.03 + (3 - hold) * 0.02 + (1200 - min_rms) / 7000.0))

    for wav in wav_files:
        name = wav.name
        positive = labels.get(name, naive_ground_truth(name))
        p = tp_base if positive else fp_base
        if rng.random() < p:
            latency = max(30.0, rng.gauss(140.0 + hold * 10.0, 35.0))
            events.append(TriggerEvent(sample_name=name, latency_ms=latency, raw_line="SIMULATED_TRIGGER"))

    return events


def write_report(report_path: Path, payload: Dict[str, Any]) -> None:
    report_path.parent.mkdir(parents=True, exist_ok=True)
    with report_path.open("w", encoding="utf-8") as f:
        json.dump(payload, f, ensure_ascii=True, indent=2)


def write_leaderboard_named(reports_dir: Path, rows: List[Dict[str, Any]], name: str) -> None:
    csv_path = reports_dir / f"leaderboard_{name}.csv"
    md_path = reports_dir / f"leaderboard_{name}.md"

    with csv_path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["rank", "trial_id", "score", "f1", "precision", "recall", "source", "gate_pass"])
        for idx, r in enumerate(rows, 1):
            s = r.get("score", {})
            writer.writerow([
                idx,
                r.get("trial_id"),
                s.get("score"),
                s.get("f1"),
                s.get("precision"),
                s.get("recall"),
                r.get("params_source"),
                s.get("gate_pass"),
            ])

    lines = [f"# Leaderboard ({name})", "", "| Rank | Trial | Score | F1 | Precision | Recall | Source | Gate |", "|---:|---:|---:|---:|---:|---:|---|---|"]
    for idx, r in enumerate(rows, 1):
        s = r.get("score", {})
        lines.append(
            "| {} | {} | {:.4f} | {:.4f} | {:.4f} | {:.4f} | {} | {} |".format(
                idx,
                r.get("trial_id"),
                float(s.get("score", 0.0)),
                float(s.get("f1", 0.0)),
                float(s.get("precision", 0.0)),
                float(s.get("recall", 0.0)),
                r.get("params_source", ""),
                "PASS" if s.get("gate_pass") else "FAIL",
            )
        )
    md_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_leaderboards(reports_dir: Path, rows: List[Dict[str, Any]]) -> None:
    write_leaderboard_named(reports_dir, rows, "all")
    # Backward compatible names
    (reports_dir / "leaderboard.csv").write_text((reports_dir / "leaderboard_all.csv").read_text(encoding="utf-8"), encoding="utf-8")
    (reports_dir / "leaderboard.md").write_text((reports_dir / "leaderboard_all.md").read_text(encoding="utf-8"), encoding="utf-8")

    param_rows = [r for r in rows if not bool(r.get("code_mutation", {}).get("applied", False))]
    mutation_rows = [r for r in rows if bool(r.get("code_mutation", {}).get("applied", False))]
    write_leaderboard_named(reports_dir, param_rows, "params")
    write_leaderboard_named(reports_dir, mutation_rows, "mutations")


def qualifies_for_tier(report: Dict[str, Any], tier_cfg: Dict[str, Any]) -> bool:
    if not report.get("build_ok") or not report.get("flash_ok"):
        return False

    score_dev = report.get("score", {})
    score_holdout = report.get("score_holdout") or {}
    stable = report.get("stable_repeat", {})

    min_score = float(tier_cfg.get("min_score", -1e9))
    min_holdout_score = float(tier_cfg.get("min_holdout_score", -1e9))
    min_stable_mean_score = float(tier_cfg.get("min_stable_mean_score", -1e9))
    require_gate_pass = bool(tier_cfg.get("require_gate_pass", False))
    require_stable_all_pass = bool(tier_cfg.get("require_stable_all_pass", False))

    if float(score_dev.get("score", -1e9)) < min_score:
        return False

    # Only enforce holdout threshold when holdout score exists for this trial.
    if score_holdout and float(score_holdout.get("score", -1e9)) < min_holdout_score:
        return False

    if float(stable.get("mean_score", -1e9)) < min_stable_mean_score:
        return False

    if require_gate_pass and not bool(score_dev.get("gate_pass", False)):
        return False

    if require_stable_all_pass and not bool(stable.get("all_gate_pass", False)):
        return False

    return True


def format_candidate_item(r: Dict[str, Any]) -> Dict[str, Any]:
    return {
        "trial_id": r.get("trial_id"),
        "score": r.get("score"),
        "score_holdout": r.get("score_holdout"),
        "params": r.get("params"),
        "stable_repeat": r.get("stable_repeat"),
        "params_source": r.get("params_source"),
        "code_mutation": r.get("code_mutation"),
    }


def write_promoted_candidates_md(reports_dir: Path, payload: Dict[str, Any]) -> None:
    lines: List[str] = [
        "# Promoted Candidates",
        "",
        f"Generated at: {payload.get('generated_at', '')}",
        "",
        "## Summary",
        "",
        f"- staging_total: {payload.get('staging_total', 0)}",
        f"- production_total: {payload.get('production_total', 0)}",
        "",
    ]

    def emit_table(title: str, rows: List[Dict[str, Any]]) -> None:
        lines.append(f"## {title}")
        lines.append("")
        if not rows:
            lines.append("(none)")
            lines.append("")
            return

        lines.append("| Trial | Dev Score | Dev F1 | Holdout Score | Stable Mean | Source | Mutation |")
        lines.append("|---:|---:|---:|---:|---:|---|---|")
        for row in rows:
            score = row.get("score") or {}
            holdout = row.get("score_holdout") or {}
            stable = row.get("stable_repeat") or {}
            mutation = row.get("code_mutation") or {}
            lines.append(
                "| {} | {:.4f} | {:.4f} | {} | {:.4f} | {} | {} |".format(
                    row.get("trial_id", ""),
                    float(score.get("score", 0.0)),
                    float(score.get("f1", 0.0)),
                    "{:.4f}".format(float(holdout.get("score", 0.0))) if holdout else "-",
                    float(stable.get("mean_score", 0.0)),
                    row.get("params_source", ""),
                    "yes" if bool(mutation.get("applied", False)) else "no",
                )
            )
        lines.append("")

    emit_table("Staging", payload.get("staging_candidates", []))
    emit_table("Production", payload.get("production_candidates", []))
    (reports_dir / "promoted_candidates.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_trial_manifest(
    reports_dir: Path,
    payload: Dict[str, Any],
    config_path: str,
    search_space_path: str,
    gate_rules_path: str,
    env_snapshot: Dict[str, Any],
) -> None:
    manifest = {
        "trial_id": payload.get("trial_id"),
        "created_at": payload.get("created_at"),
        "params": payload.get("params"),
        "params_source": payload.get("params_source"),
        "env_snapshot": env_snapshot,
        "replay": {
            "module": "tools.autotune.orchestrator.replay_trial",
            "command": (
                f"python -m tools.autotune.orchestrator.replay_trial --trial-id {payload.get('trial_id')} "
                f"--config {config_path} --search-space {search_space_path} --gate-rules {gate_rules_path}"
            ),
        },
    }
    write_report(reports_dir / f"trial_{int(payload.get('trial_id', 0)):04d}.manifest.json", manifest)


def apply_params_to_sdkconfig(project_root: Path, params: Dict[str, int]) -> None:
    sdkconfig = project_root / "sdkconfig"
    if not sdkconfig.exists():
        raise RuntimeError(f"sdkconfig not found: {sdkconfig}")

    lines = sdkconfig.read_text(encoding="utf-8").splitlines()
    index_map: Dict[str, int] = {}
    for i, line in enumerate(lines):
        if not line.startswith("CONFIG_") or "=" not in line:
            continue
        key = line.split("=", 1)[0].strip()
        index_map[key] = i

    for k, v in params.items():
        conf = f"CONFIG_{k}"
        newline = f"{conf}={v}"
        if conf in index_map:
            lines[index_map[conf]] = newline
        else:
            lines.append(newline)

    sdkconfig.write_text("\n".join(lines) + "\n", encoding="utf-8")


def read_sdkconfig_text(project_root: Path) -> str:
    sdkconfig = project_root / "sdkconfig"
    if not sdkconfig.exists():
        raise RuntimeError(f"sdkconfig not found: {sdkconfig}")
    return sdkconfig.read_text(encoding="utf-8")


def write_sdkconfig_text(project_root: Path, text: str) -> None:
    sdkconfig = project_root / "sdkconfig"
    sdkconfig.write_text(text, encoding="utf-8")


def get_git_head(project_root: Path) -> str:
    try:
        out = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=str(project_root),
            capture_output=True,
            text=True,
            check=True,
        )
        return out.stdout.strip()
    except Exception:
        return "unknown"


def build_env_snapshot(cfg: Dict[str, Any], gate_rules: Dict[str, Any], project_root: Path) -> Dict[str, Any]:
    playback_cfg = cfg.get("playback", {})
    exp = cfg.get("experiment", {})
    build_cfg = cfg.get("build", {})
    labels_file = playback_cfg.get("labels_file")

    return {
        "workspace_name": cfg.get("workspace_name", ""),
        "project_root": str(project_root),
        "git_head": get_git_head(project_root),
        "playback_mode": playback_cfg.get("mode", "pc"),
        "audio_root": playback_cfg.get("audio_root", ""),
        "labels_file": labels_file,
        "labels_file_exists": bool(labels_file and Path(labels_file).exists()),
        "strategy": exp.get("strategy", "random"),
        "max_trials": exp.get("max_trials", 0),
        "holdout_ratio": exp.get("holdout_ratio", 0.0),
        "build": {
            "enabled": build_cfg.get("enabled", False),
            "flash_after_build": build_cfg.get("flash_after_build", False),
            "dut_flash_port": build_cfg.get("dut_flash_port", ""),
        },
        "hard_gates": gate_rules.get("hard_gates", {}),
        "promotion": gate_rules.get("promotion", {}),
    }


def run_preflight(
    cfg: Dict[str, Any],
    playback_mode: str,
    wav_files: List[Path],
    llm_strategy: LlmParameterStrategy | None,
) -> Dict[str, Any]:
    checks: List[Dict[str, Any]] = []
    ok = True

    audio_root = str(cfg.get("playback", {}).get("audio_root", ""))
    if playback_mode == "simulate":
        checks.append({"name": "audio_dataset", "ok": True, "detail": "simulate mode uses virtual dataset"})
    else:
        has_wavs = len(wav_files) > 0
        checks.append({"name": "audio_dataset", "ok": has_wavs, "detail": f"audio_root={audio_root}, wav_count={len(wav_files)}"})
        ok = ok and has_wavs

    strategy = str(cfg.get("experiment", {}).get("strategy", "random"))
    if strategy == "ollama_hybrid":
        llm_ok = llm_strategy is not None and llm_strategy.is_available()
        checks.append({"name": "ollama", "ok": llm_ok, "detail": "fallback to random when unavailable"})
    else:
        checks.append({"name": "ollama", "ok": True, "detail": "not required by current strategy"})

    if playback_mode != "simulate":
        port = cfg.get("device", {}).get("dut_serial_port", "")
        serial_ok = bool(port)
        checks.append({"name": "serial_port_config", "ok": serial_ok, "detail": f"dut_serial_port={port}"})
        ok = ok and serial_ok
    else:
        checks.append({"name": "serial_port_config", "ok": True, "detail": "simulate mode skips serial"})

    return {"ok": ok, "checks": checks}


def try_code_mutation(
    cfg: Dict[str, Any],
    gate_rules: Dict[str, Any],
    project_root: Path,
    trial_id: int,
    selected_params: Dict[str, int],
    recent_reports: List[Dict[str, Any]],
) -> Dict[str, Any]:
    result: Dict[str, Any] = {
        "enabled": False,
        "applied": False,
        "reason": "disabled",
        "changed_files": [],
        "edits_count": 0,
        "edits": [],
        "backups": {},
    }

    exp = cfg.get("experiment", {})
    if not bool(exp.get("enable_code_mutation", False)):
        return result

    result["enabled"] = True
    source = str(exp.get("code_mutation_source", "ollama")).lower()

    if source == "stub":
        # Safe mutation path for pipeline validation without external LLM dependency.
        rel = "tools/autotune/sandbox/mutation_target.txt"
        target = (project_root / rel).resolve()
        if not target.exists():
            result["reason"] = "stub target missing"
            return result

        text = target.read_text(encoding="utf-8")
        find = "MUTATION_TOKEN=0" if trial_id % 2 == 1 else "MUTATION_TOKEN=1"
        repl = "MUTATION_TOKEN=1" if trial_id % 2 == 1 else "MUTATION_TOKEN=0"

        if text.count(find) != 1:
            # Self-heal by forcing a canonical form, still tracked as an applied mutation.
            backups = {rel: text}
            canonical = "# Autotune mutation sandbox target\n# This file is intentionally used by stub code mutation trials.\nMUTATION_TOKEN=0\n"
            target.write_text(canonical, encoding="utf-8")
            result.update({
                "applied": True,
                "reason": "stub canonical reset",
                "changed_files": [rel],
                "edits_count": 1,
                "backups": backups,
            })
            return result

        backups = {rel: text}
        target.write_text(text.replace(find, repl, 1), encoding="utf-8")
        result.update(
            {
                "applied": True,
                "reason": "stub applied",
                "changed_files": [rel],
                "edits_count": 1,
                "edits": [
                    {
                        "file_path": rel,
                        "find_text": find,
                        "replace_text": repl,
                    }
                ],
                "backups": backups,
            }
        )
        return result

    if source != "ollama":
        result["reason"] = f"unsupported mutation source: {source}"
        return result

    ollama_cfg = cfg.get("ollama", {})
    if not ollama_cfg:
        result["reason"] = "ollama config missing"
        return result

    timeout = int(ollama_cfg.get("timeout_sec", 45))
    proposer_cfg = ollama_cfg.get("proposer", {})
    critic_cfg = ollama_cfg.get("critic", {})
    proposer = OllamaClient(
        base_url=str(proposer_cfg.get("base_url", "")),
        model=str(proposer_cfg.get("model", "")),
        timeout_sec=timeout,
    )
    critic = OllamaClient(
        base_url=str(critic_cfg.get("base_url", "")),
        model=str(critic_cfg.get("model", "")),
        timeout_sec=timeout,
    )

    if not proposer.ping() or not critic.ping():
        result["reason"] = "ollama unavailable"
        return result

    guard = PatchGuard(
        allowed_prefixes=gate_rules.get("allowed_edit_paths", []),
        project_root=str(project_root),
    )
    mut_cfg = gate_rules.get("code_mutation", {})
    max_edits = int(mut_cfg.get("max_edits_per_trial", 4))
    max_files = int(mut_cfg.get("max_files_per_trial", 2))

    prop_payload = {
        "task": "Propose small text edits to improve parrot-call detection while preserving build stability.",
        "trial_id": trial_id,
        "selected_params": selected_params,
        "allowed_paths": gate_rules.get("allowed_edit_paths", []),
        "constraints": {
            "max_edits_per_trial": max_edits,
            "max_files_per_trial": max_files,
            "format": "JSON with key proposed_edits, each edit has file_path/find_text/replace_text",
        },
        "recent_reports": recent_reports[-5:],
    }

    try:
        prop = propose_patch(proposer, prop_payload)
    except Exception as exc:
        result["reason"] = f"propose failed: {exc}"
        return result

    raw_edits = prop.get("proposed_edits", [])
    edits = normalize_text_edits(raw_edits if isinstance(raw_edits, list) else [])
    if not edits:
        result["reason"] = "no valid edits"
        return result

    if len(edits) > max_edits:
        result["reason"] = f"edits exceed max ({len(edits)} > {max_edits})"
        return result

    uniq_files = sorted(set(e.file_path.replace("\\", "/").lstrip("./") for e in edits))
    if len(uniq_files) > max_files:
        result["reason"] = f"files exceed max ({len(uniq_files)} > {max_files})"
        return result

    violations = guard.validate_paths(uniq_files)
    if violations:
        result["reason"] = f"path violations: {violations}"
        return result

    critic_payload = {
        "task": "Review proposed text edits for safety and regression risk.",
        "trial_id": trial_id,
        "proposed_edits": [
            {
                "file_path": e.file_path,
                "find_text": e.find_text,
                "replace_text": e.replace_text,
            }
            for e in edits
        ],
        "allowed_paths": gate_rules.get("allowed_edit_paths", []),
    }
    try:
        review = critique_patch(critic, critic_payload)
    except Exception as exc:
        result["reason"] = f"critic failed: {exc}"
        return result

    if not bool(review.get("accept", False)):
        result["reason"] = f"critic rejected: {review.get('rationale', '')}"
        return result

    changed, backups, err = apply_text_edits(project_root=project_root, edits=edits)
    if err:
        result["reason"] = f"apply failed: {err}"
        return result

    result.update(
        {
            "applied": True,
            "reason": "applied",
            "changed_files": changed,
            "edits_count": len(edits),
            "edits": [
                {
                    "file_path": e.file_path,
                    "find_text": e.find_text,
                    "replace_text": e.replace_text,
                }
                for e in edits
            ],
            "backups": backups,
        }
    )
    return result


def export_promoted_candidates(
    reports_dir: Path,
    report_history: List[Dict[str, Any]],
    gate_rules: Dict[str, Any],
) -> None:
    promotion_cfg = gate_rules.get("promotion", {})
    tiers_cfg = promotion_cfg.get("tiers", {}) if isinstance(promotion_cfg, dict) else {}

    production_tier = {
        "min_score": -1e9,
        "min_holdout_score": -1e9,
        "min_stable_mean_score": -1e9,
        "require_gate_pass": True,
        "require_stable_all_pass": True,
    }
    staging_tier = {
        "min_score": 0.25,
        "min_holdout_score": 0.30,
        "min_stable_mean_score": 0.20,
        "require_gate_pass": False,
        "require_stable_all_pass": False,
    }
    if isinstance(tiers_cfg.get("production"), dict):
        production_tier.update(tiers_cfg.get("production", {}))
    if isinstance(tiers_cfg.get("staging"), dict):
        staging_tier.update(tiers_cfg.get("staging", {}))

    staged = [r for r in report_history if qualifies_for_tier(r, staging_tier)]
    promoted = [r for r in report_history if qualifies_for_tier(r, production_tier)]

    staged = sorted(staged, key=lambda x: float(x.get("score", {}).get("score", -1e9)), reverse=True)
    promoted = sorted(promoted, key=lambda x: float(x.get("score", {}).get("score", -1e9)), reverse=True)

    staged_params = [r for r in staged if not bool(r.get("code_mutation", {}).get("applied", False))]
    staged_mutations = [r for r in staged if bool(r.get("code_mutation", {}).get("applied", False))]
    promoted_params = [r for r in promoted if not bool(r.get("code_mutation", {}).get("applied", False))]
    promoted_mutations = [r for r in promoted if bool(r.get("code_mutation", {}).get("applied", False))]

    payload = {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "staging_total": len(staged),
        "promoted_total": len(promoted),
        "staging_params": [format_candidate_item(r) for r in staged_params],
        "staging_mutations": [format_candidate_item(r) for r in staged_mutations],
        "promoted_params": [format_candidate_item(r) for r in promoted_params],
        "promoted_mutations": [format_candidate_item(r) for r in promoted_mutations],
        "staging_candidates": [format_candidate_item(r) for r in staged],
        "production_candidates": [format_candidate_item(r) for r in promoted],
    }

    write_report(reports_dir / "promoted_candidates.json", payload)
    write_promoted_candidates_md(reports_dir=reports_dir, payload=payload)


def execute_trial(
    trial_id: int,
    selected_params: Dict[str, int],
    cfg: Dict[str, Any],
    build_cfg: Dict[str, Any],
    project_root: Path,
    baseline_sdkconfig: str,
    wav_files: List[Path],
    labels: Dict[str, bool],
    playback_mode: str,
    collector: SerialCollector | None,
    player: PlaybackController | None,
) -> Dict[str, Any]:
    write_sdkconfig_text(project_root, baseline_sdkconfig)
    apply_params_to_sdkconfig(project_root=project_root, params=selected_params)

    build_ok = True
    flash_ok = True
    if bool(build_cfg.get("enabled", False)):
        build_ok = idf_build(project_root) == 0
        if build_ok and bool(build_cfg.get("flash_after_build", False)):
            flash_port = str(build_cfg.get("dut_flash_port", ""))
            flash_ok = idf_flash(project_root, flash_port) == 0

    if not build_ok or not flash_ok:
        return {
            "build_ok": build_ok,
            "flash_ok": flash_ok,
            "events": [],
        }

    if playback_mode == "simulate":
        events = simulate_events(wav_files=wav_files, labels=labels, params=selected_params, trial_id=trial_id)
        return {"build_ok": build_ok, "flash_ok": flash_ok, "events": events}

    if collector is None or player is None:
        raise RuntimeError("collector/player is required in non-simulate mode")

    events: List[TriggerEvent] = []
    collector.start_capture()
    try:
        for wav in wav_files:
            t0 = datetime.now(timezone.utc).timestamp() * 1000.0
            player.play_file(wav)
            events.extend(collector.pop_events(sample_name=wav.name, started_ms=t0))
    finally:
        collector.stop_capture()

    return {"build_ok": build_ok, "flash_ok": flash_ok, "events": events}
def run_parameter_search(
    cfg: Dict[str, Any],
    search_space: Dict[str, Any],
    gate_rules: Dict[str, Any],
    config_path: str,
    search_space_path: str,
    gate_rules_path: str,
) -> None:
    exp = cfg["experiment"]
    playback_cfg = cfg["playback"]
    dut_cfg = cfg["device"]

    scheduler = ParameterScheduler(search_space=search_space, seed=int(exp.get("seed", 42)))

    # Apply simulate-mode overrides so short virtual sessions can pass gates.
    _playback_mode_early = str(playback_cfg.get("mode", "pc")).lower()
    if _playback_mode_early == "simulate":
        _sim_overrides = gate_rules.get("simulate_mode_overrides", {})
        if _sim_overrides:
            gate_rules = copy.deepcopy(gate_rules)
            for _section in ("hard_gates", "promotion"):
                if _section in _sim_overrides:
                    gate_rules.setdefault(_section, {}).update(_sim_overrides[_section])
            print("[info] simulate mode: applied simulate_mode_overrides to gate_rules")

    judge = Judge(gate_rules=gate_rules)

    audio_root = Path(playback_cfg["audio_root"])
    wav_files = list_wav_files(audio_root)

    labels_file = playback_cfg.get("labels_file")
    labels = load_labels(Path(labels_file)) if labels_file else {}

    playback_mode = str(playback_cfg.get("mode", "pc")).lower()
    if not wav_files and playback_mode != "simulate":
        raise RuntimeError(f"No wav files found under: {audio_root}")
    if playback_mode == "simulate":
        wav_files = build_virtual_wavs(labels)

    seed = int(exp.get("seed", 42))
    holdout_ratio = float(exp.get("holdout_ratio", 0.0))
    dev_wavs, holdout_wavs = split_wavs(wav_files, holdout_ratio=holdout_ratio, seed=seed)

    reports_dir = Path(exp["reports_dir"])
    project_root = Path(cfg["project_root"])
    build_cfg = cfg.get("build", {})
    strategy = str(exp.get("strategy", "random"))
    promotion_rules = gate_rules.get("promotion", {})
    best_score = -1e9
    best_trial = None
    baseline_holdout_score = None
    report_history: List[Dict[str, Any]] = []
    mutation_reason_counts: Dict[str, int] = {}

    llm_strategy = None
    if strategy == "ollama_hybrid":
        ollama_cfg = cfg.get("ollama", {})
        if ollama_cfg:
            llm_strategy = LlmParameterStrategy(ollama_cfg=ollama_cfg, search_space=search_space)
            if not llm_strategy.is_available():
                print("[warn] Ollama unavailable, fallback to random strategy")
                llm_strategy = None

    preflight = run_preflight(cfg=cfg, playback_mode=playback_mode, wav_files=wav_files, llm_strategy=llm_strategy)
    if not preflight["ok"]:
        raise RuntimeError(f"preflight failed: {preflight['checks']}")

    baseline_sdkconfig = read_sdkconfig_text(project_root)
    env_snapshot = build_env_snapshot(cfg=cfg, gate_rules=gate_rules, project_root=project_root)
    min_stable_repeats = int(promotion_rules.get("min_stable_repeats", 1))

    collector = None
    player = None
    if playback_mode != "simulate":
        collector = SerialCollector(
            port=dut_cfg["dut_serial_port"],
            baudrate=int(dut_cfg.get("dut_baudrate", 115200)),
        )
        player = PlaybackController(gap_ms=int(playback_cfg.get("inter_case_gap_ms", 800)))

    for cand in scheduler.generate(int(exp["max_trials"])):
        selected_params = copy.deepcopy(cand.params)
        param_source = "random"
        critic_accept = None
        critic_reason = ""

        if llm_strategy is not None:
            llm_cand = llm_strategy.propose(trial_id=cand.trial_id, recent_reports=report_history)
            if llm_cand is not None:
                if llm_cand.critic_accept:
                    selected_params = llm_cand.params
                    param_source = llm_cand.source
                    critic_accept = True
                    critic_reason = llm_cand.critic_reason
                else:
                    critic_accept = False
                    critic_reason = llm_cand.critic_reason

        print(f"[trial {cand.trial_id}] source={param_source} params={selected_params}")

        mutation = try_code_mutation(
            cfg=cfg,
            gate_rules=gate_rules,
            project_root=project_root,
            trial_id=cand.trial_id,
            selected_params=selected_params,
            recent_reports=report_history,
        )
        reason = str(mutation.get("reason", "unknown"))
        mutation_reason_counts[reason] = mutation_reason_counts.get(reason, 0) + 1

        try:
            trial_exec = execute_trial(
                trial_id=cand.trial_id,
                selected_params=selected_params,
                cfg=cfg,
                build_cfg=build_cfg,
                project_root=project_root,
                baseline_sdkconfig=baseline_sdkconfig,
                wav_files=wav_files,
                labels=labels,
                playback_mode=playback_mode,
                collector=collector,
                player=player,
            )
        finally:
            backups = mutation.get("backups", {})
            if backups:
                restore_files(project_root=project_root, backups=backups)
        build_ok = bool(trial_exec["build_ok"])
        flash_ok = bool(trial_exec["flash_ok"])

        if not build_ok or not flash_ok:
            payload = {
                "trial_id": cand.trial_id,
                "params": selected_params,
                "params_source": param_source,
                "critic_accept": critic_accept,
                "critic_reason": critic_reason,
                "build_ok": build_ok,
                "flash_ok": flash_ok,
                "code_mutation": {
                    "enabled": mutation.get("enabled", False),
                    "applied": mutation.get("applied", False),
                    "reason": mutation.get("reason", ""),
                    "changed_files": mutation.get("changed_files", []),
                    "edits_count": mutation.get("edits_count", 0),
                    "edits": mutation.get("edits", []),
                },
                "score": {
                    "precision": 0.0,
                    "recall": 0.0,
                    "f1": 0.0,
                    "score": -1e9,
                    "gate_pass": False,
                },
                "event_count": 0,
                "created_at": datetime.now(timezone.utc).isoformat(),
            }
            write_report(reports_dir / f"trial_{cand.trial_id:04d}.json", payload)
            write_trial_manifest(
                reports_dir=reports_dir,
                payload=payload,
                config_path=config_path,
                search_space_path=search_space_path,
                gate_rules_path=gate_rules_path,
                env_snapshot=env_snapshot,
            )
            report_history.append(payload)
            print("  skipped: build/flash failed")
            continue

        events = trial_exec["events"]

        dev_metrics = compute_metrics(events=events, wav_files=dev_wavs, labels=labels)
        dev_scored = judge.evaluate(dev_metrics)

        holdout_metrics = None
        holdout_scored = None
        if holdout_wavs:
            holdout_metrics = compute_metrics(events=events, wav_files=holdout_wavs, labels=labels)
            holdout_scored = judge.evaluate(holdout_metrics)
            if baseline_holdout_score is None:
                baseline_holdout_score = holdout_scored.score

        gate_pass = dev_scored.gate_pass
        if bool(promotion_rules.get("require_holdout_improvement", False)) and holdout_scored is not None:
            threshold = baseline_holdout_score if baseline_holdout_score is not None else -1e9
            gate_pass = gate_pass and (holdout_scored.score >= threshold)

        stable_repeat = {
            "required": min_stable_repeats,
            "runs": 1,
            "all_gate_pass": gate_pass,
            "mean_score": dev_scored.score,
        }
        if gate_pass and min_stable_repeats > 1:
            repeat_scores = [dev_scored.score]
            repeat_gate_pass = [gate_pass]
            for repeat_idx in range(2, min_stable_repeats + 1):
                repeat_exec = execute_trial(
                    trial_id=cand.trial_id * 100 + repeat_idx,
                    selected_params=selected_params,
                    cfg=cfg,
                    build_cfg=build_cfg,
                    project_root=project_root,
                    baseline_sdkconfig=baseline_sdkconfig,
                    wav_files=wav_files,
                    labels=labels,
                    playback_mode=playback_mode,
                    collector=collector,
                    player=player,
                )
                if not repeat_exec["build_ok"] or not repeat_exec["flash_ok"]:
                    repeat_gate_pass.append(False)
                    repeat_scores.append(-1e9)
                    continue
                repeat_metrics = compute_metrics(events=repeat_exec["events"], wav_files=dev_wavs, labels=labels)
                repeat_scored = judge.evaluate(repeat_metrics)
                repeat_scores.append(repeat_scored.score)
                repeat_gate_pass.append(repeat_scored.gate_pass)

            stable_repeat = {
                "required": min_stable_repeats,
                "runs": len(repeat_scores),
                "all_gate_pass": all(repeat_gate_pass),
                "mean_score": sum(repeat_scores) / len(repeat_scores),
                "scores": repeat_scores,
            }
            gate_pass = gate_pass and stable_repeat["all_gate_pass"]

        payload = {
            "trial_id": cand.trial_id,
            "params": selected_params,
            "params_source": param_source,
            "critic_accept": critic_accept,
            "critic_reason": critic_reason,
            "build_ok": build_ok,
            "flash_ok": flash_ok,
            "code_mutation": {
                "enabled": mutation.get("enabled", False),
                "applied": mutation.get("applied", False),
                "reason": mutation.get("reason", ""),
                "changed_files": mutation.get("changed_files", []),
                "edits_count": mutation.get("edits_count", 0),
                "edits": mutation.get("edits", []),
            },
            "metrics_dev": asdict(dev_metrics),
            "score_dev": asdict(dev_scored),
            "metrics_holdout": asdict(holdout_metrics) if holdout_metrics is not None else None,
            "score_holdout": asdict(holdout_scored) if holdout_scored is not None else None,
            "score": {
                "precision": dev_scored.precision,
                "recall": dev_scored.recall,
                "f1": dev_scored.f1,
                "score": dev_scored.score,
                "gate_pass": gate_pass,
            },
            "stable_repeat": stable_repeat,
            "event_count": len(events),
            "created_at": datetime.now(timezone.utc).isoformat(),
        }
        write_report(reports_dir / f"trial_{cand.trial_id:04d}.json", payload)
        write_trial_manifest(
            reports_dir=reports_dir,
            payload=payload,
            config_path=config_path,
            search_space_path=search_space_path,
            gate_rules_path=gate_rules_path,
            env_snapshot=env_snapshot,
        )
        report_history.append(payload)

        print(
            "  dev: precision={:.3f} recall={:.3f} f1={:.3f} score={:.3f} gate_pass={}".format(
                dev_scored.precision,
                dev_scored.recall,
                dev_scored.f1,
                dev_scored.score,
                gate_pass,
            )
        )
        if holdout_scored is not None:
            print(
                "  holdout: precision={:.3f} recall={:.3f} f1={:.3f} score={:.3f}".format(
                    holdout_scored.precision,
                    holdout_scored.recall,
                    holdout_scored.f1,
                    holdout_scored.score,
                )
            )

        if gate_pass and dev_scored.score > best_score:
            best_score = dev_scored.score
            best_trial = payload

    # Restore baseline after all trials so repository/runtime state is not polluted.
    write_sdkconfig_text(project_root, baseline_sdkconfig)

    summary = {
        "best_score": best_score,
        "best_trial": best_trial,
        "preflight": preflight,
        "env_snapshot": env_snapshot,
        "dataset": {
            "total": len(wav_files),
            "dev": len(dev_wavs),
            "holdout": len(holdout_wavs),
            "labels_loaded": len(labels),
        },
        "leaderboard_top5": sorted(
            [r for r in report_history if r.get("build_ok") and r.get("flash_ok")],
            key=lambda x: float(x.get("score", {}).get("score", -1e9)),
            reverse=True,
        )[:5],
        "mutation_stats": {
            "enabled_trials": sum(1 for r in report_history if bool(r.get("code_mutation", {}).get("enabled", False))),
            "applied_trials": sum(1 for r in report_history if bool(r.get("code_mutation", {}).get("applied", False))),
            "reason_counts": mutation_reason_counts,
        },
        "finished_at": datetime.now(timezone.utc).isoformat(),
    }
    write_report(reports_dir / "summary.json", summary)
    ranked_rows = sorted(
        [r for r in report_history if r.get("build_ok") and r.get("flash_ok")],
        key=lambda x: float(x.get("score", {}).get("score", -1e9)),
        reverse=True,
    )[:20]
    write_leaderboards(reports_dir=reports_dir, rows=ranked_rows)
    export_promoted_candidates(reports_dir=reports_dir, report_history=report_history, gate_rules=gate_rules)
    print(f"Done. Summary written to: {reports_dir / 'summary.json'}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Autotune runner (phase A skeleton)")
    parser.add_argument(
        "--config",
        default="tools/autotune/configs/autotune.yaml",
        help="Path to autotune config yaml",
    )
    parser.add_argument(
        "--search-space",
        default="tools/autotune/configs/search_space.yaml",
        help="Path to parameter search space yaml",
    )
    parser.add_argument(
        "--gate-rules",
        default="tools/autotune/configs/gate_rules.yaml",
        help="Path to gate rules yaml",
    )
    args = parser.parse_args()

    cfg = load_yaml(Path(args.config))
    search_space = load_yaml(Path(args.search_space))
    gate_rules = load_yaml(Path(args.gate_rules))

    mode = cfg.get("experiment", {}).get("mode", "parameter_search")
    if mode != "parameter_search":
        raise NotImplementedError(f"Mode not supported yet: {mode}")

    run_parameter_search(
        cfg,
        search_space,
        gate_rules,
        config_path=args.config,
        search_space_path=args.search_space,
        gate_rules_path=args.gate_rules,
    )


if __name__ == "__main__":
    main()

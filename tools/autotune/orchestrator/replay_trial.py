from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict

from tools.autotune.orchestrator.runner import (
    execute_trial,
    load_labels,
    list_wav_files,
    build_virtual_wavs,
    read_sdkconfig_text,
    write_sdkconfig_text,
    compute_metrics,
    load_yaml,
)
from tools.autotune.orchestrator.judge import Judge
from tools.autotune.hil.serial_collector import SerialCollector
from tools.autotune.hil.playback_controller import PlaybackController


def main() -> None:
    parser = argparse.ArgumentParser(description="Replay a single autotune trial")
    parser.add_argument("--trial-id", type=int, required=True)
    parser.add_argument("--config", default="tools/autotune/configs/autotune.yaml")
    parser.add_argument("--search-space", default="tools/autotune/configs/search_space.yaml")
    parser.add_argument("--gate-rules", default="tools/autotune/configs/gate_rules.yaml")
    parser.add_argument("--reports-dir", default="")
    args = parser.parse_args()

    cfg = load_yaml(Path(args.config))
    gate_rules = load_yaml(Path(args.gate_rules))
    judge = Judge(gate_rules)

    reports_dir = Path(args.reports_dir) if args.reports_dir else Path(cfg["experiment"]["reports_dir"])
    trial_path = reports_dir / f"trial_{args.trial_id:04d}.json"
    if not trial_path.exists():
        raise RuntimeError(f"trial report not found: {trial_path}")

    payload: Dict[str, Any] = json.loads(trial_path.read_text(encoding="utf-8"))
    params = payload.get("params") or {}

    playback_cfg = cfg["playback"]
    playback_mode = str(playback_cfg.get("mode", "pc")).lower()
    audio_root = Path(playback_cfg["audio_root"])
    wav_files = list_wav_files(audio_root)

    labels_file = playback_cfg.get("labels_file")
    labels = load_labels(Path(labels_file)) if labels_file else {}
    if playback_mode == "simulate":
        wav_files = build_virtual_wavs(labels)

    project_root = Path(cfg["project_root"])
    build_cfg = cfg.get("build", {})
    baseline = read_sdkconfig_text(project_root)

    collector = None
    player = None
    if playback_mode != "simulate":
        dut_cfg = cfg["device"]
        collector = SerialCollector(port=dut_cfg["dut_serial_port"], baudrate=int(dut_cfg.get("dut_baudrate", 115200)))
        player = PlaybackController(gap_ms=int(playback_cfg.get("inter_case_gap_ms", 800)))

    try:
        result = execute_trial(
            trial_id=args.trial_id,
            selected_params=params,
            cfg=cfg,
            build_cfg=build_cfg,
            project_root=project_root,
            baseline_sdkconfig=baseline,
            wav_files=wav_files,
            labels=labels,
            playback_mode=playback_mode,
            collector=collector,
            player=player,
        )
        if not result["build_ok"] or not result["flash_ok"]:
            print("Replay failed due to build/flash failure")
            return

        metrics = compute_metrics(result["events"], wav_files, labels)
        scored = judge.evaluate(metrics)

        replay_payload = {
            "trial_id": args.trial_id,
            "params": params,
            "metrics": {
                "true_positive": metrics.true_positive,
                "false_positive": metrics.false_positive,
                "false_negative": metrics.false_negative,
                "avg_latency_ms": metrics.avg_latency_ms,
                "false_triggers_per_hour": metrics.false_triggers_per_hour,
            },
            "score": {
                "precision": scored.precision,
                "recall": scored.recall,
                "f1": scored.f1,
                "score": scored.score,
                "gate_pass": scored.gate_pass,
            },
            "replayed_at": datetime.now(timezone.utc).isoformat(),
        }
        out = reports_dir / f"trial_{args.trial_id:04d}.replay.json"
        out.write_text(json.dumps(replay_payload, ensure_ascii=True, indent=2), encoding="utf-8")
        print(f"Replay report written: {out}")
    finally:
        write_sdkconfig_text(project_root, baseline)


if __name__ == "__main__":
    main()

# Autotune External Agent Runbook

This document is the execution contract for any agent that needs to run the autotune workflow without VS Code.

## Scope

- Run all training from an external terminal, not from VS Code integrated terminals.
- Use LAN Ollama over HTTP API.
- Default LAN workstation:
  - `http://192.168.1.88:11434`
- Default model:
  - `qwen2.5:32b-instruct-q4_k_m`

## Hard Rules

- Do not start training inside VS Code.
- Do not depend on VS Code extensions, tasks, or agent tools.
- Use PowerShell or Windows Terminal opened outside VS Code.
- Keep `build.enabled: false` unless ESP-IDF is fully configured in that external shell.
- Treat `tools/autotune/reports` as the source of truth for results.

## Working Directory

Run all commands from:

```powershell
cd d:\Work\esp32\projects\parrot-buddy
```

## Required Files

- Config:
  - `tools/autotune/configs/autotune.lan_ollama.yaml`
- Search space:
  - `tools/autotune/configs/search_space.yaml`
- Gate rules:
  - `tools/autotune/configs/gate_rules.yaml`
- LAN launcher:
  - `tools/autotune/run_lan_ollama.ps1`

## Prerequisites

1. Python is installed and available in the external shell.
2. Python dependencies are installed:

```powershell
pip install -r tools/autotune/requirements.txt
```

3. LAN Ollama endpoint is reachable:

```powershell
Invoke-RestMethod -Uri "http://192.168.1.88:11434/api/tags" -Method Get
```

4. The model exists on the workstation:

- `qwen2.5:32b-instruct-q4_k_m`

## Recommended Execution Sequence

### Step 1: LAN LLM Health Check

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools/autotune/run_lan_ollama.ps1 `
  -ProposerUrl "http://192.168.1.88:11434" `
  -CriticUrl "http://192.168.1.88:11434" `
  -ProposerModel "qwen2.5:32b-instruct-q4_k_m" `
  -CriticModel "qwen2.5:32b-instruct-q4_k_m"
```

Expected behavior:

- Script prints proposer/critic endpoint checks.
- If endpoint or model is missing, script exits immediately.
- If healthy, script starts the autotune runner.

### Step 2: Smoke Test

Create a temporary 1-trial config and run it:

```powershell
$src = "tools/autotune/configs/autotune.lan_ollama.yaml"
$tmp = Join-Path $env:TEMP "parrot-buddy.autotune.lan.smoketest.yaml"
$text = Get-Content -Path $src -Raw -Encoding UTF8
$text = $text -replace 'max_trials:\s*20', 'max_trials: 1'
Set-Content -Path $tmp -Value $text -Encoding UTF8

powershell -ExecutionPolicy Bypass -File tools/autotune/run_lan_ollama.ps1 `
  -Config $tmp `
  -ProposerUrl "http://192.168.1.88:11434" `
  -CriticUrl "http://192.168.1.88:11434" `
  -ProposerModel "qwen2.5:32b-instruct-q4_k_m" `
  -CriticModel "qwen2.5:32b-instruct-q4_k_m"
```

Pass criteria:

- Console shows `source=ollama` for the trial.
- `tools/autotune/reports/summary.json` is updated.
- `tools/autotune/reports/trial_0001.manifest.json` contains a replay command using a persisted runtime config under `tools/autotune/reports/runtime_configs`.

### Step 3: Short Batch

Create a temporary 5-trial config and run it:

```powershell
$src = "tools/autotune/configs/autotune.lan_ollama.yaml"
$tmp = Join-Path $env:TEMP "parrot-buddy.autotune.lan.batch5.yaml"
$text = Get-Content -Path $src -Raw -Encoding UTF8
$text = $text -replace 'max_trials:\s*20', 'max_trials: 5'
Set-Content -Path $tmp -Value $text -Encoding UTF8

powershell -ExecutionPolicy Bypass -File tools/autotune/run_lan_ollama.ps1 `
  -Config $tmp `
  -ProposerUrl "http://192.168.1.88:11434" `
  -CriticUrl "http://192.168.1.88:11434" `
  -ProposerModel "qwen2.5:32b-instruct-q4_k_m" `
  -CriticModel "qwen2.5:32b-instruct-q4_k_m"
```

Pass criteria:

- Most or all trials should report `source=ollama`.
- `summary.json` should show `strategy: ollama_hybrid` in `env_snapshot`.
- `promoted_candidates.md` should be regenerated.

### Step 4: Full Simulate Training

Run the default 20-trial config:

```powershell
powershell -ExecutionPolicy Bypass -File tools/autotune/run_lan_ollama.ps1
```

Expected outputs:

- `tools/autotune/reports/summary.json`
- `tools/autotune/reports/promoted_candidates.json`
- `tools/autotune/reports/promoted_candidates.md`
- `tools/autotune/reports/leaderboard_all.csv`
- `tools/autotune/reports/runtime_configs/*.yaml`

## How To Judge The Result

Read these files in order:

1. `tools/autotune/reports/summary.json`
2. `tools/autotune/reports/promoted_candidates.md`
3. `tools/autotune/reports/leaderboard_all.md`

Interpretation:

- If `params_source` is mostly `ollama`, LAN LLM integration is healthy.
- If `staging_total > 0` and `production_total = 0`, the current bottleneck is usually gate strictness or stable-repeat variance.
- If top trials have `score_dev.gate_pass: true` but final `score.gate_pass: false`, the stable-repeat gate is the blocker.
- If all trials are `random`, LLM proposal or critic flow regressed.

## Replay Procedure

Use the replay command recorded in each manifest.

Example:

```powershell
python -m tools.autotune.orchestrator.replay_trial --trial-id 1 --config d:\Work\esp32\projects\parrot-buddy\tools\autotune\reports\runtime_configs\autotune.runtime.TIMESTAMP.yaml --search-space tools/autotune/configs/search_space.yaml --gate-rules tools/autotune/configs/gate_rules.yaml
```

Important:

- Always replay with the config path recorded in the manifest.
- Do not swap in `tools/autotune/configs/autotune.yaml` when replaying LAN runs.

## Transition To Real Audio Training

Only switch to real audio after the simulate run is stable.

Required changes:

1. Set `playback.mode: pc` in the active config.
2. Set a real `audio_root`.
3. Set a real `labels_file`.
4. Confirm the DUT serial port is correct.
5. Keep `build.enabled: false` until external ESP-IDF environment is confirmed.

## Common Failure Modes

### LAN endpoint timeout

Symptom:

- `/api/tags` fails or the launcher exits before training.

Action:

- Verify `192.168.1.88:11434` is reachable from the external shell.
- Verify the Ollama service is running on the workstation.

### Model missing

Symptom:

- Launcher reports model not present.

Action:

- Pull the model on the LAN workstation.

### Trials fall back to random

Symptom:

- Trial output prints `source=random`.

Action:

- Inspect `critic_accept` and `critic_reason` in the per-trial report.
- Inspect `summary.json` and verify endpoint/model settings.

### Replay cannot reproduce

Symptom:

- Replay uses the wrong config.

Action:

- Use the exact replay command from the manifest.
- Ensure the referenced file exists under `tools/autotune/reports/runtime_configs`.

## Minimal Command Set For Another Agent

```powershell
cd d:\Work\esp32\projects\parrot-buddy
pip install -r tools/autotune/requirements.txt
powershell -ExecutionPolicy Bypass -File tools/autotune/run_lan_ollama.ps1
Get-Content tools/autotune/reports/summary.json
Get-Content tools/autotune/reports/promoted_candidates.md
```

## Known Issues in Simulate Mode

### Why `staging_total=0` after a 3-trial run

With the original gate_rules.yaml, `max_false_triggers_per_hour=8` was designed for real continuous deployment
(hours of audio). In simulate mode the session is only ~2 minutes (40 virtual files × 3 s), so even a 5% FP rate
maps to ~66 triggers/hour — always failing the gate.

**Fix applied (2026-04-23):** `gate_rules.yaml` now has a `simulate_mode_overrides` block that is automatically
applied by `runner.py` when `playback.mode=simulate`. Effective overrides:

| Setting | Production (real) | Simulate override |
|---|---|---|
| `max_false_triggers_per_hour` | 8 | 200 |
| `max_avg_latency_ms` | 300 | 500 |
| `min_precision` | 0.85 | 0.40 |
| `min_recall` | 0.75 | 0.40 |
| `min_stable_repeats` | 3 | 2 |

### Stable-repeat variance in simulate mode

Each stable-repeat run uses a different RNG seed (based on `trial_id * 100 + repeat_idx`).
Simulate is stochastic so repeat scores can diverge, especially near parameter boundaries.
`min_stable_repeats=2` reduces this noise while still validating consistency.

### Score formula with extreme false-trigger rates

The score formula used to include an unbounded penalty term `−0.15 × false_triggers_per_hour`.
At 66 triggers/hour this collapsed scores to −9.9 or worse even when F1 was decent.

**Fix applied (2026-04-23):** `judge.py` now caps the FTR term at 20 before scoring, so the maximum
false-trigger penalty is −3.0. Gate logic still uses the raw uncapped value.

### Critic over-rejection

The original critic prompt used the word "strict", causing the LLM to reject valid parameter proposals.

**Fix applied (2026-04-23):** `critique_patch.py` now uses a balanced reviewer prompt that accepts proposals
within the search-space bounds unless there is a clear safety risk.

## Do Not Do These Things

- Do not run training from VS Code terminals.
- Do not assume local Ollama is installed.
- Do not replay LAN trials with the default `autotune.yaml`.
- Do not enable build/flash in an external shell unless ESP-IDF is already initialized there.
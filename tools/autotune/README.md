# Autotune Phase A Quickstart

This folder provides a runnable phase-A loop for PC playback + DUT serial log capture + parameter search.

External-agent runbook:

- See `docs/autotune_external_agent_runbook.md` for a non-VS-Code execution procedure intended for other agents and external terminals.

## 1. Install dependencies

```powershell
pip install -r tools/autotune/requirements.txt
```

## 2. Configure

Edit these files:
- `tools/autotune/configs/autotune.yaml`
- `tools/autotune/configs/search_space.yaml`
- `tools/autotune/configs/gate_rules.yaml`

Minimum required config:
- `device.dut_serial_port`
- `playback.audio_root`
- `experiment.max_trials`

Recommended:
- `playback.labels_file` (CSV ground truth)
- `playback.mode` (`pc` or `simulate`)

Optional build/flash:
- `build.enabled: true`
- `build.flash_after_build: true`
- `build.dut_flash_port: COMx`

LLM strategy options:
- `experiment.strategy: random`
- `experiment.strategy: ollama_hybrid`

Optional code mutation (guarded):
- `experiment.enable_code_mutation: true|false`
- `experiment.code_mutation_source: ollama|stub`
- Limits are controlled in `gate_rules.yaml` under `code_mutation`.
- Only paths in `allowed_edit_paths` can be edited.
- Applied edits are automatically restored after each trial.

When using `stub`, the runner mutates `tools/autotune/sandbox/mutation_target.txt` to validate the guard/apply/restore pipeline without requiring Ollama.

When `ollama_hybrid` is enabled, configure LAN Ollama endpoints in `ollama.proposer` and `ollama.critic`.

## 3. Run

```powershell
powershell -ExecutionPolicy Bypass -File tools/autotune/run_phase_a.ps1
```

Run with LAN Ollama API (recommended for formal training):

```powershell
powershell -ExecutionPolicy Bypass -File tools/autotune/run_local_ollama.ps1
```

Or use the LAN-named wrapper script:

```powershell
powershell -ExecutionPolicy Bypass -File tools/autotune/run_lan_ollama.ps1
```

LAN Ollama notes:

- Config files:
  - `tools/autotune/configs/autotune.local_ollama.yaml` (LAN API, backward compatible name)
  - `tools/autotune/configs/autotune.lan_ollama.yaml` (recommended)
- Default endpoints: `http://192.168.1.88:11434` (proposer), `http://192.168.1.88:11434` (critic)
- Default models: `qwen2.5:32b-instruct-q4_k_m` (proposer), `qwen2.5:32b-instruct-q4_k_m` (critic)
- Script behavior:
  - checks LAN endpoints via `/api/tags`
  - verifies target models exist on those endpoints
  - stops if endpoint/model is missing
  - runs the autotune runner with configured API endpoints

Example with explicit workstation endpoint override:

```powershell
powershell -ExecutionPolicy Bypass -File tools/autotune/run_lan_ollama.ps1 `
  -ProposerUrl "http://192.168.1.88:11434" `
  -CriticUrl "http://192.168.1.88:11434"
```

Dry run without hardware:

```powershell
# set playback.mode=simulate in autotune.yaml first
powershell -ExecutionPolicy Bypass -File tools/autotune/run_phase_a.ps1
```

## 4. Outputs

Reports are written to `experiment.reports_dir` (default: `tools/autotune/reports`):
- `trial_0001.json`, `trial_0002.json`, ...
- `trial_0001.manifest.json`, ...
- `leaderboard.csv`
- `leaderboard.md`
- `leaderboard_params.csv` / `leaderboard_params.md`
- `leaderboard_mutations.csv` / `leaderboard_mutations.md`
- `promoted_candidates.json`
- `promoted_candidates.md`
- `summary.json`

`summary.json` includes:
- `preflight` checks
- `env_snapshot` (strategy/playback/build/gates/git head)

Each trial report contains:
- selected params
- params source (`random` or `ollama`)
- build/flash status
- dev metrics + score
- holdout metrics + score (when holdout split enabled)
- gate result

Ground-truth labels CSV format:

```csv
sample_name,is_positive
parrot_call_001.wav,1
noise_tv_001.wav,0
```

If `labels_file` is missing or sample not listed, filename heuristic is used (`parrot`/`bird`).

Replay a specific trial:

```powershell
python -m tools.autotune.orchestrator.replay_trial --trial-id 3
```

Promotion stability gate:

- Controlled by `promotion.min_stable_repeats` in `gate_rules.yaml`.
- If a trial initially passes gate and `min_stable_repeats > 1`, runner re-executes the same params.
- Promotion requires all repeat runs to pass gate.

Tiered promotion:

- Controlled by `promotion.tiers` in `gate_rules.yaml`.
- `staging` can be configured as a looser candidate pool for follow-up validation.
- `production` remains strict (default: requires gate pass + stable repeats all pass).
- Export includes both `staging_candidates` and `production_candidates`.

## 5. Notes

- The runner restores `sdkconfig` baseline after all trials.
- In each trial, it restores baseline first and then applies candidate params to avoid trial contamination.
- In `ollama_hybrid` mode, if LAN Ollama is unavailable, runner falls back to random strategy.

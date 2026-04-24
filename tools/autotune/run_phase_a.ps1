param(
    [string]$Config = "tools/autotune/configs/autotune.yaml",
    [string]$SearchSpace = "tools/autotune/configs/search_space.yaml",
    [string]$GateRules = "tools/autotune/configs/gate_rules.yaml"
)

$ErrorActionPreference = "Stop"

python -m tools.autotune.orchestrator.runner `
  --config $Config `
  --search-space $SearchSpace `
  --gate-rules $GateRules

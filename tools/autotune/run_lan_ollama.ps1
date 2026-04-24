param(
    [string]$Config = "tools/autotune/configs/autotune.lan_ollama.yaml",
    [string]$SearchSpace = "tools/autotune/configs/search_space.yaml",
    [string]$GateRules = "tools/autotune/configs/gate_rules.yaml",
    [string]$ProposerUrl = "http://192.168.1.88:11434",
    [string]$ProposerModel = "qwen2.5:32b-instruct-q4_k_m",
    [string]$CriticUrl = "http://192.168.1.88:11434",
    [string]$CriticModel = "qwen2.5:32b-instruct-q4_k_m"
)

powershell -ExecutionPolicy Bypass -File tools/autotune/run_local_ollama.ps1 `
    -Config $Config `
    -SearchSpace $SearchSpace `
    -GateRules $GateRules `
    -ProposerUrl $ProposerUrl `
    -ProposerModel $ProposerModel `
    -CriticUrl $CriticUrl `
    -CriticModel $CriticModel

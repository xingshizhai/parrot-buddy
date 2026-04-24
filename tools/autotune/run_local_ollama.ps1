param(
    [string]$Config = "tools/autotune/configs/autotune.local_ollama.yaml",
    [string]$SearchSpace = "tools/autotune/configs/search_space.yaml",
    [string]$GateRules = "tools/autotune/configs/gate_rules.yaml",
    [string]$ProposerUrl = "http://192.168.1.88:11434",
    [string]$ProposerModel = "qwen2.5:32b-instruct-q4_k_m",
    [string]$CriticUrl = "http://192.168.1.88:11434",
    [string]$CriticModel = "qwen2.5:32b-instruct-q4_k_m"
)

$ErrorActionPreference = "Stop"

function Test-OllamaEndpoint {
    param(
        [Parameter(Mandatory = $true)][string]$BaseUrl,
        [Parameter(Mandatory = $true)][string]$ExpectedModel
    )

    try {
        $resp = Invoke-RestMethod -Uri "$BaseUrl/api/tags" -Method Get -TimeoutSec 8
        $models = @($resp.models | Select-Object -ExpandProperty name)
        $hasModel = $models -contains $ExpectedModel
        return @{
            ok = $true
            has_model = $hasModel
            model_count = $models.Count
            detail = "reachable"
        }
    } catch {
        return @{
            ok = $false
            has_model = $false
            model_count = 0
            detail = $_.Exception.Message
        }
    }
}

$proposerState = Test-OllamaEndpoint -BaseUrl $ProposerUrl -ExpectedModel $ProposerModel
$criticState = Test-OllamaEndpoint -BaseUrl $CriticUrl -ExpectedModel $CriticModel

Write-Host "[check] proposer url: $ProposerUrl ok=$($proposerState.ok) model=$ProposerModel present=$($proposerState.has_model)"
Write-Host "[check] critic   url: $CriticUrl ok=$($criticState.ok) model=$CriticModel present=$($criticState.has_model)"

if (-not $proposerState.ok) {
    Write-Host "[error] proposer endpoint unreachable: $($proposerState.detail)" -ForegroundColor Red
    exit 1
}
if (-not $criticState.ok) {
    Write-Host "[error] critic endpoint unreachable: $($criticState.detail)" -ForegroundColor Red
    exit 1
}
if (-not $proposerState.has_model) {
    Write-Host "[error] proposer model missing on endpoint: $ProposerModel" -ForegroundColor Red
    exit 1
}
if (-not $criticState.has_model) {
    Write-Host "[error] critic model missing on endpoint: $CriticModel" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $Config)) {
    Write-Host "[error] config not found: $Config" -ForegroundColor Red
    exit 1
}

$configText = Get-Content -Path $Config -Raw -Encoding UTF8
$configText = $configText -replace '(?m)(^\s*proposer:\s*\r?\n\s*base_url:\s*).+$', ('$1' + $ProposerUrl)
$configText = $configText -replace '(?m)(^\s*proposer:\s*\r?\n\s*base_url:\s*.+\r?\n\s*model:\s*).+$', ('$1' + $ProposerModel)
$configText = $configText -replace '(?m)(^\s*critic:\s*\r?\n\s*base_url:\s*).+$', ('$1' + $CriticUrl)
$configText = $configText -replace '(?m)(^\s*critic:\s*\r?\n\s*base_url:\s*.+\r?\n\s*model:\s*).+$', ('$1' + $CriticModel)

$reportsDir = ""
$match = [regex]::Match($configText, '(?m)^\s*reports_dir:\s*(.+)\s*$')
if ($match.Success) {
    $reportsDir = $match.Groups[1].Value.Trim()
}

if ([string]::IsNullOrWhiteSpace($reportsDir)) {
    $reportsDir = Join-Path (Split-Path -Parent $Config) "..\reports"
}

$runtimeDir = Join-Path $reportsDir "runtime_configs"
New-Item -ItemType Directory -Force -Path $runtimeDir | Out-Null
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$runtimeConfig = Join-Path $runtimeDir ("autotune.runtime.{0}.yaml" -f $stamp)
Set-Content -Path $runtimeConfig -Value $configText -Encoding UTF8

Write-Host "[info] Running autotune with LAN Ollama API endpoints..."
python -m tools.autotune.orchestrator.runner --config $runtimeConfig --search-space $SearchSpace --gate-rules $GateRules

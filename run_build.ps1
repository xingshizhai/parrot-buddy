param(
    [switch]$CleanFirst,
    [switch]$Flash
)

$ErrorActionPreference = "Stop"

. 'D:\Work\esp32\projects\parrot-buddy\build_esp32.ps1' -ActivateOnly

$projectRoot = 'D:\Work\esp32\projects\parrot-buddy'
$buildDir = Join-Path $projectRoot 'build'
$buildNinja = Join-Path $buildDir 'build.ninja'

Write-Host "IDF_PATH: $env:IDF_PATH"
Write-Host "idf.py location: $(Get-Command idf.py | Select-Object -ExpandProperty Definition)"

if ($Flash) {
    Write-Host "About to run idf.py -p COM3 flash"
    idf.py -p COM3 flash
    $exit = $LASTEXITCODE
    Write-Host "idf.py flash exit code: $exit"
} else {
    if ($CleanFirst) {
        Write-Host "CleanFirst 已开启，先执行 idf.py fullclean..."
        idf.py fullclean
        if ($LASTEXITCODE -ne 0) {
            $cleanExit = $LASTEXITCODE
            Write-Host "idf.py fullclean 失败，退出码: $cleanExit"
            exit $cleanExit
        }
    }

    if ($CleanFirst -or -not (Test-Path -LiteralPath $buildNinja)) {
        Write-Host "准备执行 idf.py reconfigure..."
        idf.py reconfigure
        if ($LASTEXITCODE -ne 0) {
            $reconfExit = $LASTEXITCODE
            Write-Host "idf.py reconfigure 失败，退出码: $reconfExit"
            exit $reconfExit
        }
    }

    Write-Host "About to run idf.py build"
    idf.py build
    $exit = $LASTEXITCODE
    Write-Host "idf.py exit code: $exit"
    $binPath = Join-Path $buildDir 'parrot-buddy.bin'
    Write-Host "Binary exists: $(Test-Path $binPath)"
}

$logDir = Join-Path $buildDir 'log'
if (Test-Path $logDir) {
    Write-Host "Latest build/log files:"
    Get-ChildItem $logDir | Sort-Object LastWriteTime -Descending | Select-Object -First 5 | ForEach-Object {
        Write-Host "  $($_.Name) ($($_.Length) bytes)"
    }
}

exit $exit

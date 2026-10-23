param([switch]$Visual)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
if (-not (Test-Path -LiteralPath (Join-Path $repo 'build\SteadyBrightness.exe'))) { throw 'Run build.cmd first.' }
$suites = @('test-app.ps1', 'test-slider-accessibility.ps1')
if ($Visual) { $suites += @('test-slider-input.ps1', 'test-themes.ps1') }
foreach ($suite in $suites) {
    Write-Host "Running $suite"
    & "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo "tests\$suite")
    if ($LASTEXITCODE -ne 0) { throw "$suite failed (exit $LASTEXITCODE)." }
}
Write-Host 'All requested suites passed. Results: test-results/'

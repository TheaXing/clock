param([switch]$IncludeLegacy)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$testCommands = @(
    'BSP/AHT20/tests/run_tests.ps1',
    'BSP/Console/tests/run_tests.ps1',
    'BSP/RTC/tests/run_tests.ps1',
    'BSP/OTA/tests/run_tests.ps1',
    'BSP/OTA/tests/run_http_tests.ps1',
    'BSP/OTA/tests/run_boot_net_tests.ps1',
    'BSP/OTA/tests/run_boot_policy_tests.ps1',
    'BSP/OTA/tests/run_app_tests.ps1'
)
foreach ($testCommand in $testCommands) {
    & (Join-Path $repoRoot $testCommand)
    if ($LASTEXITCODE -ne 0) { throw "Test failed: $testCommand" }
}
& node --test (Join-Path $repoRoot 'tools/ota/test_ota.mjs')
if ($LASTEXITCODE -ne 0) { throw 'OTA host tools failed.' }
if ($IncludeLegacy) {
    $legacyRunner = Join-Path $repoRoot 'tests/run_host_tests.ps1'
    if (!(Test-Path -LiteralPath $legacyRunner)) { throw 'Ignored legacy test runner is absent in this checkout.' }
    & $legacyRunner
    if ($LASTEXITCODE -ne 0) { throw 'Legacy checkout tests failed.' }
}

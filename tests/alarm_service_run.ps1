$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$testBuild = Join-Path $repoRoot 'build/host-tests/alarm-service'
$null = New-Item -ItemType Directory -Force -Path $testBuild
$vcvars = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
$batch = Join-Path $testBuild 'alarm_service.cmd'
@"
@echo off
where cl >nul 2>nul
if errorlevel 1 call "$vcvars" >nul
if errorlevel 1 exit /b 1
cd /d "$testBuild"
cl /nologo /std:c11 /W4 /WX /I"$repoRoot/tests/alarm_service_stubs" /I"$repoRoot/app/alarm" /I"$repoRoot/app" /I"$repoRoot/BSP/RTC" /I"$repoRoot/BSP/Keys" /I"$repoRoot/BSP/Buzzer" /I"$repoRoot/BSP/BL24C512" /I"$repoRoot/BSP/Console" "$repoRoot/tests/alarm_service_test.c" "$repoRoot/app/alarm/alarm_service.c" "$repoRoot/app/alarm/alarm_engine.c" "$repoRoot/app/alarm/alarm_menu.c" "$repoRoot/app/alarm/alarm_store.c" /Fe:alarm_service_test.exe
if errorlevel 1 exit /b 1
exit /b 0
"@ | Set-Content -LiteralPath $batch -Encoding ascii
& $env:ComSpec /d /c $batch
if ($LASTEXITCODE -ne 0) { throw "Alarm service tests failed ($LASTEXITCODE)" }
foreach ($scenario in 1..21) {
    & (Join-Path $testBuild 'alarm_service_test.exe') $scenario
    if ($LASTEXITCODE -ne 0) { throw "Alarm service scenario $scenario failed ($LASTEXITCODE)" }
}

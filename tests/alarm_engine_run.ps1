$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$testBuild = Join-Path $repoRoot 'build/host-tests/alarm-engine'
$null = New-Item -ItemType Directory -Force -Path $testBuild
$vcvars = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
$batch = Join-Path $testBuild 'alarm_engine.cmd'
@"
@echo off
where cl >nul 2>nul
if errorlevel 1 call "$vcvars" >nul
if errorlevel 1 exit /b 1
cd /d "$testBuild"
cl /nologo /std:c11 /W4 /WX /I"$repoRoot/app/alarm" /I"$repoRoot/BSP/RTC" "$repoRoot/tests/alarm_engine_test.c" "$repoRoot/app/alarm/alarm_engine.c" /Fe:alarm_engine_test.exe
if errorlevel 1 exit /b 1
alarm_engine_test.exe
exit /b %errorlevel%
"@ | Set-Content -LiteralPath $batch -Encoding ascii
& $env:ComSpec /d /c $batch
if ($LASTEXITCODE -ne 0) { throw "Alarm engine tests failed ($LASTEXITCODE)" }

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$testBuild = Join-Path $repoRoot 'build/host-tests'
$null = New-Item -ItemType Directory -Force -Path $testBuild
$vcvars = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
if (!(Test-Path -LiteralPath $vcvars)) { throw 'MSVC C compiler not found; install the Visual Studio C++ build tools.' }
$batch = Join-Path $testBuild 'weather.cmd'
@"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b 1
cd /d "$testBuild"
cl /nologo /std:c11 /W4 /WX /wd4244 /D_CRT_SECURE_NO_WARNINGS /I"$repoRoot/app" "$repoRoot/tests/test_weather.c" "$repoRoot/app/weather.c" /Fe:weather.exe
if errorlevel 1 exit /b 1
weather.exe
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "$repoRoot/tests/renderer_run.ps1"
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "$repoRoot/tests/app_run.ps1"
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "$repoRoot/tests/alarm_menu_run.ps1"
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "$repoRoot/tests/alarm_page_run.ps1"
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "$repoRoot/tests/alarm_engine_run.ps1"
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "$repoRoot/tests/alarm_store_run.ps1"
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "$repoRoot/tests/alarm_service_run.ps1"
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "$repoRoot/tests/hardware_alarm_run.ps1"
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "$repoRoot/BSP/AHT20/tests/run_tests.ps1"
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "$repoRoot/BSP/RTC/tests/run_tests.ps1"
if errorlevel 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "$repoRoot/BSP/Console/tests/run_tests.ps1"
if errorlevel 1 exit /b 1
call "$repoRoot/tests/esp_run_tests.cmd"
exit /b %errorlevel%
"@ | Set-Content -LiteralPath $batch -Encoding ascii
& $env:ComSpec /d /c $batch
if ($LASTEXITCODE -ne 0) { throw "Clock host tests failed ($LASTEXITCODE)" }

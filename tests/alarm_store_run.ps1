param([string]$Compiler = 'cl.exe')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$output = Join-Path $repo 'build/host-alarm-store-tests'
$null = New-Item -ItemType Directory -Path $output -Force
$vcvars = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
$batch = Join-Path $output 'alarm_store.cmd'
@"
@echo off
where "$Compiler" >nul 2>nul
if errorlevel 1 call "$vcvars" >nul
if errorlevel 1 exit /b 1
cd /d "$output"
"$Compiler" /nologo /std:c11 /W4 /WX /utf-8 /I"$repo/app/alarm" /I"$repo/BSP/RTC" "$PSScriptRoot/alarm_store_test.c" "$repo/app/alarm/alarm_store.c" "$repo/app/alarm/alarm_engine.c" /Fe:alarm_store_test.exe
if errorlevel 1 exit /b 1
alarm_store_test.exe
exit /b %errorlevel%
"@ | Set-Content -LiteralPath $batch -Encoding ascii
& $env:ComSpec /d /c $batch
if ($LASTEXITCODE -ne 0) { throw "Alarm store tests failed ($LASTEXITCODE)" }

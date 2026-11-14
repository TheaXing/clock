$ErrorActionPreference = 'Stop'
$otaRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent (Split-Path -Parent $otaRoot)
$testBuild = Join-Path $env:TEMP 'clock-ota-uart-tests'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$devCmd = Join-Path $vsInstall 'Common7/Tools/VsDevCmd.bat'
$nativeCommand = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /I"{1}/uart_stubs" /I"{2}/bootloader" "{2}/bootloader/boot_uart.c" "{1}/test_boot_uart.c" /Fo"{3}\\" /Fe"{3}/test_uart.exe" && "{3}/test_uart.exe"' -f $devCmd, $PSScriptRoot, $repoRoot, $testBuild
& cmd.exe /d /s /c $nativeCommand
if ($LASTEXITCODE -ne 0) { throw 'Boot UART tests failed.' }

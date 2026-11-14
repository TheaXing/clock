$ErrorActionPreference = 'Stop'
$otaRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$testBuild = Join-Path $env:TEMP 'clock-ota-tests'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsInstall) { throw 'MSVC C compiler is required for OTA tests.' }
$devCmd = Join-Path $vsInstall 'Common7/Tools/VsDevCmd.bat'
$sources = @('ota_crypto.c', 'ota_image.c', 'ota_journal.c', 'ota_installer.c') | ForEach-Object { '"' + (Join-Path $otaRoot $_) + '"' }
$nativeCommand = '"{0}" -no_logo -arch=x64 && cl /nologo /std:c11 /utf-8 /W4 /WX /I"{1}" {2} "{3}" /Fo"{4}\\" /Fe"{4}/test_ota.exe" && "{4}/test_ota.exe"' -f $devCmd, $otaRoot, ($sources -join ' '), (Join-Path $PSScriptRoot 'test_ota.c'), $testBuild
& cmd.exe /d /s /c $nativeCommand
if ($LASTEXITCODE -ne 0) { throw 'OTA core tests failed.' }

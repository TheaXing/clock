$ErrorActionPreference = 'Stop'
$otaRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent (Split-Path -Parent $otaRoot)
$testBuild = Join-Path $env:TEMP 'clock-ota-net-tests'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$devCmd = Join-Path $vsInstall 'Common7/Tools/VsDevCmd.bat'
$nativeCommand = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /I"{1}" /I"{2}/bootloader" "{1}/ota_http.c" "{2}/bootloader/boot_net.c" "{3}" /Fo"{4}\\" /Fe"{4}/test_net.exe" && "{4}/test_net.exe"' -f $devCmd, $otaRoot, $repoRoot, (Join-Path $PSScriptRoot 'test_boot_net.c'), $testBuild
& cmd.exe /d /s /c $nativeCommand
if ($LASTEXITCODE -ne 0) { throw 'Boot network tests failed.' }

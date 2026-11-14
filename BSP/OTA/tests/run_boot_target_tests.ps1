$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$testBuild = Join-Path $env:TEMP 'clock-boot-target-tests'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$devCmd = Join-Path $vsInstall 'Common7/Tools/VsDevCmd.bat'
$nativeCommand = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /I"{1}/BSP/OTA" /I"{1}/bootloader" "{1}/bootloader/boot_target.c" "{1}/BSP/OTA/ota_crypto.c" "{1}/BSP/OTA/ota_image.c" "{1}/BSP/OTA/tests/test_boot_target.c" /Fo"{2}\\" /Fe"{2}/test_target.exe" && "{2}/test_target.exe"' -f $devCmd, $repoRoot, $testBuild
& cmd.exe /d /s /c $nativeCommand
if ($LASTEXITCODE -ne 0) { throw 'Boot fixed target tests failed.' }

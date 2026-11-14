$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$otaRoot = Join-Path $repositoryRoot 'BSP/OTA'
$bootRoot = Join-Path $repositoryRoot 'bootloader'
$testBuild = Join-Path $env:TEMP 'clock-boot-policy-tests'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsInstall) { throw 'MSVC C compiler is required for boot policy tests.' }
$devCmd = Join-Path $vsInstall 'Common7/Tools/VsDevCmd.bat'
$nativeCommand = '"{0}" -no_logo -arch=x64 && cl /nologo /std:c11 /utf-8 /W4 /WX /I"{1}" /I"{2}" "{3}" "{4}" "{6}" /Fo"{5}\\" /Fe"{5}/test_boot_policy.exe" && "{5}/test_boot_policy.exe"' -f $devCmd, $otaRoot, $bootRoot, (Join-Path $bootRoot 'boot_policy.c'), (Join-Path $PSScriptRoot 'test_boot_policy.c'), $testBuild, (Join-Path $otaRoot 'ota_journal.c')
& cmd.exe /d /s /c $nativeCommand
if ($LASTEXITCODE -ne 0) { throw 'Boot policy tests failed.' }

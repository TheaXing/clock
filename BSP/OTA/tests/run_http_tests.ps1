$ErrorActionPreference = 'Stop'
$otaRoot = Split-Path -Parent $PSScriptRoot
$testBuild = Join-Path $env:TEMP 'clock-ota-http-tests'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsInstall) { throw 'MSVC is required.' }
$devCmd = Join-Path $vsInstall 'Common7/Tools/VsDevCmd.bat'
$nativeCommand = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /I"{1}" "{2}" "{3}" /Fo"{4}\\" /Fe"{4}/test_http.exe" && "{4}/test_http.exe"' -f $devCmd, $otaRoot, (Join-Path $otaRoot 'ota_http.c'), (Join-Path $PSScriptRoot 'test_http.c'), $testBuild
& cmd.exe /d /s /c $nativeCommand
if ($LASTEXITCODE -ne 0) { throw 'OTA HTTP tests failed.' }

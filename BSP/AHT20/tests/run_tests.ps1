$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not (Test-Path (Join-Path $taskRoot 'aht20.c'))) {
    throw 'AHT20 migration is missing; device behavior tests are red.'
}
$testBuild = Join-Path $env:TEMP 'clock-aht20-tests'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsInstall) { throw 'MSVC C compiler is required for these native tests.' }
$devCmd = Join-Path $vsInstall 'Common7/Tools/VsDevCmd.bat'
$nativeCommand = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /I"{1}" /I"{2}" "{3}" "{4}" /Fo"{5}\\" /Fe"{5}/test_aht20.exe" && "{5}/test_aht20.exe"' -f $devCmd, (Join-Path $PSScriptRoot 'stubs'), $taskRoot, (Join-Path $taskRoot 'aht20.c'), (Join-Path $PSScriptRoot 'test_aht20.c'), $testBuild
& cmd.exe /d /s /c $nativeCommand
if ($LASTEXITCODE -ne 0) { throw 'AHT20 tests failed.' }

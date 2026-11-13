$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not (Test-Path (Join-Path $taskRoot 'clock_console.c'))) {
    throw 'Console migration is missing; device behavior tests are red.'
}
$testBuild = Join-Path $env:TEMP 'clock-console-tests'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsInstall) { throw 'MSVC C compiler is required for these native tests.' }
$devCmd = Join-Path $vsInstall 'Common7/Tools/VsDevCmd.bat'
# MSVC's own CRT exports _write; rename only in this host build to avoid collision.
$nativeCommand = '"{0}" -no_logo -arch=x64 && cl /nologo /std:c11 /W4 /WX /D_write=clock_test_write /FI"{1}/logger_redirect.h" /I"{1}" /I"{2}" "{3}" "{4}" /Fo"{5}\\" /Fe"{5}/test_console.exe" && "{5}/test_console.exe" && "{5}/test_console.exe" fail-first && "{5}/test_console.exe" fail-second' -f $devCmd, (Join-Path $PSScriptRoot 'stubs'), $taskRoot, (Join-Path $taskRoot 'clock_console.c'), (Join-Path $PSScriptRoot 'test_console.c'), $testBuild
& cmd.exe /d /s /c $nativeCommand
if ($LASTEXITCODE -ne 0) { throw 'Console tests failed.' }

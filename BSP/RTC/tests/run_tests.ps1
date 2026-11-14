$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not (Test-Path (Join-Path $taskRoot 'clock_rtc.c'))) {
    throw 'RTC migration is missing; device behavior tests are red.'
}
$testBuild = Join-Path $env:TEMP 'clock-rtc-tests'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsInstall) { throw 'MSVC C compiler is required for these native tests.' }
$devCmd = Join-Path $vsInstall 'Common7/Tools/VsDevCmd.bat'
$nativeCommand = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /I"{1}" /I"{2}" "{3}" "{4}" /Fo"{5}\\" /Fe"{5}/test_clock_rtc.exe" && "{5}/test_clock_rtc.exe"' -f $devCmd, (Join-Path $PSScriptRoot 'stubs'), $taskRoot, (Join-Path $taskRoot 'clock_rtc.c'), (Join-Path $PSScriptRoot 'test_clock_rtc.c'), $testBuild
& cmd.exe /d /s /c $nativeCommand
if ($LASTEXITCODE -ne 0) { throw 'RTC tests failed.' }
$coreRtc = (Resolve-Path (Join-Path $taskRoot '../../Core/Src/rtc.c')).Path
# CubeMX's RTC_Init USER 0 deliberately returns before generated date defaults.
$hardwareCommand = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /wd4702 /I"{1}" "{2}" "{3}" /Fo"{4}\\" /Fe"{4}/test_rtc_hardware.exe" && "{4}/test_rtc_hardware.exe"' -f $devCmd, (Join-Path $PSScriptRoot 'hardware_stubs'), $coreRtc, (Join-Path $PSScriptRoot 'test_rtc_hardware.c'), $testBuild
& cmd.exe /d /s /c $hardwareCommand
if ($LASTEXITCODE -ne 0) { throw 'RTC hardware initialization tests failed.' }

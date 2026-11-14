$ErrorActionPreference = 'Stop'
$moduleRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$testBuild = Join-Path $env:TEMP 'clock-bl24c512-tests'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsInstall) { throw 'MSVC C compiler is required for EEPROM diagnostics tests.' }
$devCmd = Join-Path $vsInstall 'Common7/Tools/VsDevCmd.bat'
foreach ($otaEnabled in @(0, 1)) {
    $nativeCommand = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /DCLOCK_OTA_ENABLED={6} /I"{1}" /I"{2}" "{3}" "{4}" /Fo"{5}\\" /Fe"{5}/test_bl24c512.exe" && "{5}/test_bl24c512.exe"' -f $devCmd, (Join-Path $PSScriptRoot 'stubs'), $moduleRoot, (Join-Path $moduleRoot 'bl24c512.c'), (Join-Path $PSScriptRoot 'test_bl24c512.c'), $testBuild, $otaEnabled
    & cmd.exe /d /s /c $nativeCommand
    if ($LASTEXITCODE -ne 0) { throw "EEPROM diagnostics tests failed (OTA=$otaEnabled)." }
}

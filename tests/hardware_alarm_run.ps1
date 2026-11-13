param([ValidateSet('all', 'keys', 'eeprom', 'buzzer')][string]$Only = 'all')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$testBuild = Join-Path $env:TEMP 'clock-alarm-hardware-tests'
$null = New-Item -ItemType Directory -Force -Path $testBuild
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsInstall) { throw 'MSVC C compiler is required for alarm hardware tests.' }
$devCmd = Join-Path $vsInstall 'Common7/Tools/VsDevCmd.bat'
$common = '/nologo /std:c11 /W4 /WX /I"{0}" /I"{1}" /I"{2}" /I"{3}" /Fo"{4}\\"' -f (Join-Path $PSScriptRoot 'hardware_alarm_stubs'), (Join-Path $repoRoot 'BSP/Keys'), (Join-Path $repoRoot 'BSP/BL24C512'), (Join-Path $repoRoot 'BSP/Buzzer'), $testBuild
foreach ($case in @(@('keys', 'Keys/board_keys.c', ''), @('eeprom', 'BL24C512/bl24c512.c', ''), @('buzzer', 'Buzzer/buzzer.c', '/DCLOCK_BUZZER_ACTIVE=0'), @('buzzer', 'Buzzer/buzzer.c', '/DCLOCK_BUZZER_ACTIVE=1'))) {
    if ($Only -ne 'all' -and $Only -ne $case[0]) { continue }
    $nativeCommand = '"{0}" -no_logo -arch=x64 && cl {1} {2} "{3}" "{4}" /Fe"{5}/alarm_{6}.exe" && "{5}/alarm_{6}.exe"' -f $devCmd, $common, $case[2], (Join-Path $repoRoot ('BSP/' + $case[1])), (Join-Path $PSScriptRoot ('hardware_alarm_' + $case[0] + '_test.c')), $testBuild, $case[0]
    & cmd.exe /d /s /c $nativeCommand
    if ($LASTEXITCODE -ne 0) { throw ('Alarm hardware test failed: ' + $case[0]) }
}

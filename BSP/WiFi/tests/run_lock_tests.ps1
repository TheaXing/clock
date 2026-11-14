$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$testBuild = Join-Path $env:TEMP 'clock-wifi-lock-tests'
$null = New-Item -ItemType Directory -Force -Path $testBuild
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsInstall) { throw 'MSVC C compiler is required.' }
$devCmd = Join-Path $vsInstall 'Common7/Tools/VsDevCmd.bat'
$includes = @('BSP/OTA/tests/app_stubs', 'app/ota', 'BSP/OTA', 'app/alarm', 'app', 'BSP/BL24C512', 'BSP/RTC', 'BSP/ESP_AT', 'BSP/Keys', 'BSP/Buzzer', 'BSP/Console') | ForEach-Object { '/I"' + (Join-Path $repoRoot $_) + '"' }
$sources = @('BSP/OTA/tests/app_alarm_test.c', 'app/alarm/alarm_service.c', 'app/alarm/alarm_engine.c', 'app/alarm/alarm_menu.c', 'app/alarm/alarm_store.c') | ForEach-Object { '"' + (Join-Path $repoRoot $_) + '"' }
foreach ($otaEnabled in @(0, 1)) {
    $command = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /DCLOCK_OTA_ENABLED={4} {1} {2} /Fo"{3}\\" /Fe"{3}/alarm_lock_test.exe"' -f $devCmd, ($includes -join ' '), ($sources -join ' '), $testBuild, $otaEnabled
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw "Alarm lock compilation failed (OTA=$otaEnabled)." }
    foreach ($scenario in 1..7) {
        & (Join-Path $testBuild 'alarm_lock_test.exe') $scenario
        if ($LASTEXITCODE -ne 0) { throw "Alarm lock scenario $scenario failed (OTA=$otaEnabled)." }
    }
}

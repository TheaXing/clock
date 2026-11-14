param([int[]]$OtaModes = @(0, 1), [int[]]$Scenarios = (1..7))
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$testBuild = Join-Path $repoRoot 'build/wifi-validation/app'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsInstall) { throw 'MSVC C compiler is required.' }
$devCmd = Join-Path $vsInstall 'Common7/Tools/VsDevCmd.bat'
$includes = @('BSP/WiFi/tests/app_stubs', 'app', 'app/page', 'app/web', 'app/onenet', 'app/ota', 'BSP/RTC', 'BSP/AHT20', 'BSP/ESP_AT', 'BSP/Console', 'BSP/BL24C512') | ForEach-Object { '/I"' + (Join-Path $repoRoot $_) + '"' }
foreach ($ota in $OtaModes) {
    $command = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /DCLOCK_OTA_ENABLED={1} /FI"{2}/BSP/WiFi/tests/app_stubs/test_config.h" {3} "{2}/BSP/WiFi/tests/test_wifi_app.c" /Fo"{4}\\" /Fe"{4}/test_wifi_app_{1}.exe"' -f $devCmd, $ota, $repoRoot, ($includes -join ' '), $testBuild
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw "Wi-Fi application compilation OTA=$ota failed." }
    foreach ($scenario in $Scenarios) {
        & (Join-Path $testBuild "test_wifi_app_$ota.exe") $scenario
        if ($LASTEXITCODE -ne 0) { throw "Wi-Fi application OTA=$ota scenario=$scenario failed." }
    }
}

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$testBuild = Join-Path $repoRoot 'build/host-tests/ota-app'
$null = New-Item -ItemType Directory -Force -Path $testBuild
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsInstall) { throw 'MSVC C compiler is required.' }
$devCmd = Join-Path $vsInstall 'Common7/Tools/VsDevCmd.bat'
$includes = @('BSP/OTA/tests/app_stubs', 'app/ota', 'BSP/OTA', 'app/alarm', 'app', 'BSP/BL24C512', 'BSP/RTC', 'BSP/ESP_AT') | ForEach-Object { '/I"' + (Join-Path $repoRoot $_) + '"' }
$sources = @('BSP/OTA/tests/test_ota_service.c', 'app/ota/ota_service.c', 'BSP/OTA/ota_crypto.c', 'BSP/OTA/ota_discovery.c') | ForEach-Object { '"' + (Join-Path $repoRoot $_) + '"' }
$command = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX {1} {2} /Fo"{3}\\" /Fe"{3}/test_ota_service.exe"' -f $devCmd, ($includes -join ' '), ($sources -join ' '), $testBuild
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'OTA application compilation failed.' }
foreach ($scenario in @((1..11) + (13..28) + 30)) {
    & (Join-Path $testBuild 'test_ota_service.exe') $scenario
    if ($LASTEXITCODE -ne 0) { throw "OTA application scenario $scenario failed." }
}
$command = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /DOTA_TEST_NO_ADMIN {1} {2} /Fo"{3}\\" /Fe"{3}/app_no_admin_test.exe" && "{3}/app_no_admin_test.exe" 12 && "{3}/app_no_admin_test.exe" 6' -f $devCmd, ($includes -join ' '), ($sources -join ' '), $testBuild
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'OTA empty admin token tests failed.' }
$alarmIncludes = @('BSP/Keys', 'BSP/Buzzer', 'BSP/Console') | ForEach-Object { '/I"' + (Join-Path $repoRoot $_) + '"' }
$alarmSources = @('BSP/OTA/tests/app_alarm_test.c', 'app/alarm/alarm_service.c', 'app/alarm/alarm_engine.c', 'app/alarm/alarm_menu.c', 'app/alarm/alarm_store.c') | ForEach-Object { '"' + (Join-Path $repoRoot $_) + '"' }
$command = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /DCLOCK_OTA_ENABLED=1 {1} {2} {3} /Fo"{4}\\" /Fe"{4}/app_alarm_test.exe"' -f $devCmd, ($includes -join ' '), ($alarmIncludes -join ' '), ($alarmSources -join ' '), $testBuild
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'OTA alarm owner compilation failed.' }
foreach ($scenario in 1..7) {
    & (Join-Path $testBuild 'app_alarm_test.exe') $scenario
    if ($LASTEXITCODE -ne 0) { throw "OTA alarm owner scenario $scenario failed." }
}
$webIncludes = @('app/web', 'BSP/ESP_AT', 'BSP/Console') | ForEach-Object { '/I"' + (Join-Path $repoRoot $_) + '"' }
$webSources = @('BSP/OTA/tests/app_web_test.c', 'app/web/clock_web.c', 'app/web/clock_http.c', 'app/web/web_page.c') | ForEach-Object { '"' + (Join-Path $repoRoot $_) + '"' }
$command = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /DCLOCK_OTA_ENABLED=1 {1} {2} {3} /Fo"{4}\\" /Fe"{4}/app_web_test.exe" && "{4}/app_web_test.exe"' -f $devCmd, ($includes -join ' '), ($webIncludes -join ' '), ($webSources -join ' '), $testBuild
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'OTA web tests failed.' }
$disabledSources = @('BSP/OTA/tests/app_disabled_test.c', 'app/ota/ota_service.c') | ForEach-Object { '"' + (Join-Path $repoRoot $_) + '"' }
$command = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /DCLOCK_OTA_ENABLED=0 {1} {2} /Fo"{3}\\" /Fe"{3}/app_disabled_test.exe" && "{3}/app_disabled_test.exe"' -f $devCmd, ($includes -join ' '), ($disabledSources -join ' '), $testBuild
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'OTA disabled tests failed.' }
$eepromSources = @('BSP/OTA/tests/app_eeprom_test.c', 'BSP/BL24C512/bl24c512.c') | ForEach-Object { '"' + (Join-Path $repoRoot $_) + '"' }
$command = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /DCLOCK_OTA_ENABLED=1 {1} {2} /Fo"{3}\\" /Fe"{3}/app_eeprom_test.exe" && "{3}/app_eeprom_test.exe"' -f $devCmd, ($includes -join ' '), ($eepromSources -join ' '), $testBuild
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'OTA EEPROM transaction tests failed.' }
$uiSources = @('BSP/OTA/tests/app_ui_heartbeat_test.c', 'app/ui.c') | ForEach-Object { '"' + (Join-Path $repoRoot $_) + '"' }
$command = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /DCLOCK_OTA_ENABLED=1 {1} /I"{2}" {3} /Fo"{4}\\" /Fe"{4}/app_ui_heartbeat_test.exe" && "{4}/app_ui_heartbeat_test.exe"' -f $devCmd, ($includes -join ' '), (Join-Path $repoRoot 'BSP/ST7789'), ($uiSources -join ' '), $testBuild
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'OTA UI progress hook tests failed.' }
& node (Join-Path $PSScriptRoot 'app_web_ui_test.mjs')
if ($LASTEXITCODE -ne 0) { throw 'OTA browser behavior tests failed.' }

$resetSources = @('BSP/OTA/tests/test_ota_reset.c', 'app/ota/ota_reset.c') | ForEach-Object { '"' + (Join-Path $repoRoot $_) + '"' }
$command = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX {1} {2} /Fo"{3}\\" /Fe"{3}/test_ota_reset.exe" && "{3}/test_ota_reset.exe"' -f $devCmd, ($includes -join ' '), ($resetSources -join ' '), $testBuild
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'OTA reset snapshot tests failed.' }

$autoSources = @('BSP/OTA/tests/test_ota_autocheck.c', 'app/ota/ota_service.c', 'BSP/OTA/ota_crypto.c', 'BSP/OTA/ota_discovery.c') | ForEach-Object { '"' + (Join-Path $repoRoot $_) + '"' }
$command = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX {1} {2} /Fo"{3}\\" /Fe"{3}/test_ota_autocheck.exe"' -f $devCmd, ($includes -join ' '), ($autoSources -join ' '), $testBuild
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'OTA automatic check compilation failed.' }
foreach ($scenario in 1..28) {
    & (Join-Path $testBuild 'test_ota_autocheck.exe') $scenario
    if ($LASTEXITCODE -ne 0) { throw "OTA automatic check scenario $scenario failed." }
}
$noAutoSources = @('BSP/OTA/tests/app_no_auto_test.c', 'app/ota/ota_service.c', 'BSP/OTA/ota_crypto.c', 'BSP/OTA/ota_discovery.c') | ForEach-Object { '"' + (Join-Path $repoRoot $_) + '"' }
$command = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /DCLOCK_OTA_AUTO_CHECK=0 {1} {2} /Fo"{3}\\" /Fe"{3}/app_no_auto_test.exe" && "{3}/app_no_auto_test.exe"' -f $devCmd, ($includes -join ' '), ($noAutoSources -join ' '), $testBuild
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'OTA explicit automatic check disable tests failed.' }
$command = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /DOTA_TEST_NO_ADMIN {1} {2} /Fo"{3}\\" /Fe"{3}/app_auto_no_admin_test.exe" && "{3}/app_auto_no_admin_test.exe" 4' -f $devCmd, ($includes -join ' '), ($autoSources -join ' '), $testBuild
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'OTA automatic check without manual admin token tests failed.' }
$longUrlSources = @('BSP/OTA/tests/app_long_url_test.c', 'app/ota/ota_service.c', 'BSP/OTA/ota_crypto.c', 'BSP/OTA/ota_discovery.c') | ForEach-Object { '"' + (Join-Path $repoRoot $_) + '"' }
foreach ($accepted in @($false, $true)) {
    $define = if ($accepted) { '/DOTA_TEST_URL_ACCEPTED' } else { '' }
    $command = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /DOTA_TEST_LONG_URL {1} {2} {3} /Fo"{4}\\" /Fe"{4}/app_long_url_test.exe" && "{4}/app_long_url_test.exe"' -f $devCmd, $define, ($includes -join ' '), ($longUrlSources -join ' '), $testBuild
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw 'OTA discovery runtime URL boundary tests failed.' }
}

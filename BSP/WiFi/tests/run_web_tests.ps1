$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$testBuild = Join-Path $repoRoot 'build/wifi-validation/web'
New-Item -ItemType Directory -Force -Path $testBuild | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsInstall) { throw 'MSVC C compiler is required.' }
$devCmd = Join-Path $vsInstall 'Common7/Tools/VsDevCmd.bat'
$command = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /I"{1}/app/web" "{1}/BSP/WiFi/tests/test_wifi_form.c" "{1}/app/web/wifi_form.c" /Fo"{2}\\" /Fe"{2}/test_wifi_form.exe" && "{2}/test_wifi_form.exe"' -f $devCmd, $repoRoot, $testBuild
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi form tests failed.' }
$includes = @('BSP/OTA/tests/app_stubs', 'app/ota', 'BSP/OTA', 'app/alarm', 'app', 'app/web', 'BSP/BL24C512', 'BSP/RTC', 'BSP/ESP_AT', 'BSP/Console') | ForEach-Object { '/I"' + (Join-Path $repoRoot $_) + '"' }
$sources = @('BSP/WiFi/tests/test_wifi_web.c', 'app/web/clock_web.c', 'app/web/clock_http.c', 'app/web/web_page.c', 'app/web/wifi_form.c', 'app/web/wifi_setup_page.c') | ForEach-Object { '"' + (Join-Path $repoRoot $_) + '"' }
foreach ($ota in 0..1) {
    $command = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /DCLOCK_OTA_ENABLED={1} {2} {3} /Fo"{4}\\" /Fe"{4}/test_wifi_web_{1}.exe" && "{4}/test_wifi_web_{1}.exe"' -f $devCmd, $ota, ($includes -join ' '), ($sources -join ' '), $testBuild
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw "Wi-Fi HTTP OTA=$ota tests failed." }
}
& node (Join-Path $PSScriptRoot 'test_wifi_page.mjs')
if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi browser behavior tests failed.' }

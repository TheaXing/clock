$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$output = Join-Path $repo 'build/host-tests/wifi-state'
New-Item -ItemType Directory -Force $output | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'MSVC is required.' }
$dev = Join-Path $vs 'Common7/Tools/VsDevCmd.bat'
$includes = @('BSP/OTA/tests/app_stubs','app','app/web','BSP/WiFi','BSP/ESP_AT','BSP/Console','BSP/BL24C512') | ForEach-Object { '/I"' + (Join-Path $repo $_) + '"' }
$sources = @('BSP/WiFi/tests/test_wifi_state.c','app/wifi.c','app/web/wifi_form.c') | ForEach-Object { '"' + (Join-Path $repo $_) + '"' }
$command = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /D_CRT_SECURE_NO_WARNINGS {1} {2} /Fo"{3}\\" /Fe"{3}/test_wifi_state.exe"' -f $dev, ($includes -join ' '), ($sources -join ' '), $output
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi state compilation failed.' }
foreach ($scenario in @((1..8) + (10..11))) {
    & (Join-Path $output 'test_wifi_state.exe') $scenario
    if ($LASTEXITCODE -ne 0) { throw "Wi-Fi state scenario $scenario failed." }
}
$defaults = Join-Path $PSScriptRoot 'state_defaults.h'
$command = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /FI"{1}" {2} {3} /Fo"{4}\\" /Fe"{4}/test_wifi_defaults.exe" && "{4}/test_wifi_defaults.exe" 9 && "{4}/test_wifi_defaults.exe" 2' -f $dev, $defaults, ($includes -join ' '), ($sources -join ' '), $output
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi default configuration tests failed.' }
$identityIncludes = @('BSP/WiFi/tests/identity_stubs','app','app/web') | ForEach-Object { '/I"' + (Join-Path $repo $_) + '"' }
$identitySources = @('BSP/WiFi/tests/test_wifi_identity.c','app/wifi_identity.c') | ForEach-Object { '"' + (Join-Path $repo $_) + '"' }
$command = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /D_CRT_SECURE_NO_WARNINGS {1} {2} /Fo"{3}\\" /Fe"{3}/test_wifi_identity.exe" && "{3}/test_wifi_identity.exe"' -f $dev, ($identityIncludes -join ' '), ($identitySources -join ' '), $output
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi identity tests failed.' }

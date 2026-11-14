$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$testBuild = Join-Path $env:TEMP 'clock-wifi-boot-tests'
$null = New-Item -ItemType Directory -Force -Path $testBuild
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsInstall) { throw 'MSVC C compiler is required.' }
$devCmd = Join-Path $vsInstall 'Common7/Tools/VsDevCmd.bat'
$includes = @('BSP/WiFi/tests/boot_stubs', 'BSP/OTA/tests/uart_stubs', 'BSP/WiFi', 'bootloader', 'BSP/OTA', 'app') | ForEach-Object { '/I"' + (Join-Path $repoRoot $_) + '"' }
$sources = @('BSP/WiFi/tests/test_boot_wifi.c', 'BSP/WiFi/wifi_store.c', 'bootloader/main.c', 'bootloader/boot_net.c', 'bootloader/boot_target.c', 'bootloader/boot_policy.c', 'BSP/OTA/ota_journal.c', 'BSP/OTA/ota_image.c', 'BSP/OTA/ota_crypto.c', 'BSP/OTA/ota_installer.c', 'BSP/OTA/ota_http.c') | ForEach-Object { '"' + (Join-Path $repoRoot $_) + '"' }
$command = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /Dmain=boot_main {1} {2} /Fo"{3}\\" /Fe"{3}/test_boot_wifi.exe" && "{3}/test_boot_wifi.exe"' -f $devCmd, ($includes -join ' '), ($sources -join ' '), $testBuild
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'Boot Wi-Fi selection tests failed.' }

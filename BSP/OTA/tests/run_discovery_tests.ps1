$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$testBuild = Join-Path $repoRoot 'build/host-tests/ota-discovery'
$null = New-Item -ItemType Directory -Force -Path $testBuild
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsInstall) { throw 'MSVC C compiler is required.' }
$devCmd = Join-Path $vsInstall 'Common7/Tools/VsDevCmd.bat'
$sources = @('BSP/OTA/tests/test_discovery.c', 'BSP/OTA/ota_discovery.c', 'BSP/OTA/ota_image.c', 'BSP/OTA/ota_crypto.c') | ForEach-Object { '"' + (Join-Path $repoRoot $_) + '"' }
$command = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /I"{1}" {2} /Fo"{3}\\" /Fe"{3}/test_discovery.exe" && "{3}/test_discovery.exe"' -f $devCmd, (Join-Path $repoRoot 'BSP/OTA'), ($sources -join ' '), $testBuild
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'OTA discovery tests failed.' }
$commandSources = @('BSP/OTA/tests/test_discovery_command.c', 'BSP/ESP_AT/esp_at_parse.c') | ForEach-Object { '"' + (Join-Path $repoRoot $_) + '"' }
$command = '"{0}" -no_logo -arch=x64 && cl /nologo /utf-8 /std:c11 /W4 /WX /I"{1}" {2} /Fo"{3}\\" /Fe"{3}/test_discovery_command.exe" && "{3}/test_discovery_command.exe"' -f $devCmd, (Join-Path $repoRoot 'BSP/ESP_AT'), ($commandSources -join ' '), $testBuild
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'OTA discovery HTTP command boundary tests failed.' }
